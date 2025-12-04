#include "../lib-misc.h"
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

#define swait(s) do { if (sem_wait((s)) != 0) exit_with_sys_err("sem_wait()");} while (0);
#define spost(s) do { if (sem_post((s)) != 0) exit_with_sys_err("sem_post()");} while (0);

struct ogloc {
    long long op[2];
    char ops;
    long long res;
    bool work;

    sem_t sem1; // initialized to 0
    sem_t sem2; // initialized to 1
    short flags[3];
};

struct operand_tdata {
    short i;
    char *path;

    struct ogloc *sh;
};
void *opx_tfunc(void *arg) {

    struct operand_tdata *s = (struct operand_tdata *)arg;
    struct ogloc *sh = s->sh;
    short i = s->i;
    bool work = true;

    FILE *fp = fopen(s->path, "r");
    if (!fp) {
        fprintf(stderr, "[OP%d] %s ", i, s->path);
        exit_with_sys_err("fopen");
    }
    printf("[OP%d] leggo gli operandi dal file %s\n", i, s->path);

    char buffer[32];
    size_t count = 0;
    do {

        if (fgets(buffer, 32, fp)) {

            char *end;
            errno = 0;
            sh->op[i] = strtoll(buffer, &end, 10);
            if (errno) exit_with_sys_err("Overflow/underflow");
            if (buffer == end) exit_with_err_msg("[OP%d] No number read\n", i);

            printf("[OP%d] %s operando n.%zu: %lld\n",
                i,
                (i == 0) ? "primo" : "secondo",
                ++count, 
                sh->op[i]);

        } else {
            if (ferror(fp)) exit_with_sys_err("Error on file");
            work = false;
        }

        if (work) {
            swait(&sh->sem1);
            sh->flags[i] = 1;
            if (sh->flags[0] && sh->flags[1] && sh->flags[2]) spost(&sh->sem2);
            spost(&sh->sem1);

            short busy = 1;
            do {
                swait(&sh->sem1);
                busy = sh->flags[i];
                spost(&sh->sem1);
            } while (busy);
        }
        
    } while (work);

    fclose(fp);
    printf("[OP%d] termino\n", i);
    return NULL;
}

struct other_tdata {
    char *path;

    struct ogloc *sh;
};
void *ops_tfunc(void *arg) {

    struct other_tdata *s = (struct other_tdata *)arg;
    struct ogloc *sh = s->sh;
    long long prog = 0;
    bool work = true;

    FILE *fp = fopen(s->path, "r");
    if (!fp) {
        fprintf(stderr, "[OPS] %s ", s->path);
        exit_with_sys_err("fopen");
    }
    printf("[OPS] leggo gli operandi dal file %s\n", s->path);

    char buffer[32];
    size_t count = 0;
    long long expected;
    do {

        if (count != 0) {
            prog += sh->res;
            printf("[OPS] sommatoria dei risultati parziali dopo %zu operazione/i: %lld\n", count, prog);
        }

        if (fgets(buffer, 32, fp)) {

            switch (buffer[0]) {
                case '+':
                case 'x':
                case '-':
                    sh->ops = buffer[0];
                    printf("[OPS] operazione n.%zu: %c\n", ++count, buffer[0]);
                    break;
                default:
                    char *end;
                    errno = 0;
                    expected = strtoll(buffer, &end, 10);
                    if (errno) exit_with_sys_err("[OPS] Overflow/underflow");
                    if (buffer == end) exit_with_err_msg("[OPS] No number read\n");
                    sh->work = false;
                    work = false;
                    break;
            }

        } else {
            if (ferror(fp)) exit_with_sys_err("Error on file");
            work = false;
        }

        if (work) {
            swait(&sh->sem1);
            sh->flags[2] = 1;
            if (sh->flags[0] && sh->flags[1] && sh->flags[2]) spost(&sh->sem2);
            spost(&sh->sem1);

            short busy = 1;
            do {
                swait(&sh->sem1);
                busy = sh->flags[2];
                spost(&sh->sem1);
            } while (busy);
        } else {
            spost(&sh->sem2);
        }
    } while (work);

    printf("[CALC] risultato finale atteso: %lld %s\n",
        expected, (expected == prog) ? "(corretto)" : "(sbagliato!!)"
    );

    fclose(fp);
    printf("[OPS] termino\n");
    return NULL;
}

void *calc_tfunc(void *arg) {

    struct ogloc *sh = (struct ogloc *)arg;
    bool work = true;

    size_t count = 0;
    do {

        swait(&sh->sem2);

        if (!sh->work) work = false;

        if (work) {
            
            switch (sh->ops) {
                case '+': sh->res = sh->op[0] + sh->op[1]; break;
                case '-': sh->res = sh->op[0] - sh->op[1]; break;
                case 'x': sh->res = sh->op[0] * sh->op[1]; break;
                default: exit_with_err_msg("[CALC] invalid op character %c\n", sh->ops);
            }
            printf("[CALC] operazione minore n.%zu: %lld %c %lld = %lld\n",
                ++count, sh->op[0], sh->ops, sh->op[1], sh->res
            );

            swait(&sh->sem1);
            for (short i = 0; i < 3; i++) sh->flags[i] = 0;
            spost(&sh->sem1);
        }

    } while (work);

    printf("[CALC] termino\n");
    return NULL;
}

int main(int argc, char **argv) {

    if (argc != 4) {
        exit_with_err_msg("[MAIN] check args\n");
    }

    struct ogloc sh = {.work = true, .flags = {0}};
    if (sem_init(&sh.sem1, PTHREAD_PROCESS_PRIVATE, 1) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&sh.sem2, PTHREAD_PROCESS_PRIVATE, 0) != 0) exit_with_sys_err("sem_init");

    struct operand_tdata td0 = {0, argv[1], &sh};
    struct operand_tdata td1 = {1, argv[2], &sh};
    struct other_tdata tds = {argv[3], &sh};

    printf("[MAIN] creo quei bastardi\n");

    pthread_t tid[4];
    int err = pthread_create(&tid[0], NULL, opx_tfunc, &td0);
    if (err) exit_with_err("pthread_create 0", err);
    err = pthread_create(&tid[1], NULL, opx_tfunc, &td1);
    if (err) exit_with_err("pthread_create 1", err);
    err = pthread_create(&tid[2], NULL, ops_tfunc, &tds);
    if (err) exit_with_err("pthread_create 2", err);
    err = pthread_create(&tid[3], NULL, calc_tfunc, &sh);
    if (err) exit_with_err("pthread_create 3", err);

    for (short i = 0; i < 4; i++) {
        err = pthread_join(tid[i], NULL);
        if (err) exit_with_err("pthread_join", err);
    }

    printf("[MAIN] termino il processo\n");


}

