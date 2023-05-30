#pragma once
#include "fiber.h"
#include <functional>
#include <iostream>
#include <unistd.h>

namespace sylar {
bool is_hook_enable();
void set_hook_enable( bool flag );
} // namespace sylar

extern "C" {

typedef unsigned int ( *sleep_fun )( unsigned int seconds );
extern sleep_fun sleep_f;

typedef int ( *usleep_fun )( useconds_t usec );
extern usleep_fun usleep_f;
}