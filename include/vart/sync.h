#ifndef VART_SYNC_H
#define VART_SYNC_H

#include <pthread.h>

#ifdef CONFIG_DEBUG_LOCKS
#include <stdatomic.h>
#include <stdint.h>
#endif

typedef enum VartLockRank {
    VART_LOCK_RANK_UNCLASSIFIED,
    VART_LOCK_RANK_VM = 10,
    VART_LOCK_RANK_ADDRESS_SPACE = 20,
    VART_LOCK_RANK_DEVICE = 30,
    VART_LOCK_RANK_QUEUE = 40,
} VartLockRank;

typedef struct VartMutex {
    pthread_mutex_t mutex;
#ifdef CONFIG_DEBUG_LOCKS
    atomic_uintptr_t owner;
    const char *name;
    const char *init_file;
    unsigned int init_line;
    VartLockRank rank;
#endif
} VartMutex;

typedef struct VartCond {
    pthread_cond_t cond;
} VartCond;

int vart_mutex_init_impl(VartMutex *mutex, VartLockRank rank, const char *name,
                         const char *file, unsigned int line);
void vart_mutex_destroy_impl(VartMutex *mutex, const char *file,
                             unsigned int line);
void vart_mutex_lock_impl(VartMutex *mutex, const char *file,
                          unsigned int line);
void vart_mutex_unlock_impl(VartMutex *mutex, const char *file,
                            unsigned int line);
void vart_mutex_assert_held_impl(const VartMutex *mutex, const char *file,
                                 unsigned int line);
void vart_mutex_assert_not_held_impl(const VartMutex *mutex, const char *file,
                                     unsigned int line);

int vart_cond_init(VartCond *cond);
void vart_cond_destroy(VartCond *cond);
void vart_cond_wait_impl(VartCond *cond, VartMutex *mutex, const char *file,
                         unsigned int line);
void vart_cond_signal(VartCond *cond);
void vart_cond_broadcast(VartCond *cond);

#define vart_mutex_init(mutex) \
    vart_mutex_init_impl((mutex), VART_LOCK_RANK_UNCLASSIFIED, #mutex, \
                         __FILE__, __LINE__)
#define vart_mutex_init_rank(mutex, rank) \
    vart_mutex_init_impl((mutex), (rank), #mutex, __FILE__, __LINE__)
#define vart_mutex_destroy(mutex) \
    vart_mutex_destroy_impl((mutex), __FILE__, __LINE__)
#define vart_mutex_lock(mutex) \
    vart_mutex_lock_impl((mutex), __FILE__, __LINE__)
#define vart_mutex_unlock(mutex) \
    vart_mutex_unlock_impl((mutex), __FILE__, __LINE__)
#define vart_mutex_assert_held(mutex) \
    vart_mutex_assert_held_impl((mutex), __FILE__, __LINE__)
#define vart_mutex_assert_not_held(mutex) \
    vart_mutex_assert_not_held_impl((mutex), __FILE__, __LINE__)
#define vart_cond_wait(cond, mutex) \
    vart_cond_wait_impl((cond), (mutex), __FILE__, __LINE__)

#endif
