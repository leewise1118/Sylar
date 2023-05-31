#include "iomanager.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include <cstdint>
#include <error.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME( "system" );

IOManager::FdContext::EventContext &
    IOManager::FdContext::getContext( Event event ) {
    switch ( event ) {
    case READ:
        return read;
    case WRITE:
        return write;
    default:
        SYLAR_ASSERT2( false, "getContext" );
    }
}
void IOManager::FdContext::resetContext( EventContext &ctx ) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}
// 触发事件上下文
void IOManager::FdContext::triggerEvent( Event event ) {
    SYLAR_ASSERT( events & event );

    events            = (Event) ( events & ~event );
    EventContext &ctx = getContext( event );
    if ( ctx.cb ) {
        ctx.scheduler->schedule( &ctx.cb );
    } else {
        ctx.scheduler->schedule( &ctx.fiber );
    }
    ctx.scheduler = nullptr;
    return;
}

IOManager::IOManager( size_t thread, bool use_caller, const std::string &name )
    : Scheduler( thread, use_caller, name ) {

    m_epfd = epoll_create( 5000 ); // 创建epoll句柄，事件数目5000
    SYLAR_ASSERT( m_epfd > 0 );

    int rt = pipe( m_tickleFds );
    SYLAR_ASSERT( rt == 0 );

    epoll_event event;
    memset( &event, 0, sizeof( epoll_event ) );
    // 设置事件为读事件，并设置为水平触发模式(LT,level-triggered)
    event.events = EPOLLIN | EPOLLET;
    // 设置事件句柄位于管道读端
    event.data.fd = m_tickleFds[ 0 ];

    // fcntl,file control 根据文件描述词来操作文件的特性
    // 将读端设置为非阻塞
    rt = fcntl( m_tickleFds[ 0 ], F_SETFL, O_NONBLOCK );
    SYLAR_ASSERT( !rt );

    // 将监听的文件描述符添加到epoll实例中
    rt = epoll_ctl( m_epfd, EPOLL_CTL_ADD, m_tickleFds[ 0 ], &event );
    SYLAR_ASSERT( !rt );

    contextResize( 32 );
    start();
}
IOManager::~IOManager() {
    stop();
    close( m_epfd );
    close( m_tickleFds[ 0 ] );
    close( m_tickleFds[ 1 ] );
    for ( size_t i = 0; i < m_fdContexts.size(); ++i ) {
        if ( m_fdContexts[ i ] ) {
            delete m_fdContexts[ i ];
        }
    }
}

void IOManager::contextResize( size_t size ) {
    m_fdContexts.resize( size );
    for ( size_t i = 0; i < m_fdContexts.size(); ++i ) {
        if ( !m_fdContexts[ i ] ) {
            m_fdContexts[ i ]     = new FdContext;
            m_fdContexts[ i ]->fd = i;
        }
    }
}
int IOManager::addEvent( int fd, Event event, Func cb ) {
    FdContext          *fd_ctx = nullptr; // 创建事件上下文
    MutexType::ReadLock Rlock( m_mutex );
    // 如果在事件池里就取出来，如果不在就resize事件池,再取出来
    if ( (int) m_fdContexts.size() > fd ) {
        fd_ctx = m_fdContexts[ fd ];
        Rlock.unlock();
    } else {
        Rlock.unlock();
        MutexType::WriteLock Wlock( m_mutex );
        contextResize( fd * 1.5 );
        fd_ctx = m_fdContexts[ fd ];
    }

    FdContext::MutexType::Lock lock( fd_ctx->mutex );
    if ( SYLAR_UNLIKELY( fd_ctx->events & event ) ) {
        SYLAR_LOG_ERROR( g_logger )
            << "addEvent assert fd=" << fd << " event=" << event
            << " fd_ctx.event =" << fd_ctx->events;
        SYLAR_ASSERT( !( fd_ctx->events & event ) );
    }
    // 事件存在则更改文件描述相关联的事件，事件不存在就在epoll实例上注册目标事件。
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    epoll_event epevent;
    // 边沿触发(ET,edge-trigger)
    epevent.events   = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    // 更改或注册事件, 并关联到fd上
    int rt = epoll_ctl( m_epfd, op, fd, &epevent );

    if ( rt ) {
        SYLAR_LOG_ERROR( g_logger )
            << "epoll_ctl(" << m_epfd << ", " << op << "," << fd << ","
            << epevent.events << "):" << rt << " (" << errno << ") ("
            << strerror( errno ) << ")";
        return -1;
    }

    ++m_pendingEventCount; // 等待执行的事件数量+1
    fd_ctx->events = (Event) ( fd_ctx->events | event );

    FdContext::EventContext &event_ctx = fd_ctx->getContext( event );

    SYLAR_ASSERT( !event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb );
    // 更改事件上下文内容
    event_ctx.scheduler = Scheduler::GetThis();
    if ( cb ) {
        event_ctx.cb.swap( cb );
    } else {
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT2( event_ctx.fiber->getState() == Fiber::EXEC,
                       "state= " << event_ctx.fiber->getState() );
    }
    return 0;
}

bool IOManager::delEvent( int fd, Event event ) {
    MutexType::ReadLock Rlock( m_mutex );
    if ( (int) m_fdContexts.size() <= fd ) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[ fd ];
    Rlock.unlock();

    FdContext::MutexType::Lock lock( fd_ctx->mutex );
    if ( SYLAR_UNLIKELY( !( fd_ctx->events & event ) ) ) {
        return false;
    }
    Event       new_events = (Event) ( fd_ctx->events & ~event );
    int         op         = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;
    int rt           = epoll_ctl( m_epfd, op, fd, &epevent );
    if ( rt ) {
        SYLAR_LOG_ERROR( g_logger )
            << "epoll_ctl(" << m_epfd << ", " << op << "," << fd << ","
            << epevent.events << "):" << rt << " (" << errno << ") ("
            << strerror( errno ) << ")";
        return false;
    }
    --m_pendingEventCount;
    fd_ctx->events                     = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getContext( event );
    fd_ctx->resetContext( event_ctx );
    return true;
}
bool IOManager::cancelEvent( int fd, Event event ) {
    MutexType::ReadLock Rlock( m_mutex );
    if ( (int) m_fdContexts.size() <= fd ) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[ fd ];
    Rlock.unlock();

    FdContext::MutexType::Lock lock( fd_ctx->mutex );
    if ( SYLAR_UNLIKELY( !( fd_ctx->events & event ) ) ) {
        return false;
    }
    Event       new_events = (Event) ( fd_ctx->events & ~event );
    int         op         = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl( m_epfd, op, fd, &epevent );
    if ( rt ) {
        SYLAR_LOG_ERROR( g_logger )
            << "epoll_ctl(" << m_epfd << ", " << op << "," << fd << ","
            << epevent.events << "):" << rt << " (" << errno << ") ("
            << strerror( errno ) << ")";
        return false;
    }
    fd_ctx->triggerEvent( event );
    --m_pendingEventCount;

    return true;
}
bool IOManager::cancelAll( int fd ) {
    MutexType::ReadLock Rlock( m_mutex );
    if ( (int) m_fdContexts.size() <= fd ) {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[ fd ];
    Rlock.unlock();

    FdContext::MutexType::Lock lock( fd_ctx->mutex );
    if ( !( fd_ctx->events ) ) {
        return false;
    }
    int         op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;
    int rt           = epoll_ctl( m_epfd, op, fd, &epevent );
    if ( rt ) {
        SYLAR_LOG_ERROR( g_logger )
            << "epoll_ctl(" << m_epfd << ", " << op << "," << fd << ","
            << epevent.events << "):" << rt << " (" << errno << ") ("
            << strerror( errno ) << ")";
        return false;
    }
    if ( fd_ctx->events & READ ) {
        fd_ctx->triggerEvent( READ );
        --m_pendingEventCount;
    }
    if ( fd_ctx->events & WRITE ) {
        fd_ctx->triggerEvent( WRITE );
        --m_pendingEventCount;
    }
    SYLAR_ASSERT( fd_ctx->events == 0 );

    return true;
}
IOManager *IOManager::GetThis() {
    // 基类向子类转换
    return dynamic_cast<IOManager *>( Scheduler::GetThis() );
}

void IOManager::tickle() {
    if ( !hasIdleThread() ) {
        return;
    }
    // 给写端写数据，写入"T"
    int rt = write( m_tickleFds[ 1 ], "T", 1 );
    SYLAR_ASSERT( rt == 1 );
}
bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping( timeout );
}
bool IOManager::stopping( uint64_t &timeout ) {
    timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCount == 0 &&
           Scheduler::stopping();
}
void IOManager::idle() {
    SYLAR_LOG_DEBUG( g_logger ) << "idle";
    epoll_event *events = new epoll_event[ 64 ]();

    // 智能指针托管怠惰事件
    std::shared_ptr<epoll_event> shared_events(
        events, []( epoll_event *ptr ) { delete[] ptr; } );
    while ( true ) {
        uint64_t next_timeout = 0;
        if ( SYLAR_UNLIKELY( stopping( next_timeout ) ) ) {
            SYLAR_LOG_INFO( g_logger )
                << "name= " << getName() << " idle stopping exit";
            break;
        }

        int rt = 0;
        do {
            static const int MAX_TIMEOUT = 3000;
            if ( next_timeout != ~0ull ) {
                next_timeout = (int) next_timeout > MAX_TIMEOUT ? MAX_TIMEOUT
                                                                : next_timeout;
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            // epoll_wait等待epoll事件从epoll实例中发生，并返回对应的描述符
            rt = epoll_wait(
                m_epfd, events, 64,
                (int) next_timeout ); // 查看事件是否触发,返回事件的长度
            SYLAR_LOG_INFO( g_logger ) << "epoll_wait rt= " << rt;
            if ( rt < 0 && errno == EINTR ) {
            } else {
                break;
            }
        } while ( true );

        std::vector<Func> cbs;
        listExpiredCb( cbs );
        if ( !cbs.empty() ) {
            schedule( cbs.begin(), cbs.end() );
            cbs.clear();
        }

        for ( int i = 0; i < rt; ++i ) {
            epoll_event &event = events[ i ];
            if ( event.data.fd == m_tickleFds[ 0 ] ) {
                uint8_t dummy[ 256 ];
                while ( read( m_tickleFds[ 0 ], &dummy, sizeof( dummy ) ) > 0 )
                    ;
                continue;
            }

            FdContext *fd_ctx = (FdContext *) event.data.ptr; //

            FdContext::MutexType::Lock lock( fd_ctx->mutex );
            if ( event.events & ( EPOLLERR | EPOLLHUP ) ) {
                event.events |= EPOLLIN | EPOLLOUT & fd_ctx->events;
            }
            int real_events = NONE;
            if ( event.events & EPOLLIN ) {
                real_events |= READ;
            }
            if ( event.events & EPOLLOUT ) {
                real_events |= WRITE;
            }
            if ( ( fd_ctx->events & real_events ) == NONE ) {
                continue;
            }
            int left_events = ( fd_ctx->events & ~real_events );
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;
            int rt2         = epoll_ctl( m_epfd, op, fd_ctx->fd, &event );
            if ( rt2 ) {
                SYLAR_LOG_ERROR( g_logger )
                    << "epoll_ctl(" << m_epfd << ", " << op << "," << fd_ctx->fd
                    << "," << event.events << "):" << rt2 << " (" << errno
                    << ") (" << strerror( errno ) << ")";
                continue;
            }
            if ( real_events & READ ) {
                fd_ctx->triggerEvent( READ );
                --m_pendingEventCount;
            }
            if ( real_events & WRITE ) {
                fd_ctx->triggerEvent( WRITE );
                --m_pendingEventCount;
            }
        }
        Fiber::ptr cur     = Fiber::GetThis();
        auto       raw_ptr = cur.get();
        cur.reset();
        raw_ptr->swapOut();
    }
}

void IOManager::onTimeInsertedAtFront() {
    tickle();
}
} // namespace sylar