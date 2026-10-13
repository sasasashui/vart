#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/sync.h"

#ifdef CONFIG_DEBUG_LOCKS
static _Thread_local unsigned char vart_lock_owner_token;

#define VART_LOCK_DEPTH_MAX 16

static _Thread_local const VartMutex *held_locks[VART_LOCK_DEPTH_MAX];
static _Thread_local unsigned int held_lock_count;

static uintptr_t lock_owner(void)
{
    return (uintptr_t)&vart_lock_owner_token;
}

static void lock_abort(const VartMutex *mutex, const char *reason,
                       const char *file, unsigned int line)
{
    fprintf(stderr,
            "VART lock error: %s: %s at %s:%u (initialized at %s:%u)\n",
            mutex->name, reason, file, line, mutex->init_file,
            mutex->init_line);
    abort();
}

static void lock_set_unowned(VartMutex *mutex)
{
    atomic_store_explicit(&mutex->owner, 0, memory_order_release);
}

static void lock_set_owned(VartMutex *mutex)
{
    atomic_store_explicit(&mutex->owner, lock_owner(), memory_order_release);
}

static int lock_is_owned(const VartMutex *mutex)
{
    return atomic_load_explicit(&mutex->owner, memory_order_acquire) ==
           lock_owner();
}

static void lockdep_acquire(VartMutex *mutex, const char *file,
                            unsigned int line)
{
    const VartMutex *parent;

    if (held_lock_count == VART_LOCK_DEPTH_MAX) {
        lock_abort(mutex, "lock nesting is too deep", file, line);
    }
    if (held_lock_count != 0 && mutex->rank != VART_LOCK_RANK_UNCLASSIFIED) {
        parent = held_locks[held_lock_count - 1];
        if (parent->rank != VART_LOCK_RANK_UNCLASSIFIED &&
            mutex->rank <= parent->rank) {
            lock_abort(mutex, "lock order inversion", file, line);
        }
    }
    held_locks[held_lock_count++] = mutex;
}

static void lockdep_release(VartMutex *mutex, const char *file,
                            unsigned int line)
{
    if (held_lock_count == 0 || held_locks[held_lock_count - 1] != mutex) {
        lock_abort(mutex, "non-LIFO unlock", file, line);
    }
    held_lock_count--;
}
#else
static void lock_set_unowned(VartMutex *mutex)
{
    (void)mutex;
}

static void lock_set_owned(VartMutex *mutex)
{
    (void)mutex;
}

static void lockdep_acquire(VartMutex *mutex, const char *file,
                            unsigned int line)
{
    (void)mutex;
    (void)file;
    (void)line;
}

static void lockdep_release(VartMutex *mutex, const char *file,
                            unsigned int line)
{
    (void)mutex;
    (void)file;
    (void)line;
}
#endif

static void pthread_abort(int error, const char *operation)
{
    if (error == 0) {
        return;
    }
    fprintf(stderr, "VART pthread error: %s: %s\n", operation,
            strerror(error));
    abort();
}

int vart_mutex_init_impl(VartMutex *mutex, VartLockRank rank, const char *name,
                         const char *file, unsigned int line)
{
    int ret;

#ifdef CONFIG_DEBUG_LOCKS
    pthread_mutexattr_t attr;

    ret = pthread_mutexattr_init(&attr);
    if (ret != 0) {
        return -ret;
    }
    ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if (ret == 0) {
        ret = pthread_mutex_init(&mutex->mutex, &attr);
    }
    pthread_mutexattr_destroy(&attr);
    if (ret != 0) {
        return -ret;
    }
    atomic_init(&mutex->owner, 0);
    mutex->name = name;
    mutex->init_file = file;
    mutex->init_line = line;
    mutex->rank = rank;
#else
    (void)rank;
    (void)name;
    (void)file;
    (void)line;
    ret = pthread_mutex_init(&mutex->mutex, NULL);
    if (ret != 0) {
        return -ret;
    }
#endif
    return 0;
}

void vart_mutex_destroy_impl(VartMutex *mutex, const char *file,
                             unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (atomic_load_explicit(&mutex->owner, memory_order_acquire) != 0) {
        lock_abort(mutex, "destroying a locked mutex", file, line);
    }
#else
    (void)file;
    (void)line;
#endif
    pthread_abort(pthread_mutex_destroy(&mutex->mutex), "mutex destroy");
}

void vart_mutex_lock_impl(VartMutex *mutex, const char *file,
                          unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (lock_is_owned(mutex)) {
        lock_abort(mutex, "recursive lock", file, line);
    }
#else
    (void)file;
    (void)line;
#endif
    pthread_abort(pthread_mutex_lock(&mutex->mutex), "mutex lock");
    lock_set_owned(mutex);
    lockdep_acquire(mutex, file, line);
}

void vart_mutex_unlock_impl(VartMutex *mutex, const char *file,
                            unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (!lock_is_owned(mutex)) {
        lock_abort(mutex, "unlock by non-owner", file, line);
    }
#else
    (void)file;
    (void)line;
#endif
    lockdep_release(mutex, file, line);
    lock_set_unowned(mutex);
    pthread_abort(pthread_mutex_unlock(&mutex->mutex), "mutex unlock");
}

void vart_mutex_assert_held_impl(const VartMutex *mutex, const char *file,
                                 unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (!lock_is_owned(mutex)) {
        lock_abort(mutex, "mutex is not held", file, line);
    }
#else
    (void)mutex;
    (void)file;
    (void)line;
#endif
}

void vart_mutex_assert_not_held_impl(const VartMutex *mutex, const char *file,
                                     unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (lock_is_owned(mutex)) {
        lock_abort(mutex, "mutex is already held", file, line);
    }
#else
    (void)mutex;
    (void)file;
    (void)line;
#endif
}

int vart_cond_init(VartCond *cond)
{
    int ret = pthread_cond_init(&cond->cond, NULL);

    return ret == 0 ? 0 : -ret;
}

void vart_cond_destroy(VartCond *cond)
{
    pthread_abort(pthread_cond_destroy(&cond->cond), "condition destroy");
}

void vart_cond_wait_impl(VartCond *cond, VartMutex *mutex, const char *file,
                         unsigned int line)
{
#ifdef CONFIG_DEBUG_LOCKS
    if (!lock_is_owned(mutex)) {
        lock_abort(mutex, "condition wait without mutex", file, line);
    }
#else
    (void)file;
    (void)line;
#endif
    lockdep_release(mutex, file, line);
    lock_set_unowned(mutex);
    pthread_abort(pthread_cond_wait(&cond->cond, &mutex->mutex),
                  "condition wait");
    lock_set_owned(mutex);
    lockdep_acquire(mutex, file, line);
}

void vart_cond_signal(VartCond *cond)
{
    pthread_abort(pthread_cond_signal(&cond->cond), "condition signal");
}

void vart_cond_broadcast(VartCond *cond)
{
    pthread_abort(pthread_cond_broadcast(&cond->cond),
                  "condition broadcast");
}

int vart_thread_create(pthread_t *thread, void *(*start)(void *),
                       void *opaque)
{
    sigset_t blocked;
    sigset_t old;
    int restore;
    int ret;

    sigfillset(&blocked);
    sigdelset(&blocked, SIGSEGV);
    sigdelset(&blocked, SIGFPE);
    sigdelset(&blocked, SIGILL);
    sigdelset(&blocked, SIGBUS);

    ret = pthread_sigmask(SIG_SETMASK, &blocked, &old);
    if (ret != 0) {
        return -ret;
    }
    ret = pthread_create(thread, NULL, start, opaque);
    restore = pthread_sigmask(SIG_SETMASK, &old, NULL);
    pthread_abort(restore, "restore thread signal mask");
    return ret == 0 ? 0 : -ret;
}

int vart_thread_block_signal(int signal)
{
    sigset_t blocked;
    int ret;

    sigemptyset(&blocked);
    sigaddset(&blocked, signal);
    ret = pthread_sigmask(SIG_BLOCK, &blocked, NULL);
    return ret == 0 ? 0 : -ret;
}
