#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/event-loop.h"

typedef struct EventContext {
    VartEventSource *remove;
    uint32_t events;
    unsigned int calls;
    int result;
} EventContext;

typedef struct WakeContext {
    VartEventLoop *loop;
    int result;
} WakeContext;

static void *wake_loop(void *opaque)
{
    WakeContext *context = opaque;

    context->result = vart_event_loop_wake(context->loop);
    if (context->result == 0) {
        context->result = vart_event_loop_wake(context->loop);
    }
    return NULL;
}

static int record_event(VartEventSource *source, uint32_t events,
                        void *opaque)
{
    EventContext *context = opaque;
    char byte;

    context->calls++;
    context->events = events;
    if (events & VART_EVENT_READ) {
        if (read(source->fd, &byte, 1) != 1) {
            return -EIO;
        }
    }
    if (context->remove != NULL && context->remove->registered) {
        int ret = vart_event_remove(context->remove);

        if (ret < 0) {
            return ret;
        }
    }
    return context->result;
}

int main(void)
{
    EventContext first_context = { 0 };
    EventContext second_context = { 0 };
    EventContext write_context = { 0 };
    VartEventSource first = { 0 };
    VartEventSource second = { 0 };
    VartEventSource writable = { 0 };
    VartEventLoop uninitialized = { 0 };
    VartEventLoop loop;
    WakeContext wake = { .loop = &loop };
    pthread_t thread;
    int first_pipe[2];
    int second_pipe[2];
    char byte = 'x';
    int ret;

    if (vart_event_loop_init(NULL) != -EINVAL ||
        vart_event_loop_init(&loop) < 0) {
        return EXIT_FAILURE;
    }
    vart_event_loop_destroy(&uninitialized);
    vart_event_source_init(&first);
    vart_event_source_init(&second);
    vart_event_source_init(&writable);
    if (pipe(first_pipe) < 0 || pipe(second_pipe) < 0) {
        return EXIT_FAILURE;
    }
    if (pthread_create(&thread, NULL, wake_loop, &wake) != 0 ||
        vart_event_loop_run_once(&loop, -1) != 0 ||
        pthread_join(thread, NULL) != 0 || wake.result < 0 ||
        vart_event_loop_run_once(&loop, 0) != 0) {
        return EXIT_FAILURE;
    }
    if (vart_event_add(NULL, &first, first_pipe[0], VART_EVENT_READ,
                       record_event, &first_context) != -EINVAL ||
        vart_event_add(&loop, NULL, first_pipe[0], VART_EVENT_READ,
                       record_event, &first_context) != -EINVAL ||
        vart_event_add(&loop, &first, -1, VART_EVENT_READ,
                       record_event, &first_context) != -EINVAL ||
        vart_event_add(&loop, &first, first_pipe[0], VART_EVENT_ERROR,
                       record_event, &first_context) != -EINVAL ||
        vart_event_loop_run_once(NULL, 0) != -EINVAL ||
        vart_event_loop_run_once(&loop, -2) != -EINVAL ||
        vart_event_modify(NULL, VART_EVENT_READ) != -EINVAL ||
        vart_event_modify(&second, VART_EVENT_READ) != -EINVAL ||
        vart_event_add(&loop, &first, first_pipe[0], VART_EVENT_READ,
                       record_event, &first_context) < 0 ||
        vart_event_add(&loop, &first, first_pipe[0], VART_EVENT_READ,
                       record_event, &first_context) != -EEXIST ||
        vart_event_loop_run_once(&loop, 0) != 0 ||
        write(first_pipe[1], &byte, 1) != 1 ||
        vart_event_loop_run_once(&loop, 0) != 1 ||
        first_context.calls != 1 ||
        first_context.events != VART_EVENT_READ) {
        return EXIT_FAILURE;
    }
    if (vart_event_add(&loop, &writable, first_pipe[1], VART_EVENT_WRITE,
                       record_event, &write_context) < 0 ||
        vart_event_loop_run_once(&loop, 0) != 1 ||
        write_context.calls != 1 ||
        write_context.events != VART_EVENT_WRITE ||
        vart_event_remove(&writable) < 0) {
        return EXIT_FAILURE;
    }
    if (vart_event_modify(&first, 0) < 0 ||
        write(first_pipe[1], &byte, 1) != 1 ||
        vart_event_loop_run_once(&loop, 0) != 0 ||
        vart_event_modify(&first, VART_EVENT_READ) < 0 ||
        vart_event_loop_run_once(&loop, 0) != 1 ||
        first_context.calls != 2) {
        return EXIT_FAILURE;
    }

    first_context.remove = &second;
    if (vart_event_add(&loop, &second, second_pipe[0], VART_EVENT_READ,
                       record_event, &second_context) < 0 ||
        write(first_pipe[1], &byte, 1) != 1 ||
        write(second_pipe[1], &byte, 1) != 1 ||
        vart_event_loop_run_once(&loop, 0) != 1 ||
        first_context.calls != 3 || second_context.calls != 0 ||
        second.registered || loop.count != 1) {
        return EXIT_FAILURE;
    }
    first_context.remove = NULL;
    first_context.result = -EIO;
    if (write(first_pipe[1], &byte, 1) != 1 ||
        vart_event_loop_run_once(&loop, 0) != -EIO) {
        return EXIT_FAILURE;
    }
    first_context.result = 0;
    if (vart_event_remove(&first) < 0 ||
        vart_event_remove(&first) != -EINVAL || loop.count != 0 ||
        vart_event_loop_run_once(&loop, 0) != 0) {
        return EXIT_FAILURE;
    }
    if (vart_event_add(&loop, &second, second_pipe[0], VART_EVENT_READ,
                       record_event, &second_context) < 0) {
        return EXIT_FAILURE;
    }
    close(second_pipe[1]);
    second_pipe[1] = -1;
    if (vart_event_loop_run_once(&loop, 0) != 1 ||
        second_context.calls != 1 ||
        !(second_context.events & VART_EVENT_HANGUP) ||
        vart_event_remove(&second) < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_event_add(&loop, &second, second_pipe[0], 0,
                         record_event, &second_context);
    vart_event_loop_destroy(&loop);
    if (ret < 0 || second.registered || second.loop != NULL ||
        vart_event_loop_wake(&loop) != -EINVAL) {
        return EXIT_FAILURE;
    }
    if (second_pipe[1] >= 0) {
        close(second_pipe[1]);
    }
    close(second_pipe[0]);
    close(first_pipe[1]);
    close(first_pipe[0]);
    puts("ok - dispatch file descriptors through the poll event backend");
    return EXIT_SUCCESS;
}
