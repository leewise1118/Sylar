#include "../sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void add_fiber() {
    SYLAR_LOG_INFO( g_logger ) << "add fiber";
}
void test_fiber() {
    static int s_n = 0;
    s_n++;
    SYLAR_LOG_INFO( g_logger ) << "test in fiber" << s_n;
    sleep( 1 );
}
int main() {
    SYLAR_LOG_INFO( g_logger ) << "main";
    sylar::Scheduler sc( 1, true, "test" );
    sc.start();
    SYLAR_LOG_INFO( g_logger ) << "schedule";
    for ( int i = 0; i < 3; i++ ) {
        sc.schedule( &test_fiber );
    }
    sc.stop();
    SYLAR_LOG_INFO( g_logger ) << "over";
    return 0;
}