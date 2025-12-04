#include "safe-stack.h"

void stack_init(struct stack *s, size_t nprod) {
    assert(s);

    s->count = 0;
    s->nprod = nprod;

    int err = pthread_mutex_init(&s->mut, NULL);
    if (err != 0) exit_with_err("pthread_mutex_init", err);

    if (sem_init(&s->empty, 0, SSIZ) != 0) exit_with_sys_err("sem_init 1");
    if (sem_init(&s->full, 0, 0) != 0) exit_with_sys_err("sem_init 2");
}

void push(struct stack *s, struct record *r) {
    assert(s && r);

    swait(&s->empty);
    lockm(&s->mut);

    memcpy(&(s->ar[s->count++]), r, sizeof(struct record));

    if (r->eow) s->nprod--;

    unlockm(&s->mut);
    spost(&s->full);
}

void pop(struct stack *s, struct record *r) {
    assert(s && r);

    lockm(&s->mut);
    if (s->count == 0 && s->nprod == 0) {
        r->filename = NULL;
    }
    unlockm(&s->mut);

    if (r->filename) {
        swait(&s->full);
        lockm(&s->mut);

        memcpy(r, &(s->ar[--(s->count)]), sizeof(struct record));

        unlockm(&s->mut);
        spost(&s->empty);
    }
}