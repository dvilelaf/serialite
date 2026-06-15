#pragma once

#include <stddef.h>

static inline size_t strlcpy(char *dst, const char *src, size_t dst_size)
{
    const size_t src_len = __builtin_strlen(src);
    if (dst_size > 0U) {
        size_t copy_len = src_len;
        if (copy_len >= dst_size) {
            copy_len = dst_size - 1U;
        }
        __builtin_memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

static inline size_t bsd_compat_strnlen(const char *s, size_t max_len)
{
    size_t len = 0;
    while (len < max_len && s[len] != '\0') {
        ++len;
    }
    return len;
}

static inline size_t strlcat(char *dst, const char *src, size_t dst_size)
{
    const size_t dst_len = bsd_compat_strnlen(dst, dst_size);
    const size_t src_len = __builtin_strlen(src);
    if (dst_len == dst_size) {
        return dst_size + src_len;
    }
    (void)strlcpy(dst + dst_len, src, dst_size - dst_len);
    return dst_len + src_len;
}
