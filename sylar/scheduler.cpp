#include "hook.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include "util.h"
#include <memory>
namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME( "system" );
static thread_local Scheduler *t_scheduler = nullptr; // 当前线程的协程调度器
static thread_local Fiber *t_scheduler_fiber =
    nullptr; // 协程调度器的调度工作线程

// 返回协程调度器
Scheduler *Scheduler::GetThis() {
    return t_scheduler;
}
// 返回调度工作协程
Fiber *Scheduler::GetMainFiber() {
    return t_scheduler_fiber;
}

Scheduler::Scheduler( size_t threads, bool use_caller, const std::string &name )
    : m_name( name ) {
    SYLAR_ASSERT( threads > 0 );
    // 初始化一个当前线程的调度协程
    if ( use_caller ) {

        // 实例化当前线程为主协程, 因此当前线程==当前线程的主协程
        Fiber::GetThis();
        // 线程池数量减一
        --threads;

        // 确保当前线程只有一个调度器,并且设置当前线程的协程调度器
        SYLAR_ASSERT( GetThis() == nullptr );
        t_scheduler = this;

        // Scheduler::run为实例方法，使用std::bind绑定run函数到协程上
        m_rootFiber.reset(
            new Fiber( std::bind( &Scheduler::run, this ), 0, true ) );
        // 设置当前线程的名字
        Thread::SetName( m_name );

        // 当前线程的执行协程切换为当前协程
        t_scheduler_fiber = m_rootFiber.get();
        m_rootThread      = sylar::GetThreadId();
        m_threadIds.push_back( m_rootThread );
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler::~Scheduler() {
    SYLAR_ASSERT( m_stopping );
    if ( GetThis() == this ) {
        t_scheduler = nullptr;
    }
}
void Scheduler::start() {
    MutexType::Lock lock( m_mutex );
    if ( !m_stopping ) {
        // 正在执行状态
        return;
    }
    m_stopping = false;
    SYLAR_ASSERT( m_threads.empty() );

    // 开始调度，创建调度线程池
    m_threads.resize( m_threadCount );
    // 分配线程到线程池
    for ( size_t i = 0; i < m_threadCount; i++ ) {
        m_threads[ i ] =
            std::make_shared<Thread>( std::bind( &Scheduler::run, this ),
                                      m_name + "_" + std::to_string( i ) );
        m_threadIds.push_back( m_threads[ i ]->getId() );
    }
    lock.unlock();
}
void Scheduler::stop() {
    m_autoStop = true;
    SYLAR_LOG_INFO( g_logger ) << "stop()";

    if ( m_rootFiber && m_threadCount == 0 &&
         ( m_rootFiber->getState() == Fiber::TERM ||
           m_rootFiber->getState() == Fiber::INIT ) ) {
        SYLAR_LOG_INFO( g_logger ) << this << " stopped";
        m_stopping = true;
        if ( stopping() ) {
            return;
        }
    }
    if ( m_rootThread != -1 ) {
        SYLAR_ASSERT( GetThis() == this );

    } else {
        SYLAR_ASSERT( GetThis() != this );
    }
    m_stopping = true;
    for ( size_t i = 0; i < m_threadCount; i++ ) {
        tickle();
    }
    if ( m_rootFiber ) {
        tickle();
    }
    if ( m_rootFiber ) {
        if ( !stopping() ) {
            m_rootFiber->call();
        }
    }
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock( m_mutex );
        thrs.swap( m_threads );
    }
    for ( auto &i : thrs ) {
        i->join();
    }
}
void Scheduler::run() {
    SYLAR_LOG_DEBUG( g_logger ) << m_name << " run";
    set_hook_enable( true );
    setThis();
    // 判断线程是否在线程池里,如果不在，则设置主协程
    if ( GetThreadId() != m_rootThread ) {
        t_scheduler_fiber = Fiber::GetThis().get();
    }

    // 创建idle协程
    SYLAR_LOG_DEBUG( g_logger ) << "create idle fiber";
    Fiber::ptr idle_fiber =
        std::make_shared<Fiber>( std::bind( &Scheduler::idle, this ) );

    SYLAR_LOG_DEBUG( g_logger ) << "create call back function fiber";
    Fiber::ptr cb_fiber;
    Task       task;

    while ( true ) {
        task.reset();
        bool tickle_me = false;
        bool is_active = false;
        // 分配协程
        {
            MutexType::Lock lock( m_mutex );
            auto            it = m_fibers.begin(); // 取任务
            while ( it != m_fibers.end() ) {
                // 该任务不存在，且指定了线程执行，跳过,查找下一个任务
                if ( it->threadId != -1 && it->threadId != GetThreadId() ) {
                    ++it;
                    tickle_me = true;
                    continue;
                }
                SYLAR_ASSERT( it->fiber || it->cb );
                // 该任务的协程存在，且正在被执行，跳过，查找下一个任务
                if ( it->fiber && it->fiber->getState() == Fiber::EXEC ) {
                    ++it;
                    continue;
                }
                // 找到任务，指定到当前任务。
                task = *it;
                m_fibers.erase( it ); // 任务列表移除任务
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
            tickle_me |= it != m_fibers.end();
        }
        // 若任务存在，执行tickle()
        if ( tickle_me ) {
            tickle();
        }
        // 如果任务是函数，将函数创建成协程
        if ( task.fiber && ( task.fiber->getState() != Fiber::TERM &&
                             task.fiber->getState() != Fiber::EXCEPT ) ) {

            // 切换到当前协程执行
            task.fiber->swapIn();
            --m_activeThreadCount;

            // 将协程加入任务列表,并置于链表最后
            if ( task.fiber->getState() == Fiber::READY ) {
                SYLAR_LOG_DEBUG( g_logger ) << "setState to Ready";
                schedule( task.fiber );
            } else if ( task.fiber->getState() != Fiber::TERM &&
                        task.fiber->getState() != Fiber::EXCEPT ) {
                SYLAR_LOG_DEBUG( g_logger ) << "setState to Hold";
                task.fiber->setState( Fiber::HOLD );
            }

            task.reset();
        } else if ( task.cb ) {
            if ( cb_fiber ) {
                cb_fiber->reset( task.cb );
            } else {
                cb_fiber.reset( new Fiber( task.cb ) );
            }
            task.reset();
            cb_fiber->swapIn();
            --m_activeThreadCount;
            if ( cb_fiber->getState() == Fiber::READY ) {
                schedule( cb_fiber );
                cb_fiber.reset();
            } else if ( cb_fiber->getState() == Fiber::EXCEPT ||
                        cb_fiber->getState() == Fiber::TERM ) {
                cb_fiber->reset( nullptr );
            } else { // if(cb_fiber->getState() != Fiber::TERM) {
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        } else {
            if ( is_active ) {
                --m_activeThreadCount;
                continue;
            }
            if ( idle_fiber->getState() == Fiber::TERM ) {
                SYLAR_LOG_INFO( g_logger ) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if ( idle_fiber->getState() != Fiber::TERM &&
                 idle_fiber->getState() != Fiber::EXCEPT ) {
                idle_fiber->setState( Fiber::HOLD );
            }
        }
    }
}
void Scheduler::setThis() {
    t_scheduler = this;
}

void Scheduler::tickle() {
    SYLAR_LOG_INFO( g_logger ) << "tickle";
}
bool Scheduler::stopping() {
    MutexType::Lock lock( m_mutex );
    return m_autoStop && m_stopping && m_fibers.empty() &&
           m_activeThreadCount == 0;
}
void Scheduler::idle() {
    SYLAR_LOG_INFO( g_logger ) << "idle";
    while ( !stopping() ) {
        sylar::Fiber::YieldToHold();
    }
}
} // namespace sylar