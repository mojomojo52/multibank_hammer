#pragma once

#include <stdint.h>

#include "types.h"

int  mem_check(SessionConfig * cfg, MemoryBuffer * memory);
int mem_check_1GB(SessionConfig *cfg, MemoryBuffer *memory);