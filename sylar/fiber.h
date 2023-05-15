#pragma once
#include "config.h"
#include "macro.h"
#include "thread.h"
#include <memory>
#include <system_error>
#include <ucontext.h>

/*
typedef struct ucontext_t {
    unsigned long int                    __ctx( uc_flags );
    struct ucontext_t                   *uc_link; //指向上下文，即其他协程
    stack_t                              uc_stack;//该上下文使用的栈
    mcontext_t                           uc_mcontext;//上下文
    sigset_t                             uc_sigmask;//上下文中的阻塞信息
    struct _libc_fpstate                 __fpregs_mem;
    __extension__ unsigned long long int __ssp[ 4 ];
} ucontext_t;
*/

namespace sylar {

// 继承enable_shared_from_this类，获取当前类的智能指针，但是不可以在栈上去创建对象了，因为它一定要是智能指针的成员
class Fiber : public std::enable_shared_from_this<Fiber> {
    friend class Scheduler;

  public:
    using ptr       = std::shared_ptr<Fiber>;
    using FiberFunc = std::function<void()>;

    enum State {
        INIT,  // 初始化状态
        HOLD,  // 暂停状态
        EXEC,  // 执行状态
        TERM,  // 结束状态
        READY, // 就绪状态
        EXCEPT // 异常状态
    };

  public:
    Fiber( FiberFunc cb, size_t stacksize = 0, bool use_caller = false );
    ~Fiber();

    // 重置协程函数，并重置状态
    // INIT, TERM
    void reset( FiberFunc cb );
    // 切换到当前协程执行
    void swapIn();
    // 切换到后台执行
    void swapOut();

    void call();
    void back();

    uint64_t getId() const {
        return m_id;
    }
    State getState() const {
        return m_state;
    }
    void setState( State state ) {
        m_state = state;
    }

  public:
    // 设置当前协程
    static void SetThis( Fiber *f );
    // 返回当前协程
    static Fiber::ptr GetThis();
    // 协程切换到后台，并且设置为Ready状态
    static void YieldToReady();
    // 协程切换到后台，并且设置为Hold状态
    static void YieldToHold();
    // 总协程数量
    static uint64_t TotalFibers();
    static uint64_t GetFiberId();

    static void MainFunc();
    static void CallerMainFunc();

  private:
    uint64_t   m_id        = 0;
    uint32_t   m_stacksize = 0;
    State      m_state     = INIT;
    ucontext_t m_ctx;
    void      *m_stack = nullptr;
    FiberFunc  m_cb;

  private:
    Fiber();
};

} // namespace sylar