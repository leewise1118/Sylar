#pragma once
#include "fiber.h"
#include "thread.h"
#include <list>
#include <memory>
#include <vector>

/*
    协程调度模块 scheduler
              1-N        1-M
    scheduler --> thread ---> fiber

    1. 线程池, 分配一组线程
    2. 协程调度器， 将协程指定到相应的线程上执行

 */

namespace sylar {

class Scheduler {
  private:
    /**
     * @brief 任务类
     * 等待分配线程执行的任务，可以是Fiber 也可以是 std::function
     **/
    struct Task {
        using TaskFunc = std::function<void()>;

        Fiber::ptr fiber;
        TaskFunc   cb;
        int        threadId; // 绑定的线程id
        Task( Fiber::ptr f, int thr )
            : fiber( f )
            , threadId( thr ) {
        }
        Task( Fiber::ptr *f, int thr )
            : threadId( thr ) {
            fiber.swap( *f );
        }
        Task( TaskFunc f, int thr )
            : cb( f )
            , threadId( thr ) {
        }
        Task( TaskFunc *f, int thr )
            : threadId( thr ) {
            cb.swap( *f );
        }
        Task()
            : threadId( -1 ) {
        }
        void reset() {
            fiber    = nullptr;
            cb       = nullptr;
            threadId = -1;
        }
    };

  public:
    using ptr       = std::shared_ptr<Scheduler>;
    using MutexType = Mutex;

    /**
     * @brief 构造函数
     * @param thread_size 线程池线程数量
     * @param use_caller 是否将 Scheduler 实例化所在的协程设置为主协程
     * @param name 调度器的名字
     **/
    Scheduler( size_t threads = 1, bool use_caller = true,
               const std::string &name = "" );
    virtual ~Scheduler();

    void start();
    void stop();

    const std::string &getName() const {
        return m_name;
    }
    static Scheduler *GetThis();
    static Fiber     *GetMainFiber();

    /**
     * @brief 添加任务 thread-safe
     * @param FiberOrCb 模板类是函数或者协程
     * @param thread 线程id
     **/
    template <class FiberOrCb> void schedule( FiberOrCb fc, int thread = -1 ) {
        bool need_tickle = false;
        {
            MutexType::Lock lock( m_mutex );
            need_tickle = scheduleNoLock( fc, thread );
        }
        // 如果是空闲状态下的第一个新任务，则工作
        if ( need_tickle ) {
            tickle();
        }
    }
    /**
     * @brief  添加多个任务 thread-safe
     * @tparam InputIterator
     * @param begin 单向迭代器
     * @param end 单向迭代器
     */
    template <class InputIterator>
    void schedule( InputIterator begin, InputIterator end ) {
        bool need_tickle = false;
        {
            MutexType::Lock lock( m_mutex );
            while ( begin != end ) {
                need_tickle = scheduleNoLock( &*begin, -1 ) || need_tickle;
                ++begin;
            }
        }
        if ( need_tickle ) {
            tickle();
        }
    }

  private:
    /**
     * @brief 添加任务 no-thread-safe
     * @param FiberOrCb 模板类是函数或者协程
     * @param thread 线程id
     * @return 是否是空闲状态下的第一个新任务
     **/
    template <class FiberOrCb> bool scheduleNoLock( FiberOrCb fc, int thread ) {
        bool need_tickle = m_fibers.empty();
        Task task( fc, thread );
        if ( task.fiber || task.cb ) {
            m_fibers.push_back( task );
        }
        return need_tickle;
    }

  protected:
    virtual void tickle(); // 通知协程调度器有新任务
    void         run();    // 协程调度函数
    virtual bool stopping();
    virtual void idle(); // 协程无任务可调度时执行idle协程

    void setThis();
    bool hasIdleThread() {
        return m_idleThreadCount > 0;
    }

  private:
    MutexType                      m_mutex;
    std::vector<Thread::ptr>       m_threads;
    std::list<Task>                m_fibers;
    std::map<int, std::list<Task>> m_thrFibers;
    Fiber::ptr                     m_rootFiber;
    std::string                    m_name;

  protected:
    std::vector<int>    m_threadIds;
    size_t              m_threadCount = 0;
    std::atomic<size_t> m_activeThreadCount{ 0 };
    std::atomic<size_t> m_idleThreadCount{ 0 };
    bool                m_stopping   = true;  // 执行停止状态
    bool                m_autoStop   = false; // 是否自动停止
    int                 m_rootThread = 0;
};

} // namespace sylar