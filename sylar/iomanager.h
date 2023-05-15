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
        NONE  = 0x0,
        READ  = 0x1, // EPOLLIN
        WRITE = 0x4, // EPOLLOUR
    };

  private:
    struct FdContext {
        using MutexType = Mutex;
        struct EventContext {
            Scheduler            *scheduler; // 事件执行的scheduler
            Fiber::ptr            fiber;     // 事件协程
            std::function<void()> cb;        // 事件的回调函数
        };
        EventContext &getContext( Event event );
        void          resetContext( EventContext &ctx );
        void          triggerEvent( Event event );

        int          fd = 0;        // 事件关联的句柄
        EventContext read;          // 读事件
        EventContext write;         // 写事件
        Event        events = NONE; // 已经注册的事件
        MutexType    mutex;
    };

  public:
    IOManager( size_t thread = 1, bool use_caller = true,
               const std::string &name = "" );
    ~IOManager();
    // 0 success, -1 error
    int               addEvent( int fd, Event event, Func cb = nullptr );
    bool              delEvent( int fd, Event event );
    bool              cancelEvent( int fd, Event event );
    bool              cancelAll( int fd );
    static IOManager *GetThis();

  protected:
    void tickle() override;
    bool stopping() override;
    bool stopping( uint64_t &timeout );
    void idle() override;
    void onTimeInsertedAtFront() override;

    void contextResize( size_t size );

  private:
    int                      m_epfd = 0;
    int                      m_tickleFds[ 2 ];
    std::atomic<size_t>      m_pendingEventCount = { 0 };
    MutexType                m_mutex;
    std::vector<FdContext *> m_fdContexts;
};

} // namespace sylar