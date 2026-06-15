#pragma once

#include <stddef.h>

#ifndef __containerof
#define __containerof(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})
#endif
