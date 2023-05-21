#pragma once

#include "util.h"
#include <assert.h>
#include <string.h>

#define SYLAR_ASSERT( x )                                                      \
    if ( !( x ) ) {                                                            \
        SYLAR_LOG_ERROR( SYLAR_LOG_ROOT() )                                    \
            << "ASSERTION: " #x << "\nbacktrace:\n"                            \
            << sylar::BacktraceToString( 100, 2, "   " );                      \
        assert( x );                                                           \
    }

#define SYLAR_ASSERT2( x, w )                                                  \
    if ( !( x ) ) {                                                            \
        SYLAR_LOG_ERROR( SYLAR_LOG_ROOT() )                                    \
            << "ASSERTION: " #x << "\n"                                        \
            << w << "\nbacktrace:\n"                                           \
            << sylar::BacktraceToString( 100, 2, "   " );                      \
        assert( x );                                                           \
    }

// LIKCLY宏的封装，告诉编译器优化，条件大概率成立
#define SYLAR_LIKELY( x ) __builtin_expect( !!( x ), 1 )
// LIKCLY宏的封装，告诉编译器优化，条件大概率不成立
#define SYLAR_UNLIKELY( x ) __builtin_expect( !!( x ), 0 )