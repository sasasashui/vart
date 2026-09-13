#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef CONFIG_DEBUG_LOCKS
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "vart/sync.h"

typedef struct CondTest {
    VartMutex lock;
    VartCond cond;
    int ready;
    int kick_blocked;
    int fault_unblocked;
} CondTest;

static void *signal_thread(void *opaque)
{
    CondTest *test = opaque;
    sigset_t mask;

    pthread_sigmask(SIG_BLOCK, NULL, &mask);

    vart_mutex_lock(&test->lock);
    test->kick_blocked = sigismember(&mask, SIGUSR1) == 1;
    test->fault_unblocked = sigismember(&mask, SIGSEGV) == 0 &&
                            sigismember(&mask, SIGFPE) == 0 &&
                            sigismember(&mask, SIGILL) == 0 &&
                            sigismember(&mask, SIGBUS) == 0;
    test->ready = 1;
    vart_cond_signal(&test->cond);
    vart_mutex_unlock(&test->lock);
    return NULL;
}

#ifdef CONFIG_DEBUG_LOCKS
static void recursive_lock(void)
{
    VartMutex lock;

    vart_mutex_init(&lock);
    vart_mutex_lock(&lock);
    vart_mutex_lock(&lock);
}

static void unowned_unlock(void)
{
    VartMutex lock;

    vart_mutex_init(&lock);
    vart_mutex_unlock(&lock);
}

static void missing_lock(void)
{
    VartMutex lock;

    vart_mutex_init(&lock);
    vart_mutex_assert_held(&lock);
}

static void inverted_locks(void)
{
    VartMutex device_lock;
    VartMutex vm_lock;

    vart_mutex_init_rank(&device_lock, VART_LOCK_RANK_DEVICE);
    vart_mutex_init_rank(&vm_lock, VART_LOCK_RANK_VM);
    vart_mutex_lock(&device_lock);
    vart_mutex_lock(&vm_lock);
}

static void non_lifo_unlock(void)
{
    VartMutex first;
    VartMutex second;

    vart_mutex_init(&first);
    vart_mutex_init(&second);
    vart_mutex_lock(&first);
    vart_mutex_lock(&second);
    vart_mutex_unlock(&first);
}

static int expect_abort(void (*operation)(void))
{
    pid_t child;
    int status;

    child = fork();
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        operation();
        _exit(EXIT_SUCCESS);
    }
    if (waitpid(child, &status, 0) < 0) {
        return -1;
    }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT ? 0 : -1;
}
#endif

int main(void)
{
    CondTest test = { 0 };
    pthread_t thread;
    sigset_t mask_after;
    sigset_t mask_before;
    int ret;

    ret = vart_mutex_init(&test.lock);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_cond_init(&test.cond);
    if (ret < 0) {
        vart_mutex_destroy(&test.lock);
        return EXIT_FAILURE;
    }

    vart_mutex_assert_not_held(&test.lock);
    pthread_sigmask(SIG_BLOCK, NULL, &mask_before);
    vart_mutex_lock(&test.lock);
    vart_mutex_assert_held(&test.lock);
    ret = vart_thread_create(&thread, signal_thread, &test);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    while (!test.ready) {
        vart_cond_wait(&test.cond, &test.lock);
    }
    vart_mutex_unlock(&test.lock);
    pthread_join(thread, NULL);
    pthread_sigmask(SIG_BLOCK, NULL, &mask_after);
    if (!test.kick_blocked || !test.fault_unblocked ||
        sigismember(&mask_before, SIGUSR1) !=
        sigismember(&mask_after, SIGUSR1)) {
        return EXIT_FAILURE;
    }

#ifdef CONFIG_DEBUG_LOCKS
    if (expect_abort(recursive_lock) < 0 ||
        expect_abort(unowned_unlock) < 0 ||
        expect_abort(missing_lock) < 0 ||
        expect_abort(inverted_locks) < 0 ||
        expect_abort(non_lifo_unlock) < 0) {
        return EXIT_FAILURE;
    }
#endif

    vart_cond_destroy(&test.cond);
    vart_mutex_destroy(&test.lock);
    printf("ok - mutex and condition synchronization\n");
    return EXIT_SUCCESS;
}
