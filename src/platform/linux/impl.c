#include "feather.h"
#include "coro.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

struct FeatherCtx {
    int fd;
    int keep_alive;
};

static FeatherApp *_app;

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
    int cfd = (intptr_t) arg;
    set_nonblocking(cfd);

    FeatherCtx ctx = { .fd = cfd, .keep_alive = 1 };
    while (ctx.keep_alive) {
        char buf[4096];
        ConnBuf cbuf = {0};
        cbuf.buf = buf;
        ssize_t total = 0;

        while (1) {
            ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);

            if (n > 0) {
                total += n;
                cbuf.len = total;
                if (http_request_complete_buf(&cbuf)) {
                    buf[total] = '\0';
                    break;
                };
                continue;
            }

            if (n == 0 && !ctx.keep_alive) {
                close(cfd);
                return;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coro_sleep_fd(cfd, EPOLLIN);
                continue;
            }

            close(cfd);
            return;
        }

        FeatherRequest req = {0};
        feather_parse_request(&req, buf);
        total  = 0;

        for (size_t i = 0; i < req.header_count; ++i) {
            if (strcasecmp("Connection", req.headers[i].key) == 0) {
                if (strcmp(req.headers[i].value, "close") == 0) {
                    ctx.keep_alive = 0;
                }
            } 
        }


        FeatherHandler h = feather_find_handler(_app, &req);

        if (h) {
            h(&req, &ctx);
        } else {
            FeatherResponse res = {0};
            res.status = 404;
            feather_response_set_body(&res, "Not Found");
            feather_response_send(&ctx, &res);
        }
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
        feather_response_set_header(res, "Connection", "close");
    }


    char content_length[21];
    if (res->body) {
        snprintf(content_length, sizeof(content_length), "%zu", res->body_length);
        feather_response_set_header(res, "Content-Length", content_length);
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

    if (!ctx->keep_alive) {
        close(ctx->fd);
    }
}

void feather_sleep_fd(int fd, int events) {
    coro_sleep_fd(fd, events);
}

void feather_sleep_ms(int ms) {
    coro_sleep_ms(ms);
}
