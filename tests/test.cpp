#include "../sylar/log.h"
#include <iostream>
#include <memory>
#include <string>

int main() {
    sylar::Logger::ptr logger( new sylar::Logger );
    logger->addAppender(
        sylar::LogAppender::ptr( new sylar::StdoutLogAppender ) );

    std::cout << "hello sylar log" << std::endl;
    SYLAR_LOG_INFO( logger ) << "test macro";
    SYLAR_LOG_DEBUG( logger ) << "test macro DEBUG";

    return 0;
}