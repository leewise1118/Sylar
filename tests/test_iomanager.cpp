#include "../sylar/iomanager.h"
#include "../sylar/sylar.h"
#include <arpa/inet.h>
#include <boost/range/iterator_range_core.hpp>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sock = 0;

void test_fiber() {
    SYLAR_LOG_INFO( g_logger ) << "test_fiber sock=" << sock;

    sock = socket( AF_INET, SOCK_STREAM, 0 );
    fcntl( sock, F_SETFL, O_NONBLOCK );

    sockaddr_in addr;
    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_port   = htons( 80 );
    inet_pton( AF_INET, "14.119.104.189", &addr.sin_addr.s_addr );
    // inet_pton( AF_INET, "115.239.210.27", &addr.sin_addr.s_addr );
    if ( !connect( sock, (const sockaddr *) &addr, sizeof( addr ) ) ) {
    } else if ( errno == EINPROGRESS ) {
        SYLAR_LOG_INFO( g_logger )
            << "add event errno=" << errno << " " << strerror( errno );
        sylar::IOManager::GetThis()->addEvent(
            sock, sylar::IOManager::READ,
            []() { SYLAR_LOG_INFO( g_logger ) << "read connected"; } );

        sylar::IOManager::GetThis()->addEvent(
            sock, sylar::IOManager::WRITE, []() {
                SYLAR_LOG_INFO( g_logger ) << "write connected";
                sylar::IOManager::GetThis()->cancelEvent(
                    sock, sylar::IOManager::READ );
                close( sock );
            } );

    } else {
        SYLAR_LOG_INFO( g_logger )
            << "else " << errno << " " << strerror( errno );
    }
}
void test1() {
    SYLAR_LOG_INFO( g_logger )
        << "EPOLLIN=" << EPOLLIN << " EPOLLOUT=" << EPOLLOUT;
    sylar::IOManager iom;
    iom.schedule( &test_fiber );
}
void test_timer() {
    sylar::IOManager iom( 2 );
    iom.addTimer( 500, []() { SYLAR_LOG_INFO( g_logger ) << "hello timer"; } );
}

int main() {
    test_timer();
    return 0;
}