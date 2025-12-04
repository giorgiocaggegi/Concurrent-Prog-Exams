#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H
#include "../lib-misc.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#define QSIZ 10
#define spost(s) if (sem_post((s))) exit_with_sys_err("sem_post")
#define swait(s) if (sem_wait((s))) exit_with_sys_err("sem_wait")
#define lockm(m) \
    do { \
        int err_mmm = pthread_mutex_lock((m)); \
        if (err_mmm) exit_with_err("pthread_mutex_lock", err_mmm); \
    } while (0)
#define unlockm(m) \
    do { \
        int err_mmm = pthread_mutex_unlock((m)); \
        if (err_mmm) exit_with_err("pthread_mutex_unlock", err_mmm); \
    } while (0)

// 15:10

struct entry {
    char *path;
    size_t occ;
};

struct queue {
    struct entry ar[QSIZ];
    size_t top; // NEXTONE
    size_t bot;
    size_t count;
    size_t nprod;
    size_t ncons; // nessuno la deve toccare

    pthread_mutex_t mut;
    sem_t empty;
    sem_t full;
};

void queue_init(struct queue *q, size_t nprod, size_t ncons);
void queue_insert(struct queue *q, char *str, size_t occ);
void queue_insert_str(struct queue *q, char *str);
struct entry queue_remove(struct queue *q);
char *queue_remove_str(struct queue *q);
void queue_decrease(struct queue *q);

#endif
