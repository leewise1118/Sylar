#pragma once
#include "singleton.h"
#include "thread.h"
#include "util.h"
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <stdarg.h>
#include <stdint.h>
#include <string>
#include <vector>

#define SYLAR_LOG_LEVEL( logger, level )                                       \
    if ( level >= logger->getLevel() )                                         \
    sylar::LogEventWarp(                                                       \
        sylar::LogEvent::ptr( new sylar::LogEvent(                             \
            logger, level, __FILE__, __LINE__, 0, sylar::GetThreadId(),        \
            sylar::GetFiberId(), time( 0 ), sylar::Thread::GetName() ) ) )     \
        .getSS()

#define SYLAR_LOG_DEBUG( logger )                                              \
    SYLAR_LOG_LEVEL( logger, sylar::LogLevel::DEBUG )
#define SYLAR_LOG_INFO( logger )                                               \
    SYLAR_LOG_LEVEL( logger, sylar::LogLevel::INFO )
#define SYLAR_LOG_WARN( logger )                                               \
    SYLAR_LOG_LEVEL( logger, sylar::LogLevel::WARN )
#define SYLAR_LOG_ERROR( logger )                                              \
    SYLAR_LOG_LEVEL( logger, sylar::LogLevel::ERROR )
#define SYLAR_LOG_FATAL( logger )                                              \
    SYLAR_LOG_LEVEL( logger, sylar::LogLevel::FATAL )

/**
 * @brief 使用格式化方式将日志级别level的日志写入到logger
 */
#define SYLAR_LOG_FMT_LEVEL( logger, level, fmt, ... )                         \
    if ( logger->getLevel() <= level )                                         \
    sylar::LogEventWrap(                                                       \
        sylar::LogEvent::ptr( new sylar::LogEvent(                             \
            logger, level, __FILE__, __LINE__, 0, sylar::GetThreadId(),        \
            sylar::GetFiberId(), time( 0 ), sylar::Thread::GetName() ) ) )     \
        .getEvent()                                                            \
        ->format( fmt, __VA_ARGS__ )
/**
 * @brief 使用格式化方式将日志级别debug的日志写入到logger
 */
#define SYLAR_LOG_FMT_DEBUG( logger, fmt, ... )                                \
    SYLAR_LOG_FMT_LEVEL( logger, sylar::LogLevel::DEBUG, fmt, __VA_ARGS__ )

/**
 * @brief 使用格式化方式将日志级别info的日志写入到logger
 */
#define SYLAR_LOG_FMT_INFO( logger, fmt, ... )                                 \
    SYLAR_LOG_FMT_LEVEL( logger, sylar::LogLevel::INFO, fmt, __VA_ARGS__ )

/**
 * @brief 使用格式化方式将日志级别warn的日志写入到logger
 */
#define SYLAR_LOG_FMT_WARN( logger, fmt, ... )                                 \
    SYLAR_LOG_FMT_LEVEL( logger, sylar::LogLevel::WARN, fmt, __VA_ARGS__ )

/**
 * @brief 使用格式化方式将日志级别error的日志写入到logger
 */
#define SYLAR_LOG_FMT_ERROR( logger, fmt, ... )                                \
    SYLAR_LOG_FMT_LEVEL( logger, sylar::LogLevel::ERROR, fmt, __VA_ARGS__ )

/**
 * @brief 使用格式化方式将日志级别fatal的日志写入到logger
 */
#define SYLAR_LOG_FMT_FATAL( logger, fmt, ... )                                \
    SYLAR_LOG_FMT_LEVEL( logger, sylar::LogLevel::FATAL, fmt, __VA_ARGS__ )

/**
 * @brief 获取主日志器
 */
#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

#define SYLAR_LOG_NAME( name )                                                 \
    sylar::LoggerMgr::GetInstance()->getLogger( name )

namespace sylar {

class Logger;
class LoggerManager;

// 日志级别
class LogLevel {
  public:
    enum Level {
        UNKNOW = 0,
        DEBUG  = 1,
        INFO   = 2,
        WARN   = 3,
        ERROR  = 4,
        FATAL  = 5
    };
    static const char     *ToString( LogLevel::Level level );
    static LogLevel::Level FromString( const std::string &str );
};
class LogEvent {
  public:
    typedef std::shared_ptr<LogEvent> ptr;
    LogEvent( std::shared_ptr<Logger> logger, LogLevel::Level level,
              const char *file, int32_t line, uint32_t elapes,
              uint32_t thread_id, uint32_t fiber_id, uint64_t time,
              const std::string &thread_name );

    const char *getFile() const {
        return m_file;
    }
    int32_t getLine() const {
        return m_line;
    }
    uint32_t getElapse() const {
        return m_elapse;
    }
    uint32_t getTheadId() const {
        return m_threadId;
    }
    uint32_t getFiberId() const {
        return m_fiberId;
    }
    uint64_t getTime() const {
        return m_time;
    }
    std::string getContent() const {
        return m_ss.str();
    }
    std::stringstream &getSS() {
        return m_ss;
    }
    std::string getThreadName() const {
        return m_threadName;
    }
    std::shared_ptr<Logger> getLogger() const {
        return m_logger;
    }
    LogLevel::Level getLevel() const {
        return m_level;
    }

    void format( const char *fmt );

  private:
    const char       *m_file     = nullptr; // 文件名
    int32_t           m_line     = 0;       // 行号
    uint32_t          m_elapse   = 0; // 程序启动开始到现在的毫秒数
    uint32_t          m_threadId = 0; // 线程id
    uint32_t          m_fiberId  = 0; // 协程id
    uint64_t          m_time     = 0; // 时间戳
    std::stringstream m_ss;
    std::string       m_threadName;

    std::shared_ptr<Logger> m_logger;
    LogLevel::Level         m_level;
};

class LogEventWarp {
  public:
    LogEventWarp( LogEvent::ptr e );
    ~LogEventWarp();

    std::stringstream &getSS();

  private:
    LogEvent::ptr m_event;
};
// 日志格式器
class LogFormatter {
  public:
    typedef std::shared_ptr<LogFormatter> ptr;
    LogFormatter(
        const std::string &pattern =
            "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n" );
    std::string format( std::shared_ptr<Logger> logger, LogLevel::Level level,
                        LogEvent::ptr event );
    bool        isError() const {
        return m_error;
    }
    const std::string getPattern() const {
        return m_pattern;
    }

  public:
    class FormatItem {
      public:
        typedef std::shared_ptr<FormatItem> ptr;
        virtual ~FormatItem() {
        }
        virtual void format( std::ostream &os, std::shared_ptr<Logger> logger,
                             LogLevel::Level level, LogEvent::ptr event ) = 0;
    };
    void init();

  private:
    // 日志格式模板
    std::string m_pattern;
    // 日志解析后的格式
    std::vector<FormatItem::ptr> m_items;
    // 是否有错误
    bool m_error = false;
};

// 日志输出地
class LogAppender {
    friend class Logger;

  public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef SpinLock                     MutexType;
    virtual ~LogAppender(){};

    virtual void log( std::shared_ptr<Logger> logger, LogLevel::Level level,
                      LogEvent::ptr event ) = 0;
    void         setFormatter( LogFormatter::ptr val );
    LogFormatter::ptr getFormatter();

    void setLevel( LogLevel::Level level ) {
        m_level = level;
    }
    virtual std::string toYamlString() = 0;

  protected:
    LogLevel::Level   m_level = LogLevel::DEBUG;
    LogFormatter::ptr m_formatter;
    bool              m_hasFormatter = false;
    MutexType         m_mutex;
};

// 日志器
class Logger : public std::enable_shared_from_this<Logger> {
    friend class LoggerManager;

  public:
    typedef std::shared_ptr<Logger> ptr;
    typedef SpinLock                MutexType;
    Logger( const std::string &name = "root" );

    void log( LogLevel::Level level, LogEvent::ptr &event );
    void debug( LogEvent::ptr event );
    void info( LogEvent::ptr event );
    void warn( LogEvent::ptr event );
    void error( LogEvent::ptr event );
    void fatal( LogEvent::ptr event );

    void            addAppender( LogAppender::ptr appender );
    void            delAppender( LogAppender::ptr appender );
    void            clearAppenders();
    LogLevel::Level getLevel() const {
        return m_level;
    }
    void setLevel( LogLevel::Level level ) {
        m_level = level;
    }
    void              setFormatter( LogFormatter::ptr val );
    void              setFormatter( const std::string &val );
    LogFormatter::ptr getFormatter() const {
        return m_formatter;
    }

    const std::string &getName() const {
        return m_name;
    }

    std::string toYamlString();

  private:
    std::string                 m_name;      // 日志名称
    LogLevel::Level             m_level;     // 日志级别
    std::list<LogAppender::ptr> m_appenders; // Appender集合
    LogFormatter::ptr           m_formatter;
    Logger::ptr                 m_root;
    MutexType                   m_mutex;
};

// 输出到控制台的Appender
class StdoutLogAppender : public LogAppender {
  public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    void        log( Logger::ptr logger, LogLevel::Level level,
                     LogEvent::ptr event ) override;
    std::string toYamlString() override;
};

// 定义输出到文件的Appender
class FileLogAppender : public LogAppender {
  public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender( const std::string &filename );
    void log( Logger::ptr logger, LogLevel::Level level,
              LogEvent::ptr event ) override;
    // 重新打开文件，文件打开成功返回true
    bool reopen();

    std::string toYamlString() override;

  private:
    std::string   m_filename;
    std::ofstream m_filestream;
    uint64_t      m_lastTime = 0;
};

// 日志管理类
class LoggerManager {
  public:
    typedef SpinLock MutexType;
    LoggerManager();

    Logger::ptr getLogger( const std::string &name );
    void        init();
    Logger::ptr getRoot() const {
        return m_root;
    }

    std::string toYamlString();

  private:
    Logger::ptr                        m_root;
    std::map<std::string, Logger::ptr> m_loggers;
    MutexType                          m_mutex;
};

typedef sylar::Singleton<LoggerManager> LoggerMgr;
} // namespace sylar
