#include "../lib-misc.h"
#include <stdbool.h>
#include <pthread.h>
#include <ctype.h>

#define lockm(m) \
    do { \
        int err = pthread_mutex_lock((m)); \
        if (err != 0) exit_with_err("pthread_mutex_lock", err); \
    } while (0)
#define unlockm(m) \
    do { \
        int err = pthread_mutex_unlock((m)); \
        if (err != 0) exit_with_err("pthread_mutex_unlock", err); \
    } while (0)
#define waitc(c, m) \
    do { \
        int err = pthread_cond_wait((c),(m)); \
        if (err != 0) exit_with_err("pthread_cond_wait", err); \
    } while (0)
#define broadc(c) \
    do { \
        int err = pthread_cond_broadcast((c)); \
        if (err != 0) exit_with_err("pthread_cond_broadcast", err); \
    } while (0)
#define BSIZ 32
#define work(s) ((s)->work1 || (s)->work2 || (s)->work3)

struct shared_tdata {
    long long op1;
    long long op2;
    char op;
    long long res;

    bool fl1;
    bool fl2;
    bool fl3;
    bool work1;
    bool work2;
    bool work3;

    pthread_cond_t fempty; // empty fields
    pthread_cond_t ffull; // full fields
    pthread_mutex_t datamut;

};
void stdata_init(struct shared_tdata *s) {
    assert(s);
    s->fl1 = s->fl2 = s->fl3 = false;
    s->work1 = s->work2 = s->work3 = true;

    int err = pthread_mutex_init(&s->datamut, NULL); 
    if (err != 0) exit_with_err("pthread_mutex_init", err);

    err = pthread_cond_init(&s->fempty, NULL); 
    if (err != 0) exit_with_err("pthread_cond_init", err); 

    err = pthread_cond_init(&s->ffull, NULL); 
    if (err != 0) exit_with_err("pthread_cond_init", err); 
}

struct private_tdata {
    struct shared_tdata *s;
    char *path;
};

long long parse(char *str) {
    char *endptr;
    errno = 0;    /* To distinguish success/failure after call */
    long long val = strtoll(str, &endptr, 10);
    if (errno != 0) {
        perror("strtol");
        exit(EXIT_FAILURE);
    }
    if (endptr == str) {
        exit_with_err_msg("No digits were found\n");
    }
    return val;
}

void *op1_tfunc(void *arg) {
    struct private_tdata *t = (struct private_tdata *)arg;
    struct shared_tdata *s = t->s;
    
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[OP1]: fopen");

    printf("[OP1]: reading operands from %s\n", t->path);

    bool work = true;
    char buf[BSIZ];
    size_t count = 0;
    do {
        char *got = fgets(buf, BSIZ, fp);
        if (!got) {
            if (ferror(fp)) exit_with_sys_err("[OP1]: got a ferror");
            lockm(&s->datamut);
            s->work1 = false;
            if (!work(s)) broadc(&s->ffull);
            unlockm(&s->datamut);
            work = false;
        } else {
            long long value = parse(got);

            printf("[OP1]: first op n.%zu: %lld\n", count, value);

            lockm(&s->datamut);
            while (s->fl1) waitc(&s->fempty, &s->datamut);
            s->op1 = value;
            s->fl1 = true;
            if (s->fl1 && s->fl2 && s->fl3) broadc(&s->ffull);
            unlockm(&s->datamut);
        }
        count++;
    } while (work);

    printf("[OP1]: Terminating\n");
    return NULL;
}

void *op2_tfunc(void *arg) {
    struct private_tdata *t = (struct private_tdata *)arg;
    struct shared_tdata *s = t->s;
    
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[OP2]: fopen");

    printf("[OP2]: reading operands from %s\n", t->path);

    bool work = true;
    char buf[BSIZ];
    size_t count = 0;
    do {
        char *got = fgets(buf, BSIZ, fp);
        if (!got) {
            if (ferror(fp)) exit_with_sys_err("[OP2]: got a ferror");
            work = false;
            lockm(&s->datamut);
            s->work2 = false;
            if (!work(s)) broadc(&s->ffull);
            unlockm(&s->datamut);
        } else {
            long long value = parse(got);

            printf("[OP2]: second op n.%zu: %lld\n", count, value);

            lockm(&s->datamut);
            while (s->fl2) waitc(&s->fempty, &s->datamut);
            s->op2 = value;
            s->fl2 = true;
            if (s->fl1 && s->fl2 && s->fl3) broadc(&s->ffull);
            unlockm(&s->datamut);
        }
        count++;
    } while (work);

    printf("[OP2]: Terminating\n");
    return NULL;
}

void *ops_tfunc(void *arg) {
    struct private_tdata *t = (struct private_tdata *)arg;
    struct shared_tdata *s = t->s;

    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[OPS]: fopen");

    printf("[OPS]: reading operations and expected result from %s\n", t->path);

    bool work = true;
    char buf[BSIZ];
    size_t count = 0;
    long long partial = 0;
    char *got;
    do {

        lockm(&s->datamut);
        while (s->fl3) waitc(&s->fempty, &s->datamut);

        got = fgets(buf, BSIZ, fp);
        if (!got) {
            exit_with_sys_err("[OPS]: got a ferror or an early EOF");
        }
        
        if (count != 0) {
            partial += s->res;
            printf("[OPS] partial total after %zu ops: %lld\n", count, partial);
        }
        if (isdigit((unsigned char)got[0])) {
            work = false;
            s->work3 = false;
            if (!work(s)) broadc(&s->ffull);
            unlockm(&s->datamut);
        } else {
            switch (got[0]) {
                case '+':
                case '-':
                case 'x':
                    printf("[OPS] operation n.%zu: %c\n", count, got[0]);
                    s->op = got[0];
                    break;
                default:
                    exit_with_err_msg("[OPS]: got unexpected char! %c\n", got[0]);
            }
            if (work) {
                s->fl3 = true;
                if (s->fl1 && s->fl2 && s->fl3) broadc(&s->ffull);
                count++;
            }
            unlockm(&s->datamut);
        }


        
    } while (work);

    long long expected = parse(got);
    
    printf("[OPS]: expected: %llu %s\n", expected, (expected == partial) ? "(correct)":"(wrong!!)");
    printf("[OPS]: Terminating\n");
    return NULL;
}

void *calc_tfunc(void *arg) {
    struct shared_tdata *s = (struct shared_tdata *)arg;
    printf("[CALC] started\n");
    
    bool work = true;
    size_t count = 0;
    do {
        lockm(&s->datamut);
        while (work(s) && !(s->fl1 && s->fl2 && s->fl3)) waitc(&s->ffull, &s->datamut);
        
        if (work(s) || (s->fl1 && s->fl2 && s->fl3)) {
            switch (s->op) {
                case '+':
                    s->res = s->op1 + s->op2;
                    printf("[CALC] minor op n.%zu: %lld + %lld = %lld\n", count, s->op1, s->op2, s->res);
                    break;
                case '-':
                    s->res = s->op1 - s->op2;
                    printf("[CALC] minor op n.%zu: %lld - %lld = %lld\n", count, s->op1, s->op2, s->res);
                    break;
                case 'x':
                    s->res = s->op1 * s->op2;
                    printf("[CALC] minor op n.%zu: %lld x %lld = %lld\n", count, s->op1, s->op2, s->res);
                    break;
                default:
                    exit_with_err_msg("[OPS]: got unexpected char!\n");
            }
            s->fl1 = s->fl2 = s->fl3 = false;
            broadc(&s->fempty);
            unlockm(&s->datamut);

            count++;
        } else {
            work = false;
            unlockm(&s->datamut);
        }
    } while (work);
    
    printf("[CALC]: Terminating\n");
    return NULL;
}

int main(int argc, char **argv) {

    if (argc != 4) 
        exit_with_err_msg("Usage: <%s> <first-operands> <second-operands> <operations>", argv[0]);
    
    int err;
    pthread_t tids[4];
    void *(*tfuncs[4])(void *) = {op1_tfunc, op2_tfunc, ops_tfunc, calc_tfunc};
    
    /*struct shared_tdata s;
    stdata_init(&s);
    struct private_tdata privs[3];
     
    
    for (size_t i = 0; i < 4; i++) {
        privs[i].s = &s;
        privs[i].path = argv[i+1];
        
        err = pthread_create(&tids[i], NULL, tfuncs[i],
                             (i == 3) ? &s : &(privs[i])
                             );
        if (err != 0) exit_with_err("pthread_create", err);
    }
     
    for (size_t i = 0; i < 4; i++) {
        err = pthread_join(tids[i], NULL);
        if (err != 0) exit_with_err("pthread_join", err);
    }*/
     
    
    struct shared_tdata *s = (struct shared_tdata *)calloc(1, sizeof(struct shared_tdata));
    assert(s);
    stdata_init(s);
    
    for (size_t i = 0; i < 4; i++) {
        if (i != 3) {
            struct private_tdata *t = (struct private_tdata *)calloc(1, sizeof(struct private_tdata));
            assert(t);
            t->s = s;
            t->path = argv[i+1];
            
            err = pthread_create(&tids[i], NULL, tfuncs[i], t);
            if (err != 0) exit_with_err("pthread_create", err);
        } else {
            err = pthread_create(&tids[i], NULL, tfuncs[i], s);
            if (err != 0) exit_with_err("pthread_create", err);
        }

        err = pthread_detach(tids[i]);
        if (err != 0) exit_with_err("pthread_detach", err);
    }
    
    printf("[MAIN]: terminating\n");
    pthread_exit(NULL);
    
    // Freeing all the structures if the program has to continue? First thread's address space and joins should be the choice in that case...
}
