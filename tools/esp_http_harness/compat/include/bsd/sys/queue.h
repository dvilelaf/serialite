#pragma once

#include "/usr/include/x86_64-linux-gnu/sys/queue.h"

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = SLIST_FIRST((head)); \
         (var) != NULL && ((tvar) = SLIST_NEXT((var), field), 1); \
         (var) = (tvar))
#endif

#ifndef STAILQ_FOREACH_SAFE
#define STAILQ_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = STAILQ_FIRST((head)); \
         (var) != NULL && ((tvar) = STAILQ_NEXT((var), field), 1); \
         (var) = (tvar))
#endif
