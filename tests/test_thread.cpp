#include "../sylar/sylar.h"
#include <iostream>
#include <vector>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

// sylar::RWMutex s_mutex;
sylar::Mutex s_mutex;
int          count = 0;

void fun1() {
    SYLAR_LOG_INFO( g_logger )
        << "name: " << sylar::Thread::GetName()
        << " this.name: " << sylar::Thread::GetThis()->getName()
        << " id: " << sylar::GetThreadId()
        << " this.id: " << sylar::Thread::GetThis()->getId();

    for ( int i = 0; i < 1000000; i++ ) {
        sylar::Mutex::Lock lock( s_mutex );
        count++;
    }
}
void fun2() {
    while ( true ) {
        SYLAR_LOG_INFO( g_logger ) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}
void fun3() {
    while ( true ) {
        SYLAR_LOG_INFO( g_logger ) << "=================================";
    }
}
int main( int argc, char **argv ) {
    SYLAR_LOG_INFO( g_logger ) << "thread test begin";

    YAML::Node root = YAML::LoadFile( "/home/lee/C++/SYLAR/log2.yaml" );
    sylar::Config::LoadFromYaml( root );

    std::vector<sylar::Thread::ptr> thrs;
    for ( int i = 0; i < 2; ++i ) {
        sylar::Thread::ptr thr(
            new sylar::Thread( &fun2, "name_" + std::to_string( i * 2 ) ) );
        sylar::Thread::ptr thr2(
            new sylar::Thread( &fun3, "name_" + std::to_string( i * 2 + 1 ) ) );
        thrs.push_back( thr );
        thrs.push_back( thr2 );
    }
    for ( int i = 0; i < 2; ++i ) {
        thrs[ i ]->join();
    }
    SYLAR_LOG_INFO( g_logger ) << "thread test end";
    SYLAR_LOG_INFO( g_logger ) << "count= " << count;

    return 0;
}