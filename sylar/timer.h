#pragma once
#include "thread.h"
#include "util.h"
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace sylar {
class TimerManager;
class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;

  public:
    using ptr  = std::shared_ptr<Timer>;
    using Func = std::function<void()>;

    bool cancel();
    bool refresh();
    bool reset( uint64_t ms, bool from_now );

  private:
    Timer( uint64_t ms, Func cb, bool recurring, TimerManager *manager );
    Timer( uint64_t next );

  private:
    bool          m_recurring = false; // 是否循环定时器
    uint64_t      m_ms        = 0;     // 执行周期
    uint64_t      m_next      = 0;     // 精确的执行时间
    Func          m_cb;
    TimerManager *m_manager = nullptr;

  private:
    struct Comparator {
        bool operator()( const ptr &lhs, const ptr &rhs ) const;
    };
};
class TimerManager {
    friend class Timer;

  public:
    using Func      = std::function<void()>;
    using MutexType = RWMutex;
    TimerManager();
    virtual ~TimerManager();

    Timer::ptr addTimer( uint64_t ms, Func cb, bool recurring = false );
    void       addTimer( Timer::ptr val, MutexType::WriteLock &lock );
    Timer::ptr addConditionTimer( uint64_t ms, Func cb,
                                  std::weak_ptr<void> weak_cond,
                                  bool                recurring = false );
    uint64_t   getNextTimer();
    void       listExpiredCb( std::vector<Func> &cbs );
    bool       hasTimer();

  protected:
    virtual void onTimeInsertedAtFront() = 0; // 插入到最前端，即时间最短

  private:
    bool detectClockRollover( uint64_t now_ms );

  private:
    MutexType                               m_mutex;
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    bool                                    m_tickled = false;
    uint64_t                                m_previouseTime;
};

} // namespace sylar