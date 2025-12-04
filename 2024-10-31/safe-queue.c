#include "safe-queue.h"

void queue_init(struct queue *q, size_t nprod, size_t ncons) {
    assert(q);

    q->top = q->bot = q->count = 0;
    q->nprod = nprod;
    q->ncons = ncons;

    int err = pthread_mutex_init(&q->mut, NULL);
    if (err) exit_with_err("pthread_mutex_init", err);

    if (sem_init(&q->empty, 0, QSIZ)) exit_with_sys_err("sem_init");
    if (sem_init(&q->full, 0, 0)) exit_with_sys_err("sem_init");

}

void queue_insert(struct queue *q, char *str, size_t occ) {

    assert(q && str);

    swait(&q->empty);
    lockm(&q->mut);

    q->ar[q->top].path = str;
    q->ar[q->top].occ = occ;
    q->top = (q->top + 1) % QSIZ;
    q->count++;

    unlockm(&q->mut);
    spost(&q->full);

}

void queue_insert_str(struct queue *q, char *str) {

    assert(q);

    swait(&q->empty);
    lockm(&q->mut);

    q->ar[q->top].path = str;
    q->top = (q->top + 1) % QSIZ;
    q->count++;

    unlockm(&q->mut);
    spost(&q->full);

}

struct entry queue_remove(struct queue *q) {
    assert(q);

    struct entry ret = {NULL, 0};

    swait(&q->full);
    lockm(&q->mut);

    if (q->count > 0) {
        ret = q->ar[q->bot];
        q->bot = (q->bot + 1) % QSIZ;
        q->count--;
    }

    unlockm(&q->mut);
    spost(&q->empty);
    
    return ret;
}

char *queue_remove_str(struct queue *q) {
    assert(q);

    char *ret = NULL;

    swait(&q->full);
    lockm(&q->mut);

    if (q->count > 0) {
        ret = q->ar[q->bot].path;
        q->bot = (q->bot + 1) % QSIZ;
        q->count--;
    }

    unlockm(&q->mut);
    spost(&q->empty);

    return ret;
}

void queue_decrease(struct queue *q) {

    bool eow = false;

    lockm(&q->mut);
    if (--q->nprod == 0) {eow = true;}
    unlockm(&q->mut);

    if (eow) for (size_t i = 0; i < q->ncons; i++) {queue_insert_str(q, NULL);}

}