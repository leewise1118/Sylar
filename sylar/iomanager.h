#pragma once
#include "scheduler.h"
#include "timer.h"

namespace sylar {

class IOManager : public Scheduler, public TimerManager {
  public:
    using ptr       = std::shared_ptr<IOManager>;
    using MutexType = RWMutex;
    using Func      = std::function<void()>;

    enum Event {
        NONE  = 0x0, // 无事件
        READ  = 0x1, // 读事件EPOLLIN
        WRITE = 0x4, // 写事件EPOLLOUT
    };

  private:
    /**
     * @brief Socket事件上下文类
     */
    struct FdContext {
        using MutexType = Mutex;

        /**
         * @brief 事件上下文类
         */
        struct EventContext {
            Scheduler            *scheduler; // 事件执行的scheduler
            Fiber::ptr            fiber;     // 事件协程
            std::function<void()> cb;        // 事件的回调函数
        };

        /**
         * @brief 获取事件上下文类
         * @param event 事件类型
         * @return 返回对应的时间上下文
         */
        EventContext &getContext( Event event );

        /**
         * @brief 重置事件上下文
         * @param ctx:待重置的事件上下文类
         */
        void resetContext( EventContext &ctx );

        /**
         * @brief 触发事件
         * @param event:事件类型
         */
        void triggerEvent( Event event );

        int          fd = 0;        // 事件关联的句柄
        EventContext read;          // 读事件上下文
        EventContext write;         // 写事件上下文
        Event        events = NONE; // 当前的事件
        MutexType    mutex;
    };

  public:
    /**
     * @brief Construct a new IOManager object
     *
     * @param thread: 线程数量
     * @param use_caller: 是否将调用线程包含进去
     * @param name 调度器的名称
     */
    IOManager( size_t thread = 1, bool use_caller = true,
               const std::string &name = "" );
    ~IOManager();

    /**
     * @brief 添加事件
     * @param fd socket句柄
     * @param event 事件类型
     * @param cb 事件回调函数
     * @return int 添加成功返回0，失败返回-1
     */
    int addEvent( int fd, Event event, Func cb = nullptr );

    bool delEvent( int fd, Event event );
    bool cancelEvent( int fd, Event event );
    bool cancelAll( int fd );

    /**
     * @brief 返回当前的IOManager
     */
    static IOManager *GetThis();

  protected:
    void tickle() override;
    bool stopping() override;
    bool stopping( uint64_t &timeout );
    void idle() override;
    void onTimeInsertedAtFront() override;

    /**
     * @brief 重置socket句柄上下文的容器大小
     * @param size 容器大小
     */
    void contextResize( size_t size );

  private:
    int m_epfd = 0;                                  // epoll文件句柄
    int m_tickleFds[ 2 ];                            // pipe文件句柄
    std::atomic<size_t> m_pendingEventCount = { 0 }; // 当前等待执行的事件数量
    MutexType                m_mutex;
    std::vector<FdContext *> m_fdContexts; // socket事件上下文容器
};

} // namespace sylar