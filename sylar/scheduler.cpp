#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include "util.h"
namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME( "system" );
static thread_local Scheduler *t_scheduler = nullptr; // 当前线程的协程调度器
static thread_local Fiber *t_fiber = nullptr; // 协程调度器的调度工作线程

// 返回协程调度器
Scheduler *Scheduler::GetThis() {
    return t_scheduler;
}
// 返回调度工作协程
Fiber *Scheduler::GetMainFiber() {
    return t_fiber;
}

Scheduler::Scheduler( size_t threads, bool use_caller, const std::string &name )
    : m_name( std::move( name ) ) {
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
        t_fiber      = m_rootFiber.get();
        m_rootThread = sylar::GetThreadId();
        m_threadIds.push_back( m_rootThread );
    } else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler::~Scheduler() {
    SYLAR_LOG_DEBUG( g_logger ) << "Scheduler::~Scheduler()";
    SYLAR_ASSERT( m_stopping );
    if ( GetThis() == this ) {
        t_scheduler = nullptr;
    }
}
void Scheduler::start() {
    SYLAR_LOG_DEBUG( g_logger ) << "Scheduler::start() ";
    {
        MutexType::Lock lock( m_mutex );
        if ( !m_stopping ) {
            // 正在执行状态
            return;
        }
        m_stopping = false;
        SYLAR_ASSERT( m_threads.empty() );

        // 开始调度，创建调度线程池
        m_threads.resize( m_threadCount );
        // 分配线程到线程池，并绑定各自的实例
        for ( size_t i = 0; i < m_threadCount; i++ ) {
            m_threads[ i ] =
                std::make_shared<Thread>( std::bind( &Scheduler::run, this ),
                                          m_name + "_" + std::to_string( i ) );
            m_threadIds.push_back( m_threads[ i ]->getId() );
        }
    }
}
void Scheduler::stop() {
    SYLAR_LOG_DEBUG( g_logger ) << "Scheduler::stop()";
    m_autoStop = true;

    //
    if ( m_rootFiber && m_threadCount == 0 &&
         ( m_rootFiber->getState() == Fiber::TERM ||
           m_rootFiber->getState() == Fiber::INIT ) ) {
        m_stopping = true;
        if ( stopping() ) {
            return;
        }
    }
    bool exit_on_this_fiber = false;
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
    if ( stopping() ) {
        return;
    }
}
void Scheduler::run() {
    SYLAR_LOG_DEBUG( g_logger ) << "Scheduler::run()";
    setThis();
    // 判断线程是否在线程池里,如果不在，则设置主协程
    if ( GetThreadId() != m_rootThread ) {
        t_fiber = Fiber::GetThis().get();
    }

    Fiber::ptr idle_fiber =
        std::make_shared<Fiber>( std::bind( &Scheduler::idle, this ) );

    Task task;
    while ( true ) {
        task.reset();
        bool tickle_me = false;
        // 分配协程
        {
            MutexType::Lock lock( m_mutex );
            auto            it = m_fibers.begin();
            while ( it != m_fibers.end() ) {
                // 任务指定了某个协程执行，但不是当前协程，跳过，通知其他协程执行
                if ( it->threadId != -1 && it->threadId != GetThreadId() ) {
                    ++it;
                    tickle_me = true;
                    continue;
                }
                SYLAR_ASSERT( it->fiber || it->cb );
                // 当前协程正在执行，跳过，不进行处理
                if ( it->fiber && it->fiber->getState() == Fiber::EXEC ) {
                    ++it;
                    continue;
                }
                // 找到指定协程，拷贝任务
                task = *it;
                m_fibers.erase( it ); // 任务列表移除任务
                ++m_activeThreadCount;
                break;
            }
            tickle_me |= it != m_fibers.end();
        }
        if ( tickle_me ) {
            tickle();
        }
        if ( task.cb ) {
            task.fiber = std::make_shared<Fiber>( std::move( task.cb ) );
            task.cb    = nullptr; // 有必要吗？
                               // Move不是直接把所有权转到task.fiber里面了吗？
        }
        if ( task.fiber && ( task.fiber->getState() != Fiber::TERM &&
                             task.fiber->getState() != Fiber::EXCEPT ) ) {
            task.fiber->swapIn();
            --m_activeThreadCount;

            // 将协程加入任务列表
            if ( task.fiber->getState() == Fiber::READY ) {
                schedule( task.fiber );
            } else if ( task.fiber->getState() != Fiber::TERM &&
                        task.fiber->getState() != Fiber::EXCEPT ) {
                task.fiber->setState( Fiber::HOLD );
            }
            task.reset();
        } else {
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