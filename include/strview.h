#ifndef __STRVIEW_H__
#define __STRVIEW_H__

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    const char *ptr;
    size_t len;
} StrView;

#define SV_LIT(s) ((StrView) { (s), sizeof(s) - 1 })
#define SV(s) \
    _Generic((s), \
        const char *: sv_from_cstr, \
        char *:       sv_from_cstr, \
        StrView:      sv_identity \
    )(s)

static inline StrView sv_identity(StrView sv) {
    return sv;
}

static inline StrView sv_from_cstr(const char *s) {
    return (StrView) { s, strlen(s) };
}

static inline StrView sv_from_buf(const char *s, size_t n) {
    return (StrView) { s, n };
}

static inline int __sv_eq_impl(StrView a, StrView b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr,  a.len) == 0;
}


static inline int __sv_ieq_impl(StrView a, StrView b) {
    if (a.len != b.len) return 0;
    for (size_t i = 0; i < a.len; ++i) {
        if (tolower(a.ptr[i]) != tolower(b.ptr[i])) {
            return 0;
        }
    }

    return 1;
}

static inline int __sv_split_once_strview_impl(StrView sv, StrView sep, StrView *out_prefix, StrView *out_remainder) {
    if (sep.len == 0) {
        *out_prefix = sv;
        *out_remainder = sv_from_buf(NULL, 0);
        return 0;
    }

    for (size_t i = 0; i + sep.len <= sv.len; ++i) {
        if (memcmp(sv.ptr + i, sep.ptr, sep.len) == 0) {
            *out_prefix = sv_from_buf(sv.ptr, i);
            *out_remainder = sv_from_buf(sv.ptr + i + sep.len, sv.len - i - sep.len);
            return 1;
        }
    }

    *out_prefix = sv;
    *out_remainder = sv_from_buf(NULL, 0);
    return 0;
}

static inline StrView __sv_rstrip_char_impl(StrView sv, char ch) {
    size_t new_len = sv.len;
    while (new_len > 0 && sv.ptr[new_len - 1] == ch) {
        new_len -= 1;
    }

    return (StrView) { .ptr = sv.ptr, .len = new_len };
}

static inline int __sv_startswith_impl(StrView s, StrView prefix) {
    return s.len >= prefix.len && memcmp(s.ptr, prefix.ptr, prefix.len) == 0;
}

static inline int __sv_endswith_impl(StrView s, StrView suffix) {
    return s.len >= suffix.len &&
        memcmp(s.ptr + (s.len - suffix.len), suffix.ptr, suffix.len) == 0;
}


static inline int sv_atoi(StrView sv) {
    int result = 0;
    int sign = 1;

    if (sv.len == 0) return 0;

    const char *p = sv.ptr;
    size_t n = sv.len;

    while (n > 0 && isspace((unsigned char)*p)) {
        p++; n--;
    }

    if (n > 0 && (*p == '+' || *p == '-')) {
        if (*p == '-') sign = -1;
        p++; n--;
    }

    while (n > 0 && isdigit((unsigned char)*p)) {
        result = result * 10 + (*p - '0');
        p++; n--;
    }

    return sign * result;
}

#define sv_eq(a, b) __sv_eq_impl(SV(a), SV(b))
#define sv_ieq(a, b) __sv_ieq_impl(SV(a), SV(b))
#define sv_split_once_strview(sv, sep, out_prefix, out_remainder) \
    __sv_split_once_strview_impl(SV(sv), SV(sep), out_prefix, out_remainder)
#define sv_rstrip_char(sv, ch) __sv_rstrip_char_impl(SV(sv), ch)
#define sv_startswith(sv, prefix) __sv_startswith_impl(SV(sv), SV(prefix))
#define sv_endswith(sv, prefix) __sv_endswith_impl(SV(sv), SV(prefix))

#define SV_FMT "%.*s"
#define SV_ARG(s) (int)(s).len, (s).ptr

static inline char *sv_to_cstr(StrView s, char *buf, size_t bufsize) {
    size_t n = (s.len < bufsize - 1) ? s.len : bufsize - 1;

    memcpy(buf, s.ptr, n);
    buf[n] = '\0';
    return buf;
}

#endif // __STRVIEW_H__
