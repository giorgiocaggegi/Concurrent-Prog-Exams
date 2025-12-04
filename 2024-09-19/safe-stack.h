#ifndef SAFE_STACK_H
#define SAFE_STACK_H

#include "../lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#define BSIZ 1024 // block size
#define SSIZ 10 // stack size

#define lockm(m) \
    do { \
        int errmacro = pthread_mutex_lock((m)); \
        if (errmacro != 0) exit_with_err("pthread_mutex_lock", errmacro); \
    } while (0)
#define unlockm(m) \
    do { \
        int errmacro = pthread_mutex_unlock((m)); \
        if (errmacro != 0) exit_with_err("pthread_mutex_unlock", errmacro); \
    } while (0)
#define swait(m) if (sem_wait((m)) != 0) exit_with_sys_err("sem_wait")
#define spost(m) if (sem_post((m)) != 0) exit_with_sys_err("sem_post")

struct record {
    char block[BSIZ];
    char *filename;
    size_t totsiz;
    size_t curpos;
    size_t bcount;
    size_t tag;
    bool eow;
};

struct stack {
    struct record ar[SSIZ];
    size_t count;
    size_t nprod;

    sem_t full;
    sem_t empty;
    pthread_mutex_t mut;
};

void stack_init(struct stack *s, size_t nprod);
void push(struct stack *s, struct record *r);
void pop(struct stack *s, struct record *r);

#endif
