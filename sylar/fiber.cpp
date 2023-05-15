#include "fiber.h"
#include "macro.h"
#include "scheduler.h"
#include <ucontext.h>
namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME( "system" );

static std::atomic<uint64_t> s_fiber_id{ 0 };
static std::atomic<uint64_t> s_fiber_count{ 0 };

// 当前线程正在执行的协程
static thread_local Fiber *t_fiber = nullptr;
// 当前线程的主协程
static thread_local Fiber::ptr t_threadFiber = nullptr;

static ConfigVar<uint32_t>::ptr g_fiber_stack_size = Config::Lookup<uint32_t>(
    "fiber.stack_size", 1024 * 1024, "fiber stack size" );

class MallocStackAllocator {
  public:
    static void *Alloc( size_t size ) {
        return malloc( size );
    }
    static void Dealloc( void *vp, size_t size ) {
        return free( vp );
    }
};
using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId() {
    if ( t_fiber ) {
        return t_fiber->getId();
    }
}
// 主协程
Fiber::Fiber() {
    m_state = EXEC;
    SetThis( this );
    if ( getcontext( &m_ctx ) ) {
        SYLAR_ASSERT2( false, "getcontext" );
    }
    ++s_fiber_count;
    SYLAR_LOG_DEBUG( g_logger ) << "Fiber::Fiber";
}

Fiber::Fiber( FiberFunc cb, size_t stacksize, bool use_caller )
    : m_id( ++s_fiber_id )
    , m_cb( cb ) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
    // 分配协程堆栈
    m_stack = StackAllocator::Alloc( m_stacksize );
    if ( getcontext( &m_ctx ) ) {
        SYLAR_ASSERT2( false, "getcontext" );
    }
    // uc_link指向上下文, 通过主协程切换协程，所以设置没nullptr
    m_ctx.uc_link = nullptr;
    // stack_t::uc_stack为栈, ss_sp指向栈顶
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    if ( !use_caller ) {
        makecontext( &m_ctx, &Fiber::MainFunc, 0 );
    } else {
        makecontext( &m_ctx, &Fiber::CallerMainFunc, 0 );
    }

    SYLAR_LOG_DEBUG( g_logger ) << "Fiber::Fiber id=" << m_id;
}

Fiber::~Fiber() {
    --s_fiber_count;
    if ( m_stack ) {
        SYLAR_ASSERT( m_state == TERM || m_state == INIT || m_state == EXCEPT );
        StackAllocator::Dealloc( m_stack, m_stacksize );
    } else {
        SYLAR_ASSERT( !m_cb );
        SYLAR_ASSERT( m_state == EXEC );
        Fiber *cur = t_fiber;
        if ( cur == this ) {
            SetThis( nullptr );
        }
    }
    SYLAR_LOG_DEBUG( g_logger ) << "Fiber::~Fiber id=" << m_id;
}

// 重置协程函数，并重置状态
// INIT, TERM
void Fiber::reset( FiberFunc cb ) {
    SYLAR_ASSERT( m_stack );
    SYLAR_ASSERT( m_state == TERM || m_state == INIT || m_state == EXCEPT );
    m_cb = std::move( cb );
    // 初始化m_ctx,并获取当前上下文
    if ( getcontext( &m_ctx ) ) {
        SYLAR_ASSERT2( false, "getcontext" );
    }
    m_ctx.uc_link          = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext( &m_ctx, &MainFunc, 0 );
    m_state = INIT;
}

// 切换到当前协程执行
void Fiber::swapIn() {
    SYLAR_ASSERT( m_state != EXEC );

    SetThis( this );
    m_state = EXEC;

    if ( swapcontext( &Scheduler::GetMainFiber()->m_ctx, &m_ctx ) ) {
        SYLAR_ASSERT2( false, "swapincontext" );
    }
}
// 切换到后台执行
void Fiber::swapOut() {
    SetThis( Scheduler::GetMainFiber() );
    if ( swapcontext( &m_ctx, &Scheduler::GetMainFiber()->m_ctx ) ) {
        SYLAR_ASSERT2( false, "swapoutcontext" );
    }
}

void Fiber::call() {
    SYLAR_ASSERT( m_state != EXEC );
    SetThis( this );
    m_state = EXEC;
    if ( swapcontext( &t_threadFiber->m_ctx, &m_ctx ) ) {
        SYLAR_ASSERT2( false, "swapincontext" );
    }
}
void Fiber::back() {
    SetThis( t_threadFiber.get() );
    if ( swapcontext( &m_ctx, &t_threadFiber->m_ctx ) ) {
        SYLAR_ASSERT2( false, "swapoutcontext" );
    }
}
// 设置当前协程
void Fiber::SetThis( Fiber *f ) {
    t_fiber = f;
}
// 返回当前协程
// 当前协程存在，返回当前协程， 若不存在，新建一个主协程
Fiber::ptr Fiber::GetThis() {
    if ( t_fiber ) {
        // 获取对象this的智能指针
        return t_fiber->shared_from_this();
    }
    // 当 t_fiber为nullptr时， 说明该线程不存在 主协程
    t_threadFiber.reset( new Fiber() ); // 初始化一个主协程

    return t_fiber->shared_from_this(); // nullptr
}

// 协程切换到后台，并且设置为Ready状态
void Fiber::YieldToReady() {
    ptr cur      = GetThis();
    cur->m_state = READY;
    cur->swapOut();
}
// 协程切换到后台，并且设置为Hold状态
void Fiber::YieldToHold() {
    ptr cur      = GetThis();
    cur->m_state = HOLD;
    cur->swapOut();
}
// 总协程数量
uint64_t Fiber::TotalFibers() {
    return s_fiber_count;
}

void Fiber::MainFunc() {
    ptr cur = GetThis();
    try {
        cur->m_cb();
        cur->m_cb    = nullptr;
        cur->m_state = TERM;
    } catch ( std::exception &ex ) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR( g_logger ) << "Fiber Except: " << ex.what()
                                    << " fiber_id=" << cur->getId() << std::endl
                                    << sylar::BacktraceToString();
    } catch ( ... ) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR( g_logger ) << "Fiber Except"
                                    << " fiber_id=" << cur->getId() << std::endl
                                    << sylar::BacktraceToString();
    }

    // 执行结束，切回主线程
    auto raw_ptr = cur.get();
    // 释放智能指针所有权
    cur.reset();

    raw_ptr->swapOut();

    SYLAR_ASSERT2( false, "never reach fiber_id=" +
                              std::to_string( raw_ptr->getId() ) );
}

void Fiber::CallerMainFunc() {
    Fiber::ptr cur = GetThis();
    try {
        cur->m_cb();
        cur->m_cb    = nullptr;
        cur->m_state = TERM;

    } catch ( std::exception &ex ) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR( g_logger ) << "Fiber Except: " << ex.what()
                                    << " fiber_id=" << cur->getId() << std::endl
                                    << sylar::BacktraceToString();
    } catch ( ... ) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR( g_logger ) << "Fiber Except"
                                    << " fiber_id=" << cur->getId() << std::endl
                                    << sylar::BacktraceToString();
    }
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();

    SYLAR_ASSERT2( false, "never reach fiber_id=" +
                              std::to_string( raw_ptr->getId() ) );
}
} // namespace sylar