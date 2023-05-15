#pragma once

#include "log.h"
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <sys/time.h>
#include <sys/types.h>
#include <vector>

namespace sylar {

pid_t GetThreadId();

uint64_t GetFiberId();

void Backtrace( std::vector<std::string> &bt, int size, int skip = 1 );

std::string BacktraceToString( int size = 3, int skip = 2,
                               const std::string &prefix = "" );

// 时间
uint64_t GetCurrentMS();
uint64_t GetCurrentUS();

} // namespace sylar
