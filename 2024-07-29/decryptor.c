#include "../lib-misc.h"
#include <semaphore.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <ctype.h>

#define BSIZ 100
#define ABSIZ 26

#define spost(s) if (sem_post((s)) != 0) exit_with_sys_err("spost")
#define swait(s) if (sem_wait((s)) != 0) exit_with_sys_err("swait")


char decrypt(char c, char *key) {
    
    if (c >= 'a' && c <= 'z') c = toupper(c);
    else if (!(c >= 'A' && c <= 'Z')) return c;

    return key[c-'A'];
}

struct lnode {
    char *key;
    struct lnode *next;
};

void insert(char *c, struct lnode **head) {
    assert(c && head);

    struct lnode *new = (struct lnode *)malloc(sizeof(struct lnode));
    assert(new);
    new->key = (char *)malloc(sizeof(char)*(ABSIZ+1));
    assert(new->key);
    new->key[ABSIZ] = '\0';
    memcpy(new->key, c, ABSIZ);

    if (!(*head)) {
        new->next = NULL;
    } else {
        new->next = *head;
    }
    *head = new;

}

void destroy(struct lnode **head) {
    assert(head);
    if (!(*head)) return;

    struct lnode *cur = *head;
    struct lnode *prev = NULL;
    while (cur) {
        prev = cur;
        cur = cur->next;
        free(prev);
    }

    *head = NULL;
}


struct shared {
    char buf[BSIZ];
    sem_t *sems;
    sem_t mainsem;
    bool eow;
};

struct k_tdata{
    char *key;
    size_t tag;
    struct shared *sh;
};
void *k_tfunc(void *arg) {
    struct k_tdata *t = (struct k_tdata *)arg;
    struct shared *sh = t->sh;

    printf("[K%zu] chiave assegnata: %s\n", t->tag, t->key);

    while (true) {
        swait(&sh->sems[t->tag]);
        if (sh->eow) break;

        size_t slen = strlen(sh->buf);
        printf("[K%zu] sto decifrando la frase di %zu caratteri passata...\n", t->tag, slen);
        for (size_t i = 0; i < slen; i++) sh->buf[i] = decrypt(sh->buf[i], t->key);

        spost(&sh->mainsem);
    }

    free(t->key);
    free(t);
    return NULL;
}

size_t get_thread_to_wake(char *c) {
    char *endptr;
    errno = 0;
    long res = strtol(c, &endptr, 10);
    if (errno != 0) exit_with_err_msg("Invalid input file: tag conversion failed: %s\n", c);
    if (c == endptr) exit_with_err_msg("Invalid input file: no tag found: %s\n", c);
    if (res <= 0) exit_with_err_msg("Invalid input file: invalid tag: %s\n", c);

    return (size_t)res-1;
}

int main(int argc, const char **argv) {
    if (argc != 3 && argc != 4) exit_with_err_msg("Invalid args\n");

    const char *outpath;

    if (argc == 3) outpath = "plaintext-output-file.txt";
    else outpath = argv[3];

    FILE *out_fp = fopen(outpath, "w");
    if (!out_fp) exit_with_sys_err("fopen 1");
    FILE *keys_fp = fopen(argv[1], "r");
    if (!keys_fp) exit_with_sys_err("fopen 2");
    FILE *in_fp = fopen(argv[2], "r");
    if (!in_fp) exit_with_sys_err("fopen 3");

    struct shared *sh = (struct shared *)malloc(sizeof(struct shared));
    assert(sh);
    sh->eow = false;
    if (sem_init(&sh->mainsem, 0, 0) != 0) exit_with_sys_err("sem_init");

    char buf[ABSIZ+4];
    struct lnode *list = NULL;
    size_t keynum = 0;
    while (fgets(buf, ABSIZ+4, keys_fp)) {
        insert(buf, &list);
        keynum++;
    }
    if (ferror(keys_fp)) exit_with_sys_err("fgets");
    if (keynum < 2) exit_with_err_msg("Invalid key file: too few keys\n");

    printf("[M] trovate %zu chiavi: creo i thread K-i necessari\n", keynum);

    sh->sems = (sem_t *)malloc(keynum * sizeof(sem_t));
    assert(sh->sems);
    
    struct lnode *cur = list;
    for (ssize_t i = keynum-1; i >= 0; i--) {
        struct k_tdata *ieie = (struct k_tdata *)malloc(sizeof(struct k_tdata));
        ieie->key = cur->key;
        ieie->tag = (size_t)i;
        ieie->sh = sh;
        if (sem_init(&sh->sems[(size_t)i], 0, 0) != 0) exit_with_sys_err("sem_init");

        //printf("[M] Created a struct for key %s with tag %zu\n", ieie->key, ieie->tag);

        pthread_t tid;
        int err = pthread_create(&tid, NULL, k_tfunc, ieie);
        if (err != 0) exit_with_err("pthread_create", err);
        err = pthread_detach(tid);
        if (err != 0) exit_with_err("pthread_detach", err);

        cur = cur->next;
    }
    destroy(&list);
    if (fclose(keys_fp) != 0) exit_with_sys_err("fclose");

    while (true) {
        size_t count = 0;
        char stag[5] = {0};
        while (true) {
            if (count > 5) exit_with_err_msg("Invalid input file: tag conversion failed\n");
            int c = fgetc(in_fp);
            if (c == EOF) {
                if (ferror(in_fp)) exit_with_sys_err("fgetc");
                break;
            } else {
                if (!isdigit(c)) {
                    if (c == ':') stag[count] = '\0';
                    else exit_with_err_msg("Invalid input file: invalid tag\n");
                    break;
                }
                stag[count] = (char)c;
                count++;
            }
        }
        if (fgets(sh->buf, BSIZ, in_fp)) {

            size_t nlpos = strcspn(sh->buf, "\n");
            if (nlpos == BSIZ) exit_with_err_msg("Buffer too short\n");
            sh->buf[nlpos] = '\0';

            //size_t mauro = get_thread_to_wake(sh->buf);
            size_t mauro = get_thread_to_wake(stag);
            printf("[M] la riga '%s' deve essere decifrata con la chiave n.%zu\n", sh->buf, mauro+1);
            spost(&(sh->sems[mauro]));

            swait(&sh->mainsem);
            printf("[M] la riga è stata decifrata in: %s\n", sh->buf);
            if (fputs(sh->buf, out_fp) == EOF) exit_with_sys_err("fputs");
            if (fputc('\n', out_fp) == EOF) exit_with_sys_err("fputs");

        } else {
            if (ferror(in_fp)) exit_with_sys_err("fgets");
            break;
        }
    }
    sh->eow = true;
    for (size_t i = 0; i < keynum; i++) {
        spost(&(sh->sems[i]));
    }

    // MEMORY LEAKS:
    // sh: struct shared
    // sh->sems

}
