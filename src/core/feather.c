#include "feather.h"
#include "strview.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

const char *feather_method_to_str(FeatherMethod method) {
    switch (method) {
        case FEATHER_GET: return "GET";
        case FEATHER_POST: return "POST";
        case FEATHER_PUT: return "PUT";
        case FEATHER_DELETE: return "DELETE";
        case FEATHER_PATCH: return "PATCH";
        case FEATHER_OPTIONS: return "OPTIONS";
        case FEATHER_HEAD: return "HEAD";

        default: return "UNKNOWN";
    }
}

FeatherMethod feather_sv_to_method(StrView str) {
    if (sv_eq(str, "GET")) return FEATHER_GET;
    if (sv_eq(str, "POST")) return FEATHER_POST;
    if (sv_eq(str, "PUT")) return FEATHER_PUT;
    if (sv_eq(str, "DELETE")) return FEATHER_DELETE;
    if (sv_eq(str, "PATCH")) return FEATHER_PATCH;
    if (sv_eq(str, "OPTIONS")) return FEATHER_OPTIONS;
    if (sv_eq(str, "HEAD")) return FEATHER_HEAD;

    return FEATHER_UNKNOWN;
}

void feather_parse_request(FeatherRequest *req, StrView raw) {
    assert(req != NULL);

    StrView line;
    sv_split_once_strview(raw, "\r\n", &line, &raw);

    StrView method, tmp;
    sv_split_once_strview(line, " ", &method, &tmp);

    StrView version;
    sv_split_once_strview(tmp, " ", &req->path, &version);

    req->method = feather_sv_to_method(method);

    if (!version.len) return;
    
    while (raw.len > 0) {
        sv_split_once_strview(raw, "\r\n", &line, &raw);        

        if (line.len == 0) break;

        StrView key, value;
        if (!sv_split_once_strview(line, ":", &key, &value)) continue;

        while (value.len > 0 && value.ptr[0] == ' ') {
            value.ptr += 1;
            value.len -= 1;
        }

        req->headers = realloc(req->headers, (req->header_count + 1) * sizeof(FeatherHeader));
        req->headers[req->header_count].key = key;
        req->headers[req->header_count].value = value;
        req->header_count += 1;
    }

    req->body = raw;
}

static const char *status_reason(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";

        default: return "";
    }
}

size_t feather_dump_response(const FeatherResponse *res, char *buf, size_t buf_size) {
    if (!res || !buf) return 0;

    size_t offset = 0;

    int n = snprintf(buf + offset, buf_size - offset, "HTTP/1.1 %d %s\r\n", res->status, status_reason(res->status));

    if (n < 0 || (size_t) n >= buf_size - offset) return 0;
    offset += (size_t) n;

    for (size_t i = 0; i < res->header_count; ++i) {
        n = snprintf(buf + offset, buf_size - offset, SV_FMT": "SV_FMT"\r\n", SV_ARG(res->headers[i].key), SV_ARG(res->headers[i].value));
        if (n < 0 || (size_t) n >= buf_size - offset) return 0;
        offset += (size_t) n;
    }

    int has_len = 0;

    for (size_t i = 0; i < res->header_count; ++i) {
        if (sv_ieq(res->headers[i].key, "Content-Length")) {
            has_len = 1;
            break;
        }
    }

    if (!has_len && res->body.len > 0) {
        n = snprintf(buf + offset, buf_size - offset, "Content-Length: %zu\r\n", res->body.len);
        if (n < 0 || (size_t) n >= buf_size - offset) return 0;
        offset += (size_t) n;
    }

    if (offset + 2 >= buf_size) return 0;

    buf[offset++] = '\r';
    buf[offset++] = '\n';

    if (res->body.len > 0) {
        if (offset + res->body.len >= buf_size) return 0;
        memcpy(buf + offset, res->body.ptr, res->body.len);
        offset += res->body.len;
    }

    return offset;
}

void feather_response_set_header(FeatherResponse *res, StrView key, StrView value) {
    if (!res || !key.len || !value.len) return;

    for (size_t i = 0; i < res->header_count; ++i) {
        if (sv_ieq(res->headers[i].key, key)) {
            res->headers[i].key = key;
            res->headers[i].value = value;
            return;
        }
    }

    res->headers = realloc(res->headers, (res->header_count + 1) * sizeof(FeatherHeader));
    res->headers[res->header_count].key = key;
    res->headers[res->header_count].value = value;
    res->header_count += 1;
}

void feather_init_app(FeatherApp *app) {
    app->routes = NULL;
    app->route_count = 0;
}

void feather_add_route(FeatherApp *app, FeatherMethod method, const char *path, FeatherHandler handler) {
    app->routes = realloc(app->routes, (app->route_count + 1) * sizeof(FeatherRoute));
    app->routes[app->route_count].method = method;
    app->routes[app->route_count].pattern = sv_from_cstr(path);
    app->routes[app->route_count].handler = handler;

    app->route_count += 1;
}

static int feather_match_route(StrView pattern, FeatherRequest *req) {
    req->param_count = 0;


    if (sv_eq(req->path, "/")) {
        return sv_eq(pattern, "/");
    }

    StrView pattern_rem = sv_rstrip_char(pattern, '/');
    StrView path_rem = sv_rstrip_char(req->path, '/');
    while (pattern_rem.len > 0) {
        StrView p_seg, p_next, r_seg, r_next;

        if (!sv_split_once_strview(pattern_rem, "/", &p_seg, &p_next)) {
            p_seg = pattern_rem;
            p_next = sv_from_buf(NULL, 0);
        }

        if (!sv_split_once_strview(path_rem, "/", &r_seg, &r_next)) {
            r_seg = path_rem;
            r_next = sv_from_buf(NULL, 0);
        }

        pattern_rem = p_next;
        path_rem = r_next;

        if (p_seg.len == 0 && r_seg.len == 0) continue;
        if (p_seg.len == 0 || r_seg.len == 0) return 0;

        if (p_seg.len > 0 && p_seg.ptr[0] == ':') {
            if (req->param_count >= __FEATHER_MAX_PARAMS) return 0;

            StrView key = { .ptr = p_seg.ptr + 1, .len = p_seg.len - 1 };

            req->params[req->param_count].key = key;
            req->params[req->param_count].value = r_seg;
            req->param_count += 1;
        } else {
            if (!sv_eq(p_seg, r_seg)) {
                return 0;
            }
        }
    }

    return path_rem.len == 0;
}

FeatherHandler feather_find_handler(const FeatherApp *app, FeatherRequest *req) {
    FEATHER_LOG_REQUEST(req);
    for (size_t i = 0; i < app->route_count; ++i) {
        if (
            app->routes[i].method == req->method &&
            feather_match_route(app->routes[i].pattern, req)
        ) {
            return app->routes[i].handler;
        }
    }

    return NULL;
}


void feather_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);

    printf("[%s] ", buf);
    vprintf(fmt, args);
    printf("\n");

    va_end(args);
}

void feather_response_remove_header(FeatherResponse *res, StrView key) {
    if (!res || !key.len || res->header_count == 0) return;

    for (size_t i = 0; i < res->header_count; i++) {
        if (sv_ieq(res->headers[i].key, key) == 0) {
            for (size_t j = i; j + 1 < res->header_count; j++) {
                res->headers[j] = res->headers[j + 1];
            }

            res->header_count -= 1;
            res->headers = realloc(res->headers, res->header_count * sizeof(FeatherHeader));
            return;
        }
    }
}

StrView feather_request_get_header(const FeatherRequest *req, StrView header) {
    for (size_t i = 0; i < req->header_count; ++i) {
        if (sv_ieq(req->headers[i].key, header)) {
            return req->headers[i].value;
        }
    }

    return sv_from_buf(NULL, 0);
}
