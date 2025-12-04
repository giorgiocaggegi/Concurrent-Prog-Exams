#include "../lib-misc.h"
#include <semaphore.h>
#include <stdbool.h>
#include <pthread.h>

#define SSIZ 32
#define swait(s) if (sem_wait((s)) != 0) exit_with_sys_err("sem_wait");
#define spost(s) if (sem_post((s)) != 0) exit_with_sys_err("sem_post");

size_t getnumoflines(FILE *fp) {
    assert(fp);

    if (fseek(fp, 0, SEEK_SET) != 0) exit_with_sys_err("fseek");

    size_t ret = 0;
    int c;

    while (true) {
        c = fgetc(fp);
        if (c == EOF) break;
        if (c == '\n') ret++;
        if (ret > 1000) exit_with_sys_err("porcu diu");
    } 

    if (ferror(fp)) exit_with_sys_err("getchar");

    if (fseek(fp, 0, SEEK_SET) != 0) exit_with_sys_err("fseek");
    clearerr(fp);

    return ret;
}

ssize_t getstrpos(char **ar, size_t arsiz, char *str) {

    size_t low = 0;
    size_t high = arsiz-1;
    size_t mid = 0; 
    bool found = false;

    while (low <= high) {
        mid = (high+low)/2;
        int res = strcmp(ar[mid], str);
        //printf("%s %s %zu %zu %zu %d\n", ar[mid], str, low, mid, high, res);
        if (res == 0) {
            found = true;
            break;
        } else if (res > 0) 
            high = mid - 1;
        else 
            low = mid + 1;
    }

    return (found ? (ssize_t)mid : -1);
}


struct shared {
    size_t nprod;
    bool es_flag;

    char nome[SSIZ];
    char regalo[SSIZ];
    bool buono;
    long costo;

    sem_t es; // 1 initialized
    sem_t bn; // 0 initialized
    sem_t ei; // 0 initialized
    sem_t ep; // 0 initialized
    sem_t ec; // 0 initialized
};

// Elfo Indagatore
struct ei_tdata {
    char *path;

    struct shared *sh;
};
void *ei_tfunc(void *arg) {

    struct ei_tdata *t = (struct ei_tdata *)arg;
    struct shared *sh = t->sh;

    // Apre il file
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[EI] fopen");
    printf("[EI] opened %s\n", t->path);

    size_t lsiz = getnumoflines(fp);
    printf("[EI] lsiz: %zu\n", lsiz);
    
    char **names = (char **)malloc(sizeof(char*) * lsiz);
    assert(names);
    for (size_t i = 0; i < lsiz; i++) {
        names[i] = (char *)malloc(sizeof(char) * SSIZ);
        assert(names[i]);
    }
    bool *buono = (bool *)malloc(sizeof(bool) * lsiz);
    assert(buono);

    char buffer[SSIZ];
    size_t count = 0;
    while (fgets(buffer, SSIZ, fp)) {

        size_t tokpos = strcspn(buffer, ";");
        buffer[tokpos] = '\0';
        char *cabbone = buffer + tokpos + 1;

        strncpy(names[count], buffer, SSIZ);
        buono[count] = (strcmp(cabbone, "buono\n") == 0 ? true : false);

        count++;
    }
    if (ferror(fp)) exit_with_sys_err("fgets");
    //printf("[EI] count: %zu\n", count);

    //for (size_t i = 0; i < count; i++) printf("%s %d\n", names[i], buono[i]);

    while (true) {
        swait(&sh->ei);

        if (sh->nprod == 0) break;

        ssize_t murmari = getstrpos(names, lsiz, sh->nome);
        if (murmari == -1) 
            exit_with_err_msg("[EI] %s not found??\n", sh->nome);

        size_t pos = (size_t)murmari;
        sh->buono = buono[pos];
        printf("[EI] il bambino '%s' è stato %s quest'anno\n",
            sh->nome, (buono[pos] ? "buono" : "cattivo")
        );

        spost(&sh->bn);
    }

    fclose(fp);
    for (size_t i = 0; i < lsiz; i++) {
        free(names[i]);
    }
    free(names);
    free(buono);
    free(t);

    return NULL;
}

void *ep_tfunc(void *arg) {

    struct ei_tdata *t = (struct ei_tdata *)arg;
    struct shared *sh = t->sh;

    // Apre il file
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[EI] fopen");
    printf("[EP] opened %s\n", t->path);

    size_t lsiz = getnumoflines(fp) + 1;
    printf("[EP] lsiz: %zu\n", lsiz);
    
    char **regalos = (char **)malloc(sizeof(char*) * lsiz);
    assert(regalos);
    for (size_t i = 0; i < lsiz; i++) {
        regalos[i] = (char *)malloc(sizeof(char) * SSIZ);
        assert(regalos[i]);
    }
    long *costos = (long *)malloc(sizeof(long) * lsiz);
    assert(costos);

    char buffer[SSIZ];
    size_t count = 0;
    while (fgets(buffer, SSIZ, fp)) {

        size_t tokpos = strcspn(buffer, ";");
  
        buffer[tokpos] = '\0';
        char *costo = buffer + tokpos + 1;

        strncpy(regalos[count], buffer, SSIZ);

        char *endptr;
        errno = 0;
        costos[count] = strtol(costo, &endptr, 10);

        if (errno != 0) exit_with_sys_err("strtol");

        if (endptr == costo) exit_with_err_msg("[EP] no digits were found\n");

        count++;
    }
    if (ferror(fp)) exit_with_sys_err("fgets");

    //for (size_t i = 0; i < count; i++) printf("%s %ld\n", regalos[i], costos[i]);

    while (true) {
        swait(&sh->ep);
        if (sh->nprod == 0) break;

        ssize_t murmari = getstrpos(regalos, lsiz, sh->regalo);
        if (murmari == -1) 
            exit_with_err_msg("[EP] %s not found??\n", sh->nome);

        size_t pos = (size_t)murmari;

        printf("[EP] creo il regalo '%s' per il bambino '%s' al costo di %ld €'\n",
            sh->regalo, sh->nome, costos[pos]
        );

        sh->costo = costos[pos];

        spost(&sh->ec);
    }

    fclose(fp);
    for (size_t i = 0; i < lsiz; i++) {
        free(regalos[i]);
    }
    free(regalos);
    free(costos);
    free(t);
    return NULL;
}

// Elfo Smistatore
struct es_tdata {
    int tag;
    char *path;

    struct shared *sh;
};
void *es_tfunc(void *arg) {

    struct es_tdata *t = (struct es_tdata *)arg;
    struct shared *sh = t->sh;

    // Apre il file
    printf("[ES%d] leggo le letterine dal file '%s'\n", t->tag, t->path);
    FILE *fp = fopen(t->path, "r");
    if (!fp) exit_with_sys_err("[ES] fopen");

    char buffer[SSIZ];
    while (fgets(buffer, SSIZ, fp)) {

        swait(&sh->es);

        size_t tokpos = strcspn(buffer, ";");
        buffer[tokpos] = '\0';
        char *regalo = buffer + tokpos + 1;
        regalo[strcspn(regalo, "\n")] = '\0';

        printf("[ES%d] il bambino '%s' per Natale desidera '%s'\n", t->tag, buffer, regalo);
        strncpy(sh->nome, buffer, SSIZ);
        strncpy(sh->regalo, regalo, SSIZ);
        
        sh->es_flag = true;
        spost(&sh->bn);
    }
    if (ferror(fp)) exit_with_sys_err("[ES] fgets");
    swait(&sh->es);
    sh->es_flag = false;
    sh->nprod--;
    spost(&sh->bn);

    printf("[ES%d] non ho più letterine da consegnare\n", t->tag);

    fclose(fp);
    free(t);
    return NULL;
}

void *ec_tfunc(void *arg) {
    struct shared *sh = (struct shared *)arg;

    size_t letterec = 0;
    size_t buonic = 0;
    size_t tintic = 0;
    long costotot = 0;

    while (true) {
        swait(&sh->ec);
        if (sh->nprod == 0) break;

        letterec++;
        if (sh->buono) {
            buonic++;
            costotot += sh->costo;
        } else tintic++;
        sh->es_flag = false;
        spost(&sh->bn);
    }

    printf("[EC] quest'anno abbiamo ricevuto %zu richieste da %zu bambini buoni e da %zu cattivi con un costo totale di produzione di %ld €\n",
        letterec, buonic, tintic, costotot
    );

    // destroy all sems
    // free the shared struct

    return NULL;
}

void *bn_tfunc(void *arg) {

    struct shared *sh = (struct shared *)arg;
    bool work = true;

    while (work) {
        swait(&sh->bn);
        if (!sh->es_flag) {
            if (sh->nprod == 0) {
                printf("[BN] non ci sono più bambini da esaminare\n");
                work = false;
                spost(&sh->ei);
                spost(&sh->ep);
                spost(&sh->ec);
            }
            else spost(&sh->es);
        }
        else {
            // There's some valid things put by ES
            printf("[BN] come si è comportato il bambino '%s'?\n", sh->nome);
            spost(&sh->ei);

            swait(&sh->bn);

            if (sh->buono) {
                printf("[BN] il bambino '%s' riceverà il suo regalo '%s'\n", sh->nome, sh->regalo);
                spost(&sh->ep);
            } else {
                spost(&sh->ec);
            }
        }

    }

    return NULL;
}


int main(int argc, char **argv) {

    if (argc < 4) exit_with_err_msg("Invalid args\n");

    struct shared *sh = (struct shared *)malloc(sizeof(struct shared));
    assert(sh);
    sh->nprod = argc - 3;
    if (sem_init(&sh->es, 0, 1) == -1) exit_with_sys_err("sem_init es");
    if (sem_init(&sh->bn, 0, 0) == -1) exit_with_sys_err("sem_init bn");
    if (sem_init(&sh->ei, 0, 0) == -1) exit_with_sys_err("sem_init ei");
    if (sem_init(&sh->ep, 0, 0) == -1) exit_with_sys_err("sem_init ep");
    if (sem_init(&sh->ec, 0, 0) == -1) exit_with_sys_err("sem_init ec");

    struct ei_tdata *eidata = (struct ei_tdata *)malloc(sizeof(struct ei_tdata));
    assert(eidata);
    eidata->path = argv[2];
    eidata->sh = sh;

    struct ei_tdata *epdata = (struct ei_tdata *)malloc(sizeof(struct ei_tdata));
    assert(epdata);
    epdata->path = argv[1];
    epdata->sh = sh;

    struct es_tdata *esdatas[sh->nprod];
    for (size_t i = 0; i < sh->nprod; i++) {
        esdatas[i] = (struct es_tdata *)malloc(sizeof(struct es_tdata));
        assert(esdatas[i]);
        esdatas[i]->tag = (int)i;
        esdatas[i]->path = argv[i+3];
        esdatas[i]->sh = sh;
    }

    pthread_t pids[4+sh->nprod];

    int err = pthread_create(&pids[0], NULL, bn_tfunc, sh);
    if (err != 0) exit_with_err("pthread_create 0", err);
    err = pthread_detach(pids[0]);
    if (err != 0) exit_with_err("pthread_detach 0", err);

    err = pthread_create(&pids[1], NULL, ei_tfunc, eidata);
    if (err != 0) exit_with_err("pthread_create 1", err);
    err = pthread_detach(pids[1]);
    if (err != 0) exit_with_err("pthread_detach 1", err);

    err = pthread_create(&pids[2], NULL, ep_tfunc, epdata);
    if (err != 0) exit_with_err("pthread_create 2", err);
    err = pthread_detach(pids[2]);
    if (err != 0) exit_with_err("pthread_detach 2", err);

    err = pthread_create(&pids[3], NULL, ec_tfunc, sh);
    if (err != 0) exit_with_err("pthread_create 3", err);
    err = pthread_detach(pids[3]);
    if (err != 0) exit_with_err("pthread_detach 3", err);

    for (size_t i = 0; i < sh->nprod; i++) {
        err = pthread_create(&pids[i+4], NULL, es_tfunc, esdatas[i]);
        if (err != 0) exit_with_err("pthread_create ei", err);
        err = pthread_detach(pids[i+4]);
        if (err != 0) exit_with_err("pthread_detach ei", err);
    }

    pthread_exit(NULL);

}
