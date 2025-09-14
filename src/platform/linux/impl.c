#include "feather.h"
#include "coro.h"
#include "strview.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

struct FeatherCtx {
    int fd;
    int keep_alive;
};

static FeatherApp *_app;

thread_local static int counter = 0;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

typedef struct {
    char *buf;
    size_t len;
    size_t parse_offset;
} ConnBuf;

static int http_request_complete_buf(ConnBuf *cbuf) {
    size_t i = cbuf->parse_offset;
    while (i + 3 < cbuf->len) {
        if (
            cbuf->buf[i] == '\r'
            && cbuf->buf[i + 1] == '\n'
            && cbuf->buf[i + 2] == '\r'
            && cbuf->buf[i + 3] == '\n'
        ) {
            cbuf->parse_offset = i;
            return 1;
        }

        i += 1;
    }

    cbuf->parse_offset = i > 3 ? i - 3 : 0;
    return 0;
}

static void handle_client(void *arg) {
    counter += 1;
    int cfd = (intptr_t) arg;
    set_nonblocking(cfd);

    FeatherCtx ctx = { .fd = cfd, .keep_alive = 1 };
    while (ctx.keep_alive) {
        char buf[8192];
        ConnBuf cbuf = {0};
        cbuf.buf = buf;
        ssize_t total = 0;

        while (!http_request_complete_buf(&cbuf)) {
            ssize_t n = recv(cfd, buf + total, sizeof(buf) - total - 1, 0);

            if (n > 0) {
                total += n;
                cbuf.len = total;
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coro_sleep_fd(cfd, EPOLLIN);
                continue;
            }

            close(cfd);
            return;
        }

        char *header_end = strstr(buf, "\r\n\r\n");
        if (!header_end) {
            close(cfd);
            return;
        }
        size_t headers_end = (header_end - buf) + 4;

        FeatherRequest req = {0};
        feather_parse_request(&req, sv_from_buf(buf, headers_end));

        size_t content_length = 0;

        if (req.headers.content_length.len > 0) {
            content_length = sv_atoi(req.headers.content_length);
        }

        while ((size_t) total < headers_end + content_length) {
            ssize_t n = recv(cfd, buf + total, sizeof(buf) - total - 1, 0);
            if (n > 0) {
                total += n;
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coro_sleep_fd(cfd, EPOLLIN);
                continue;
            }

            close(cfd);
            return;
        }

        req.body = sv_from_buf(buf + headers_end, content_length);

        if (sv_ieq(req.headers.connection, "close")) {
            ctx.keep_alive = 0;
        }

        FeatherHandler h = feather_find_handler(_app, &req);

        if (h) {
            h(&req, &ctx);
        } else {
            FeatherResponse res = {0};
            res.status = 404;
            res.body = SV_LIT("<h3>Not Found</h3>");
            res.headers.content_type = SV_LIT("text/html");
            feather_response_send(&ctx, &res);
        }

        darr_deinit(&req.headers.other);
    }
}

static int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(1);
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEPORT");
        exit(1);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr   = { .s_addr = htonl(INADDR_ANY) }
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(1);
    }

    return fd;
}

static void accept_loop(void *arg) {
    int sfd = create_listen_socket((intptr_t) arg);

    while (1) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coro_sleep_fd(sfd, EPOLLIN);
                continue;
            } else {
                perror("accept");
                break;
            }
        }

        coro_spawn(handle_client, (void *)(intptr_t) cfd);
    }

    close(sfd);
}

#define NUM_WORKERS 6

static void *worker(void *arg) {
    coro_spawn(accept_loop, arg);

    coro_start();

    return NULL;
}

int feather_run(FeatherApp *app, int port) {
    pthread_t workers[NUM_WORKERS];
    _app = app;

    for (int i = 0; i < NUM_WORKERS; ++i) {
        pthread_create(&workers[i], NULL, worker, (void *)(intptr_t) port);
    }

    for (int i = 0; i < NUM_WORKERS; ++i) {
        pthread_join(workers[i], NULL);
    }

    return 0;
}


void feather_response_send(FeatherCtx *ctx, FeatherResponse *res) {
    if (!ctx || ctx->fd < 0 || !res) return;

    char buf[1024];
    if (!ctx->keep_alive) {
        res->headers.connection = SV_LIT("close");
    }


    char content_length[21];
    if (res->body.len > 0) {
        int n = snprintf(content_length, sizeof(content_length), "%zu", res->body.len);
        res->headers.content_length = sv_from_buf(content_length, n);
    }

    size_t len = feather_dump_response(res, buf, sizeof(buf));
    size_t sent_total = 0;

    while (sent_total < len) {
        ssize_t sent = send(ctx->fd, buf, len, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coro_sleep_fd(ctx->fd, EPOLLOUT);
                continue;
            } else {
                perror("send");
                break;
            }
        }
        sent_total += (size_t) sent;
    }

    darr_deinit(&res->headers.other);

    if (!ctx->keep_alive) {
        close(ctx->fd);
        counter -= 1;
    }
}

void feather_sleep_fd(int fd, int events) {
    coro_sleep_fd(fd, events);
}

void feather_sleep_ms(int ms) {
    coro_sleep_ms(ms);
}
