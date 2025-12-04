#include "../lib-misc.h"
#include <pthread.h>
#include <stdbool.h>

#define lockm(m) \
    do { \
        int err_macro = pthread_mutex_lock(&(m)); \
        if (err_macro) exit_with_err("pthread_mutex_lock", err_macro); \
    } while (0)

#define unlockm(m) \
    do { \
        int err_macro = pthread_mutex_unlock(&(m)); \
        if (err_macro) exit_with_err("pthread_mutex_unlock", err_macro); \
    } while (0)

#define broadc(c) \
    do { \
        int err_macro = pthread_cond_broadcast(&(c)); \
        if (err_macro) exit_with_err("pthread_cond_broadcast", err_macro); \
    } while (0)

#define signalc(c) \
    do { \
        int err_macro = pthread_cond_signal(&(c)); \
        if (err_macro) exit_with_err("pthread_cond_signal", err_macro); \
    } while (0)

#define waitc(c, m) \
    do { \
        int err_macro = pthread_cond_wait(&(c), &(m)); \
        if (err_macro) exit_with_err("pthread_cond_wait", err_macro); \
    } while (0)


struct shared {
    long long op1;
    long long op2;
    char op;
    long long res;
    //size_t i;

    bool res_set;

    bool free_to_put;

    bool go_ture;

    size_t nprod;

    pthread_mutex_t mut;
    pthread_cond_t ops;
    pthread_cond_t calc;
};

void shared_init(struct shared *s, size_t nprod) {
    assert(s);
    s->res_set = s->go_ture = false;
    s->free_to_put = true;
    //for (size_t i = 0; i < 3; i++) s->ops_set[i] = false;
    s->nprod = nprod;

    int err = pthread_mutex_init(&s->mut, NULL); 
    if (err) exit_with_err("pthread_mutex_lock", err);
    err = pthread_cond_init(&s->ops, NULL); 
    if (err) exit_with_err("pthread_cond_lock", err);
    err = pthread_cond_init(&s->calc, NULL); 
    if (err) exit_with_err("pthread_cond_lock", err);
}

long long parse(char *str) {
    char *endptr;

    errno = 0;
    long long ret = strtoll(str, &endptr, 10);
    
    if (errno) exit_with_sys_err("strtoll");
    
    if (endptr == str) exit_with_err_msg("No digits found while parsing \'%s\'\n", str);

    return ret;
}

char *get_ture(char c) {
    switch (c) {
        case '+':
            return "[ADD]";
        case 'x':
            return "[MUL]";
        case '-':
            return "[SUB]";
        default:
            exit_with_err_msg("get_ture %c\n", c);
    }
}

long long do_what(long long op1, long long op2, char c) {
    switch (c) {
        case '+':
            return op1 + op2;
        case 'x':
            return op1 * op2;
        case '-':
            return op1 - op2;
        default:
            exit_with_err_msg("do_what %c\n", c);
    }
}

struct ture_tdata {
    struct shared *s;
    char c;
};
void *ture_tfunc(void *a) {
    struct ture_tdata *t = (struct ture_tdata *)a;
    struct shared *s = t->s;
    char c = t->c;
    char *tag = get_ture(c);

    bool work = true;
    while (work) {
        lockm(s->mut);
        while (s->nprod > 0 && (!s->go_ture || s->op != c)) 
            waitc(s->ops, s->mut);
        //

        if (s->nprod == 0) work = false;
        else {
            s->res = do_what(s->op1, s->op2, c);
            printf("%s calcolo effettuato: %lld %c %lld = %lld\n", 
                tag, s->op1, c, s->op2, s->res
            );
            s->go_ture = false;
            s->res_set = true;
            broadc(s->calc);
        }

        unlockm(s->mut);
    }

    return NULL;
}

struct calc_tdata {
    struct shared *s;
    size_t i;
    char *path;
};
void *calc_tfunc(void *a) {
    struct calc_tdata *t = (struct calc_tdata *)a;
    size_t i = t->i;
    struct shared *s = t->s;

    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("fopen");

    char buffer[BUFSIZ];
    if (!fgets(buffer, BUFSIZ, fp)) exit_with_sys_err("first fgets");
    long long partial = parse(buffer);
    printf("[CALC-%zu] valore iniziale della computazione: %lld\n", i, partial);

    bool reached_end = false;
    while (!reached_end && fgets(buffer, BUFSIZ, fp)) {

        if (buffer[1] != ' ') {
            reached_end = true;
        } else {
            
            char op = buffer[0];
            buffer[0] = ' ';
            long long op2 = parse(buffer);
            printf("[CALC-%zu] prossima operazione: \'%c %lld\' \n", i, op, op2);

            lockm(s->mut);
            while (!s->free_to_put) waitc(s->calc, s->mut);

            s->free_to_put = false;
            s->go_ture = true;

            s->op1 = partial;
            s->op2 = op2;
            s->op = op;
            //s->i = i;
            unlockm(s->mut);
            broadc(s->ops);

            lockm(s->mut);
            while (!s->res_set) waitc(s->calc, s->mut);

            printf("[CALC-%zu] risultato ricevuto: %lld\n", i, s->res);
            partial = s->res;
            s->free_to_put = true;
            s->res_set = false;
            unlockm(s->mut);
            broadc(s->calc);
        }
    } 
    if (ferror(fp)) exit_with_sys_err("fgets");
    else {
        if (!reached_end) exit_with_err_msg("[CALC-%zu] ended too early\n", i);
    }

    lockm(s->mut);
    if (s->nprod > 0) s->nprod--;
    if (s->nprod == 0) broadc(s->ops);
    unlockm(s->mut);

    long long expected = parse(buffer);
    bool *res = (bool *)calloc(1, sizeof(bool));
    *res = (partial == expected);
    printf("[CALC-%zu] computazione terminata in modo %s: %lld\n", i, (*res?"corretto": "sbagliato"), expected);
    
    return (void *)res;
}

int main(int argc, char **argv) {
    if (argc < 2) exit_with_err_msg("USO: \'%s\' <calc-file-1> <calc-file-2> ... <calc-file-n>\n", argv[0]);

    int err;
    size_t n = (size_t)argc-1;
    bool *exits[n];
    pthread_t tids[n+3];
    struct calc_tdata ctds[n];
    struct ture_tdata ttds[3];
    struct shared s;
    shared_init(&s, n);

    for (size_t i = 0; i < n; i++) {
        ctds[i].path = argv[i+1];
        ctds[i].s = &s;
        ctds[i].i = i;

        if ((err = pthread_create(tids+i, NULL, calc_tfunc, ctds+i)))
            exit_with_err("pthread_create calc", err);
    }

    ttds[0].c = '+'; ttds[1].c = '-'; ttds[2].c = 'x';
    for (size_t i = 0; i < 3; i++) {
        ttds[i].s = &s;

        if ((err = pthread_create(tids+n+i, NULL, ture_tfunc, ttds+i)))
            exit_with_err("pthread_create ture", err);
    }

    for (size_t i = 0; i < n+3; i++) {
        if (i < n) {
            if ((err = pthread_join(tids[i], (void **)exits+i)))
                exit_with_err("pthread_join calc", err);
        } else {
            if ((err = pthread_join(tids[i], NULL)))
                exit_with_err("pthread_join calc", err);
        }
        
    }

    size_t alrighty = 0;
    for (size_t i = 0; i < n; i++) {
        if (*(exits[i]))
            alrighty++;
    }

    printf("\n[MAIN] verifiche completate con successo: %zu/%zu\n", alrighty, n);

}
