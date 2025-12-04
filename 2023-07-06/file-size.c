#include "../lib-misc.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define ARSIZ 10
#define swait(s) if (sem_wait((s)) != 0) exit_with_sys_err("sem_wait")
#define spost(s) if (sem_post((s)) != 0) exit_with_sys_err("sem_post")

struct record {
    char *path;
    size_t fsize;
};

struct queue {
    struct record ar[ARSIZ];
    size_t count;
    size_t low;
    size_t high;
    
    size_t nprod;
    size_t ncons;
    
    sem_t empty;
    sem_t full;
    sem_t mut;
};

void queue_init(struct queue *s, size_t nprod, size_t ncons) {
    assert(s);
    s->nprod = nprod;
    s->ncons = ncons;
    s->low = s->high = s->count = 0;

    if (sem_init(&s->empty, 0, ARSIZ) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->mut, 0, 1) != 0) exit_with_sys_err("sem_init");
    if (sem_init(&s->full, 0, 0) != 0) exit_with_sys_err("sem_init");
}

void queue_decrease(struct queue *s) {
    assert(s);
    bool sveglia = false;

    swait(&s->mut);
    if (s->nprod > 0) s->nprod--;
    if (s->nprod == 0) sveglia = true;
    spost(&s->mut);

    if (sveglia) for (size_t i = 0; i < s->ncons; i++) spost(&s->full);
}

void queue_insert(struct queue *s, struct record *r) {
    assert(s && r);

    swait(&s->empty);
    swait(&s->mut);

    memcpy(s->ar + s->high, r, sizeof(struct record));
    s->high = (s->high+1)%ARSIZ;
    s->count++;

    spost(&s->mut);
    spost(&s->full);
}

bool queue_remove(struct queue *s, struct record *r) {
    assert(s && r);
    bool picked = false;

    swait(&s->full);
    swait(&s->mut);

    if (s->count > 0) {
        memcpy(r, s->ar + s->low, sizeof(struct record));
        s->low = (s->low+1)%ARSIZ;
        s->count--;
        picked = true;
    }

    spost(&s->mut);
    if (picked) spost(&s->empty);

    return picked;
}

struct dir_tdata {
    struct queue *q;
    char *argpath;
    size_t i;
};
void *dir_tfunc(void *a) {
    struct dir_tdata *t = (struct dir_tdata *)a;
    struct queue *q = t->q;

    char *dirpath = t->argpath;
    size_t dirlen = strlen(t->argpath);
    bool tofree = false;
    if (t->argpath[dirlen-1] != '/') {
        dirpath = (char *)malloc(sizeof(char)*(dirlen+2));
        memcpy(dirpath, t->argpath, dirlen);
        dirlen++;
        dirpath[dirlen-1] = '/';
        dirpath[dirlen] = '\0';
    }

    printf("[D-%zu] scansione della cartella \'%s\'...\n", t->i, dirpath);

    DIR *dp = opendir(dirpath);
    if (!dp) exit_with_sys_err("opendir");
    struct dirent *entp = NULL;
    errno = 0;
    while ((entp = readdir(dp))) {
        if (strcmp(entp->d_name, ".") == 0 || strcmp(entp->d_name, "..") == 0) continue;

        size_t filenamelen = strlen(entp->d_name);
        char *new = (char *)malloc(sizeof(char)*(dirlen+filenamelen+1));
        memcpy(new, dirpath, dirlen);
        memcpy(new+dirlen, entp->d_name, filenamelen);
        new[dirlen+filenamelen] = '\0';

        struct stat napoli;
        if (lstat(new, &napoli) != 0) exit_with_sys_err("lstat");
        if (!S_ISREG(napoli.st_mode)) {
            free(new);
            continue;
        }

        printf("[D-%zu] trovato il file '%s' in '%s'\n", t->i, entp->d_name, dirpath);

        struct record r = {.path = new};
        queue_insert(q, &r);

        errno = 0;
    }
    if (errno) exit_with_sys_err("readdir");

    queue_decrease(q);
    if (closedir(dp) != 0) perror("closedir");
    if (tofree) free(dirpath);

    printf("[D-%zu] terminazione...\n", t->i);
    return NULL;
}

struct stat_tdata {
    struct queue *inter;
    struct queue *final;
};
void *stat_tfunc(void *a) {
    struct stat_tdata *t = (struct stat_tdata *)a;
    struct queue *inter = t->inter;
    struct queue *final = t->final;

    struct record r;
    while (queue_remove(inter, &r)) {
        struct stat napoli;
        if (stat(r.path, &napoli) != 0) exit_with_sys_err("stat");

        r.fsize = napoli.st_size;

        printf("[STAT] il file \'%s\' ha dimensione %zu byte(s)\n", r.path, r.fsize);

        queue_insert(final, &r);
    }

    queue_decrease(final);

    printf("[STAT] terminazione...\n");
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) exit_with_err_msg("USO: \'%s\' <dir-1> <dir-2> ... <dir-n>\n", argv[0]);

    int err;
    pthread_t tid;
    size_t n = (size_t)argc-1;
    struct queue inter;
    struct queue final;
    struct dir_tdata dirtds[n];
    struct stat_tdata stattd;

    queue_init(&inter, n, 1);
    queue_init(&final, 1, 1);

    for (size_t i = 0; i < n; i++) {
        dirtds[i].argpath = argv[i+1];
        dirtds[i].i = i;
        dirtds[i].q = &inter;

        err = pthread_create(&tid, NULL, dir_tfunc, dirtds+i);
        if (err) exit_with_err("pthread_create", err);
        err = pthread_detach(tid);
        if (err) exit_with_err("pthread_detach", err);
    }

    stattd.inter = &inter;
    stattd.final = &final;
    err = pthread_create(&tid, NULL, stat_tfunc, &stattd);
    if (err) exit_with_err("pthread_create", err);
    err = pthread_detach(tid);
    if (err) exit_with_err("pthread_detach", err);

    size_t total = 0;
    struct record r;
    while (queue_remove(&final, &r)) {
        total += r.fsize;
        printf("[MAIN] con il file \'%s\' il totale parziale è di %zu byte(s)\n", r.path, total);
    }

    printf("[MAIN] il totale finale è di %zu byte(s)\n", total);
    pthread_exit(NULL);

}
