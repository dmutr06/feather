#include "coro.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <threads.h>
#include <ucontext.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "dyn_arr.h"

thread_local static ucontext_t main_ctx;
thread_local static DynArr(Coro *) ready_coros = {0};
thread_local static DynArr(Coro *) finished_coros = {0};
thread_local static size_t sleeping_coros_count = 0;
thread_local static int epoll_fd;

void coro_destroy(Coro *coro) {
    free(coro->stack);
}

static void coro_trampoline(uintptr_t ptr) {
    Coro *coro = (Coro *) ptr;
    coro->state = CORO_RUNNING;

    coro->entry.func(coro->entry.arg);

    coro->state = CORO_FINISHED;
    darr_push(&finished_coros, coro);

    setcontext(&main_ctx);
}

static void coro_reset(Coro *coro, void (*func)(void *), void *arg) {
    coro->waiting_events = 0;
    coro->waiting_fd = -1;
    coro->state = CORO_READY;
    coro->entry.func = func;
    coro->entry.arg = arg;
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    coro->ctx.uc_link = &main_ctx;
    makecontext(&coro->ctx, (void (*)(void)) coro_trampoline, 1, (uintptr_t) coro);
}

Coro *coro_spawn(void (*func)(void *), void *arg) {
    Coro *coro;
    if (finished_coros.size > 0) {
        coro = finished_coros.items[finished_coros.size - 1]; 
        darr_pop(&finished_coros);
    } else {
        coro = (Coro *) malloc(sizeof(Coro));
        coro->stack = malloc(CORO_STACK_SIZE);
    }

    coro_reset(coro, func, arg);
    darr_push(&ready_coros, coro);

    return coro;
}

void coro_yield(void) {
    Coro *coro = ready_coros.items[0];
    coro->state = CORO_SUSPENDED;
    swapcontext(&coro->ctx, &main_ctx);
}

void coro_sleep_fd(int fd, int events) {
    if (fd < 0) {
        coro_yield();
        return;
    }

    Coro *coro = ready_coros.items[0];
    coro->state = CORO_SLEEPING;
    coro->waiting_fd = fd;
    coro->waiting_events = events;

    sleeping_coros_count += 1;

    struct epoll_event ev = {
        .events = events,
        .data.ptr = coro
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
        } else {
            perror("epoll_ctl");
            exit(1);
        }
    }

    swapcontext(&coro->ctx, &main_ctx);
}

void coro_sleep_ms(int ms) {
    if (ms <= 0) {
        coro_yield();
        return;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    struct itimerspec its = {0};
    its.it_value.tv_sec = ms / 1000;
    its.it_value.tv_nsec = (ms % 1000) * 1000000;

    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        coro_yield();
        return;
    }

    coro_sleep_fd(tfd, EPOLLIN);
    int a;
    read(tfd, &a, 4);
    close(tfd);
}

void coro_start(void) {
    getcontext(&main_ctx);

    epoll_fd = epoll_create1(0);
    struct epoll_event events[64];

    while (ready_coros.size > 0 || sleeping_coros_count > 0) {
        while (ready_coros.size > 0) {
            Coro *coro = ready_coros.items[0];

            coro->state = CORO_RUNNING;
            swapcontext(&main_ctx, &coro->ctx);

            if (coro->state == CORO_FINISHED || coro->state == CORO_SLEEPING) {
                ready_coros.items[0] = ready_coros.items[ready_coros.size - 1];
                ready_coros.size -= 1;
            }

            else {
                ready_coros.items[0] = ready_coros.items[ready_coros.size - 1];
                ready_coros.items[ready_coros.size - 1] = coro;
            }
        }


        if (!sleeping_coros_count) continue;
        int n = epoll_wait(epoll_fd, events, 64, ready_coros.size > 0 ? 0 : -1);

        for (int i = 0; i < n; ++i) {
            Coro *coro = (Coro *) events[i].data.ptr;
            if (events[i].events & coro->waiting_events) {
                coro->state = CORO_READY;
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, coro->waiting_fd, NULL);
                coro->waiting_fd = -1;
                coro->waiting_events = 0;
                sleeping_coros_count -= 1;
                darr_push(&ready_coros, coro);
            }
        }
    }

    close(epoll_fd);

    darr_foreach(Coro *, &ready_coros, coro) {
        coro_destroy(*coro);
        free(*coro);
    }

    darr_foreach(Coro *, &finished_coros, coro) {
        coro_destroy(*coro);
        free(*coro);
    }

    darr_deinit(&ready_coros);
    darr_deinit(&finished_coros);
}
