#include "../lib-misc.h"
#define MAX_REQUESTS 5
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <ctype.h>

#define spost(s) if (sem_post((s)) != 0) exit_with_sys_err("sem_post")
#define swait(s) if (sem_wait((s)) != 0) exit_with_sys_err("sem_wait")

struct request {
    long long op1;
    long long op2;
    char op;
    size_t i;
};

struct shared {
    struct request buf[MAX_REQUESTS];
    sem_t full;
    sem_t empty;
    sem_t mutex;
    size_t count;

    size_t actives;
    size_t n;
    long long *answers;
    sem_t *asems; // answer semaphores
};

struct shared *create_shared(size_t n) {
    struct shared *s = (struct shared *)malloc(sizeof(struct shared));
    assert(s);

    if (sem_init(&s->full, 0, 0) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->empty, 0, MAX_REQUESTS) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->mutex, 0, 1) != 0) exit_with_sys_err("sem_init");

    s->count = 0;

    s->n = s->actives = n;

    s->answers = (long long *)malloc(sizeof(long long) * n);
    s->asems = (sem_t *)malloc(sizeof(sem_t) * n);
    assert(s->answers && s->asems);

    for (size_t i = 0; i < n; i++) {
        if (sem_init(s->asems + i, 0, 0) != 0) 
            exit_with_sys_err("sem_init");
    }

    return s;
}

void insert_item(struct request *r, struct shared *s, bool last) {
    assert(r && s);

    swait(&s->empty);
    swait(&s->mutex);

    memcpy(s->buf + (s->count++), r, sizeof(struct request));

    if (last) s->actives--;

    spost(&s->mutex);
    spost(&s->full);
}

bool remove_item(struct request *r, struct shared *s) {
    assert(r && s);

    bool ret = true;

    swait(&s->mutex);
    printf("suca %zu %zu\n", s->count,s->actives);
    if ((s->count == 0) && (s->actives == 0))
    {ret = false;
         
    }
    spost(&s->mutex);

    if (ret) {
        swait(&s->full);
        swait(&s->mutex);

        memcpy(r, s->buf + (--(s->count)), sizeof(struct request));

        spost(&s->mutex);
        spost(&s->empty);
    }

    return ret;
}

long long mcwrap(char *s) {
    char *endptr;

    errno = 0;
    long long ret = strtol(s, &endptr, 10);

    if (errno != 0) {
        exit_with_sys_err("strtoll");
    }
    if (endptr == s) {
        exit_with_err_msg("No digits found: %s \n", s);
    }

    return ret;
}

struct file_tdata {
    const char *path;
    size_t i;

    struct shared *s;
};
void *file_tfunc(void *arg) {
    struct file_tdata *t = (struct file_tdata *)arg;
    struct shared *s = t->s;
    char sbuffer[BUFSIZ];

    printf("[FILE-%zu] file da verificare: '%s'\n", t->i, t->path);
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("fopen");

    if (!fgets(sbuffer, BUFSIZ, fp)) exit_with_err_msg("[FILE-%zu] file vuoto o errore\n", t->i);
    long long total = mcwrap(sbuffer);
    printf("[FILE-%zu] valore iniziale della computazione: %lld\n", t->i, total);
    //int c = fgetc(fp);
    /*if (c == EOF || (c != '+' && c != '-' && c != 'x')) 
        exit_with_err_msg("[FILE-%zu] file incompleto o errore 1\n", t->i);
    if (ungetc(c, fp) == EOF) exit_with_sys_err("ungetc\n");*/

    bool work = true;
    bool flag = false;
    struct request r = {.i = t->i};
    int next;
    do {
        r.op1 = total;

        int op = fgetc(fp);
        if ((op != '+' && op != '-' && op != 'x') || op == EOF) exit_with_err_msg("[FILE-%zu] file incompleto o errore 2\n", t->i);
        r.op = (char)op;

        if (!fgets(sbuffer, BUFSIZ, fp)) {
            if (ferror(fp)) exit_with_err_msg("fgets");
            exit_with_err_msg("[FILE-%zu] file incompleto o errore 3\n", t->i);
        }
        r.op2 = mcwrap(sbuffer);

        printf("[FILE-%zu] prossima operazione: '%c %lld'\n", t->i, r.op, r.op2);
        
        int c = fgetc(fp);
        if (c == EOF) exit_with_err_msg("[FILE-%zu] file incompleto o errore 4\n", t->i);
        if (isdigit(c)) work = false;
        if (c == '-') {
            next = fgetc(fp);
            if (next == EOF) exit_with_err_msg("[FILE-%zu] file incompleto o errore 5\n", t->i);
            if (isdigit(next)) {
                flag = true;
                work = false;
            }
        }
        if (ungetc((flag ? next : c), fp) == EOF) exit_with_sys_err("ungetc");

        insert_item(&r, s, !work);
        swait(s->asems + t->i);
        total = s->answers[t->i];
        printf("[FILE-%zu] risultato ricevuto: %lld\n", t->i, total);

    } while (work);

    sbuffer[0] = (flag ? '-' : ' ');
    if (!fgets(sbuffer+1, BUFSIZ-1, fp)) {
        if (ferror(fp)) exit_with_err_msg("fgets");
        exit_with_err_msg("[FILE-%zu] file incompleto o errore 6\n", t->i);
    }
    long long expected = mcwrap(sbuffer);

    bool *ret = (bool *)malloc(sizeof(bool));
    *ret = (expected == total);
    printf("[FILE-%zu] computazione terminata in modo %s: %lld\n", 
        t->i, 
        (*ret) ? "corretto" : "sbagghiato",
        expected
    );

    free(t);
    return ret;
}

void *calc_tfunc(void *arg) {
    struct shared *s = (struct shared *)arg;


    struct request r;
    while (remove_item(&r, s)) {

        switch (r.op) {
            case '+':
                s->answers[r.i] = r.op1 + r.op2;
                break;
            case '-':
                s->answers[r.i] = r.op1 - r.op2;
                break;
            case 'x':
                s->answers[r.i] = r.op1 * r.op2;
                break;
            default:
                exit_with_err_msg("[CALC] not expected char in op field");
        }

        printf("[CALC] calcolo effettuato: %lld %c %lld = %lld per FILE-%zu\n",
            r.op1, r.op, r.op2, s->answers[r.i], r.i
        );

        spost(s->asems + r.i);

    }
    printf("suca\n");
    return NULL;
}

int main(int argc, const char **argv) {

    if (argc == 1) exit_with_err_msg("Usage: %s <calc-file-1> <calc-file-2> ... <calc-file-n>\n", argv[0]);
    size_t n = (size_t)argc-1;

    struct shared *s = create_shared(n);

    pthread_t tids[n+1];
    for (size_t i = 0; i < n; i++) {
        struct file_tdata *f = (struct file_tdata *)malloc(sizeof(struct file_tdata));
        assert(f);

        f->path = argv[i+1];
        f->i = i;
        f->s = s;

        int err = pthread_create(tids + i, NULL, file_tfunc, f);
        if (err != 0) exit_with_err("pthread_create", err);
    }

    int err = pthread_create(tids + n, NULL, calc_tfunc, s);
    if (err != 0) exit_with_err("pthread_create", err);

    void *statuses[n];
    size_t succeded = 0;
    for (size_t i = 0; i < n; i++) {
        err = pthread_join(tids[i], statuses+i);
        if (err != 0) exit_with_err("pthread_join", err);
        if (*((bool*)(statuses[i]))) succeded++;
        free(statuses[i]);
    }
    err = pthread_join(tids[n], NULL);
    if (err != 0) exit_with_err("pthread_join", err);

    printf("[MAIN] verifiche completate con successo: %zu/%zu\n", succeded, n);
}
