#include "feather.h"
#include "coro.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

struct FeatherCtx {
    int fd;
};

static FeatherApp *_app;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


static void handle_client(void *arg) {
    int cfd = (intptr_t) arg;
    set_nonblocking(cfd);

    char buf[4096];

    while (1) {
        ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);

        if (n > 0) {
            buf[n] = '\0';
            break;
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

    FeatherCtx ctx = { .fd = cfd };

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

static void accept_loop(void *arg) {
    int sfd = (int)(intptr_t) arg;

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

        coro_create(handle_client, (void *)(intptr_t) cfd);
    }
}

int feather_run(FeatherApp *app) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        return 1;
    }
    set_nonblocking(sfd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(7070);

    if (bind(sfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        return 1;
    }

    if (listen(sfd, 128) < 0) {
        return 1;
    }

    _app = app;

    coro_create(accept_loop, (void *)(intptr_t) sfd);

    coro_start();
    
    close(sfd);

    return 0;
}

void feather_response_send(FeatherCtx *ctx, const FeatherResponse *res) {
    if (!ctx || ctx->fd < 0 || !res) return;

    char buf[4096];
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

    close(ctx->fd);
}

void feather_sleep_fd(int fd, int events) {
    coro_sleep_fd(fd, events);
}

void feather_sleep_ms(int ms) {
    coro_sleep_ms(ms);
}
