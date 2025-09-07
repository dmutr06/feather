#include "feather.h"
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

FeatherMethod feather_str_to_method(const char *str) {
    if (strcmp(str, "GET") == 0) return FEATHER_GET;
    if (strcmp(str, "POST") == 0) return FEATHER_POST;
    if (strcmp(str, "PUT") == 0) return FEATHER_PUT;
    if (strcmp(str, "DELETE") == 0) return FEATHER_DELETE;
    if (strcmp(str, "PATCH") == 0) return FEATHER_PATCH;
    if (strcmp(str, "OPTIONS") == 0) return FEATHER_OPTIONS;
    if (strcmp(str, "HEAD") == 0) return FEATHER_HEAD;

    return FEATHER_UNKNOWN;
}

void feather_parse_request(FeatherRequest *req, char *raw) {
    assert(req != NULL);

    char *line = raw;

    char *method_str = strsep(&line, " ");
    char *path_str = strsep(&line, " ");
    char *version = strsep(&line, "\r\n");

    if (!version) return;

    line += 1;

    req->method = feather_str_to_method(method_str);
    req->path = path_str;

    req->headers = NULL;
    req->header_count = 0;

    while (line && *line) {
        if (*line == '\r' || *line == '\n') {
            while (*line == '\r' || *line == '\n') {
                line += 1;
            }

            req->body = line;
            req->body_length = strlen(line);
            return;
        }

        char *header = strsep(&line, "\r\n");
        if (!header || !*header) break;

        char *colon = strchr(header, ':');
        if (!colon) continue;

        *colon = '\0';
        char *key = header;
        char *value = colon + 1;
        while (*value == ' ') value += 1;

        req->headers = realloc(req->headers, (req->header_count + 1) * sizeof(FeatherHeader));
        req->headers[req->header_count].key = key;
        req->headers[req->header_count].value = value;
        req->header_count += 1;

        line += 1;
    }
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
        n = snprintf(buf + offset, buf_size - offset, "%s: %s\r\n", res->headers[i].key, res->headers[i].value);
        if (n < 0 || (size_t) n >= buf_size - offset) return 0;
        offset += (size_t) n;
    }

    int has_len = 0;

    for (size_t i = 0; i < res->header_count; ++i) {
        if (strcasecmp(res->headers[i].key, "Content-Length") == 0) {
            has_len = 1;
            break;
        }
    }

    if (!has_len && res->body) {
        n = snprintf(buf + offset, buf_size - offset, "Content-Length: %zu\r\n", res->body_length);
        if (n < 0 || (size_t) n >= buf_size - offset) return 0;
        offset += (size_t) n;
    }

    if (offset + 2 >= buf_size) return 0;

    buf[offset++] = '\r';
    buf[offset++] = '\n';

    if (res->body && res->body_length > 0) {
        if (offset + res->body_length >= buf_size) return 0;
        memcpy(buf + offset, res->body, res->body_length);
        offset += res->body_length;
    }

    return offset;
}

void feather_response_set_header(FeatherResponse *res, const char *key, const char *value) {
    if (!res || !key || !value) return;

    for (size_t i = 0; i < res->header_count; ++i) {
        if (strcasecmp(res->headers[i].key, key) == 0) {
            res->headers[i].key = (char *) key;
            res->headers[i].value = (char *) value;
            return;
        }
    }

    res->headers = realloc(res->headers, (res->header_count + 1) * sizeof(FeatherHeader));
    res->headers[res->header_count].key = (char *) key;
    res->headers[res->header_count].value = (char *) value;
    res->header_count += 1;
}

void feather_response_set_body(FeatherResponse *res, const char *body) {
    if (!res) return;
    if (!body) {
        res->body = NULL;
        res->body_length = 0;
        return;
    }

    res->body = body;
    res->body_length = strlen(body);
}

void feather_response_set_body_n(FeatherResponse *res, const char *body, size_t len) {
    if (!res) return;
    res->body = body;
    res->body_length = len;
}

void feather_init_app(FeatherApp *app) {
    app->routes = NULL;
    app->route_count = 0;
}

void feather_add_route(FeatherApp *app, FeatherMethod method, const char *path, FeatherHandler handler) {
    app->routes = realloc(app->routes, (app->route_count + 1) * sizeof(FeatherRoute));
    app->routes[app->route_count].method = method;
    app->routes[app->route_count].pattern = path;
    app->routes[app->route_count].handler = handler;

    app->route_count += 1;
}

static void strip_trailing_slash(char *s) {
    size_t len = strlen(s);
    if (len > 1 && s[len - 1] == '/') {
        s[len - 1] = '\0';
    }
}

static int feather_match_route(const char *pattern, FeatherRequest *req) {
    req->param_count = 0;


    if (strcmp(req->path, "/") == 0) {
        return strcmp(pattern, "/") == 0;
    }

    char pattern_copy[256];
    char path_copy[256];
    strcpy(pattern_copy, pattern);
    strcpy(path_copy, req->path);

    strip_trailing_slash(pattern_copy);
    strip_trailing_slash(path_copy);

    char *p_ptr = pattern_copy;
    char *r_ptr = path_copy;
    char *p_seg, *r_seg;

    while ((p_seg = strsep(&p_ptr, "/")) && (r_seg = strsep(&r_ptr, "/"))) {
        if (*p_seg == '\0') continue; // skip empty segments
        if (*r_seg == '\0') continue;

        if (p_seg[0] == ':') {
            if (req->param_count >= __FEATHER_MAX_PARAMS) return 0;
            req->params[req->param_count].key = p_seg + 1;
            req->params[req->param_count].value = r_seg;
            req->param_count++;
        } else if (strcmp(p_seg, r_seg) != 0) {
            return 0; // static segment mismatch
        }
    }


    int res = (p_ptr == NULL || *p_ptr == '\0') && (r_ptr == NULL || *r_ptr == '\0');
    return res;
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

void feather_response_remove_header(FeatherResponse *res, const char *key) {
    if (!res || !key || res->header_count == 0) return;

    for (size_t i = 0; i < res->header_count; i++) {
        if (strcasecmp(res->headers[i].key, key) == 0) {
            for (size_t j = i; j + 1 < res->header_count; j++) {
                res->headers[j] = res->headers[j + 1];
            }

            res->header_count -= 1;
            res->headers = realloc(res->headers, res->header_count * sizeof(FeatherHeader));
            return;
        }
    }
}

void feather_log_request(const FeatherRequest *req) {
    printf("AAA\n");
    if (!req) return;

    feather_log(">>> %s %s", feather_method_to_str(req->method), req->path);

    for (size_t i = 0; i < req->header_count; i++) {
        feather_log("  %s: %s", req->headers[i].key, req->headers[i].value);
    }

    if (req->body && req->body_length > 0) {
        // print body as string safely
        char buf[128];
        size_t len = req->body_length < sizeof(buf)-1 ? req->body_length : sizeof(buf)-1;
        memcpy(buf, req->body, len);
        buf[len] = '\0';
        feather_log("  Body (%zu bytes): %s", req->body_length, buf);
    }
}

