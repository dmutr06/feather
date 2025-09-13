#ifndef __FEATHER_H__
#define __FEATHER_H__

#include <stddef.h>
#include "strview.h"

#define __FEATHER_MAX_PARAMS 16

#ifdef FEATHER_LOG
    #define FEATHER_LOG_REQUEST(req) feather_log_request(req)
#else
    #define FEATHER_LOG_REQUEST(req) ((void)0)
#endif

typedef enum {
    FEATHER_GET,
    FEATHER_POST,
    FEATHER_PUT,
    FEATHER_DELETE,
    FEATHER_PATCH,
    FEATHER_OPTIONS,
    FEATHER_HEAD,
    FEATHER_UNKNOWN
} FeatherMethod;

typedef struct {
    StrView key;
    StrView value;
} FeatherHeader;

typedef struct {
    StrView key;
    StrView value;
} FeatherParam;

typedef struct {
    FeatherMethod method;
    StrView path;
    FeatherParam params[__FEATHER_MAX_PARAMS];
    size_t param_count;
    FeatherHeader *headers;
    size_t header_count;
    StrView body;
} FeatherRequest;

typedef struct {
    int status;
    FeatherHeader *headers;
    size_t header_count;
    StrView body;
} FeatherResponse;

typedef struct FeatherCtx FeatherCtx;

typedef void (*FeatherHandler)(const FeatherRequest *req, FeatherCtx *ctx);

typedef enum { FEATHER_ROUTE_STATIC, FEATHER_ROUTE_REGEX } FeatherRouteType;

typedef struct {
    FeatherRouteType type;
    StrView pattern;
    FeatherMethod method;
    FeatherHandler handler;
} FeatherRoute;

typedef struct {
    FeatherRoute *routes;
    size_t route_count;
} FeatherApp;

const char *feather_method_to_str(FeatherMethod method);
FeatherMethod feather_sv_to_method(StrView sv);

void feather_parse_request(FeatherRequest *req, StrView raw);

size_t feather_dump_response(const FeatherResponse *response, char *buf, size_t buf_size);
void feather_response_remove_header(FeatherResponse *res, StrView key);
void feather_response_set_header(FeatherResponse *res, StrView key, StrView value);

StrView feather_request_get_header(const FeatherRequest* req, StrView header);

void feather_init_app(FeatherApp *app);
void feather_add_route(FeatherApp *app, FeatherMethod method, const char *path, FeatherHandler handler);

#define feather_get(app, path, handler) feather_add_route(app, FEATHER_GET, path, handler)
#define feather_post(app, path, handler) feather_add_route(app, FEATHER_POST, path, handler)

FeatherHandler feather_find_handler(const FeatherApp *app, FeatherRequest *req);

void feather_log(const char *fmt, ...);
void feather_log_request(const FeatherRequest *req);

// Platform-dependent funcs
int feather_run(FeatherApp *app, int port);
void feather_response_send(FeatherCtx *ctx, FeatherResponse *res);
void feather_sleep_fd(int fd, int events);
void feather_sleep_ms(int ms);

#endif // __FEATHER_H__
