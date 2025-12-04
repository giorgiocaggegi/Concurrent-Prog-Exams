#include "../lib-misc.h"
#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define BSIZ 1024
#define ASIZ 10

struct record {
    char buf[BSIZ];
    char *basename;
    size_t tot;
    size_t offset;
    size_t buflen;
    size_t i;
};

struct stack {
    struct record ar[ASIZ];
    size_t count;
    size_t nprod;
    size_t ncons;
    sem_t full;
    sem_t empty;
    sem_t mut;
};

struct stack *get_stack(size_t nprod, size_t ncons) {
    struct stack *ret = (struct stack *)malloc(sizeof(struct stack));
    assert(ret);

    ret->count = 0;
    ret->nprod = nprod;
    ret->ncons = ncons;

    int err = sem_init(&ret->full, 0, 0);
    if (err != 0) exit_with_sys_err("sem_init");
    err = sem_init(&ret->mut, 0, 1);
    if (err != 0) exit_with_sys_err("sem_init");
    err = sem_init(&ret->empty, 0, ASIZ);
    if (err != 0) exit_with_sys_err("sem_init");

    return ret;
}

void stack_insert(struct stack *s, struct record *r) {
    assert(s && r);

    int err = sem_wait(&s->empty);
    if (err != 0) exit_with_sys_err("sem_wait");
    err = sem_wait(&s->mut);
    if (err != 0) exit_with_sys_err("sem_wait");

    memcpy(s->ar+(s->count++), r, sizeof(struct record));

    err = sem_post(&s->mut);
    if (err != 0) exit_with_sys_err("sem_post");
    err = sem_post(&s->full);
    if (err != 0) exit_with_sys_err("sem_post");
}

bool stack_remove(struct stack *s, struct record *r) {
    assert(s && r);
    bool picked = false;

    int err = sem_wait(&s->full);
    if (err != 0) exit_with_sys_err("sem_wait");
    err = sem_wait(&s->mut);
    if (err != 0) exit_with_sys_err("sem_wait");

    if (s->count > 0) {
        memcpy(r, s->ar+(--s->count), sizeof(struct record));
        picked = true;
    }

    err = sem_post(&s->mut);
    if (err != 0) exit_with_sys_err("sem_post");
    if (picked) err = sem_post(&s->empty);
    if (err != 0) exit_with_sys_err("sem_post");

    return picked;
}

void decrease(struct stack *s) {
    bool rusbiggh = false;
    size_t ncons;

    int err = sem_wait(&s->mut); // start
    if (err != 0) exit_with_sys_err("sem_wait");

    if (s->nprod > 0) s->nprod--;
    if (s->nprod == 0) {
        rusbiggh = true;
        ncons = s->ncons;
    }

    err = sem_post(&s->mut);
    if (err != 0) exit_with_sys_err("sem_post"); // end 

    if (rusbiggh) 
        for (size_t i = 0; i < ncons; i++) {
            err = sem_post(&s->full);
            if (err != 0) exit_with_sys_err("sem_post");
        }
}

struct tdata {
    const char *argpath;
    char *path;
    size_t i;
    size_t totlen;
    struct stack *s;
    size_t n;
};

void *reader_tfunc(void *arg) {
    struct tdata *t = (struct tdata *)arg;
    int fd = open(t->argpath, O_RDONLY);
    if (fd == -1) exit_with_sys_err("open");

    printf("[READER-%zu] lettura del file '%s' di %zu byte\n", t->i, t->argpath, t->totlen);

    struct record r = {.basename = t->path, .tot = t->totlen, .offset = 0, .buflen = 0, .i = t->i};
    while (true) {

        ssize_t howmany = read(fd, r.buf, BSIZ);
        if (howmany == -1) exit_with_sys_err("read");
        else {
            r.buflen = howmany;
            printf("[READER-%zu] lettura del blocco di offset %zu di %zu byte\n", 
                t->i, r.offset, howmany
            );

            stack_insert(t->s, &r);

            
            if (howmany < BSIZ) {
                decrease(t->s);
                break;
            } else r.offset += howmany;
        }

    }

    printf("[READER-%zu] terminazione...\n", t->i);
    close(fd);
    free(t);
    return NULL;
}


void *writer_tfunc(void *arg) {
    struct tdata *t = (struct tdata *)arg;
    int fds[t->n];
    for (size_t i = 0; i < t->n; i++) fds[i] = -2;
    size_t dirlen = strlen(t->path);

    bool work = true;
    struct record r;
    while (work) {
        work = stack_remove(t->s, &r);
        if (work) {
            // open the file?
            if (fds[r.i] == -2) {
                size_t baselen = strlen(r.basename);
                char ture[dirlen+baselen+1];
                memcpy(ture, t->path, sizeof(char)*dirlen);
                memcpy(ture+dirlen, r.basename, sizeof(char)*baselen);
                ture[dirlen+baselen] = '\0';
                printf("[WRITER] opening file %s\n", ture);
                fds[r.i] = open(ture, O_CREAT | O_WRONLY, 0666);
                if (fds[r.i] == -1) exit_with_sys_err("open");
            }
            // copying content
            if (lseek(fds[r.i], r.offset, SEEK_SET) == (off_t)(-1)) exit_with_sys_err("lseek");
            ssize_t howmany = write(fds[r.i], r.buf, r.buflen);
            if (howmany < (ssize_t)r.buflen) exit_with_sys_err("write");
            printf("[WRITER] scrittura del blocco di offset %zu di %zu byte sul file '%s'\n",
                r.offset, r.buflen, r.basename
            );
            /*
            if (r.offset + r.buflen == r.tot) {
                printf("[WRITER] scrittura del file '%s' completata\n", r.basename);
                close(fds[r]);
            } else if (r.offset + r.buflen > r.tot) {
                exit_with_err_msg("Danno: %zu, %zu, %zu, %s, %i\n",
                    r.offset, r.buflen, r.tot, r.basename, r.i
                );
            }*/
        }
    }

    for (size_t i = 0; i < t->n; i++) if (close(fds[i]) != 0) exit_with_sys_err("close");
    return NULL;
}


int main(int argc, const char **argv) {
    if (argc < 3) 
        exit_with_err_msg("Uso: %s <file-1> <file-2> ... <file-n> <destination-dir>\n", argv[0]);
    size_t n = (size_t)argc-2;
    printf("[MAIN] duplicazione di %zu file\n", n);

    struct stack *s = get_stack(n, 1);

    pthread_t tids[n+1];
    char *tofree[n+1];
    for (size_t i = 0; i < n; i++) {
        struct stat napoli;
        if (lstat(argv[i+1], &napoli) != 0) exit_with_sys_err("lstat");
        if (!S_ISREG(napoli.st_mode)) exit_with_err_msg("[MAIN] non un file regolare: %s\n", argv[i+1]);

        struct tdata *t = (struct tdata *)malloc(sizeof(struct tdata));
        assert(t);

        tofree[i] = strdup(argv[i+1]);
        t->path = basename(tofree[i]);
        t->argpath = argv[i+1];
        t->i = i;
        t->s = s;
        t->totlen = napoli.st_size;
        
        int err = pthread_create(tids+i, NULL, reader_tfunc, t);
        if (err != 0) exit_with_err("pthread_create", err);
    }

    struct stat napoli;
    if (lstat(argv[argc-1], &napoli) != 0) exit_with_sys_err("lstat");
    if (!S_ISDIR(napoli.st_mode)) exit_with_err_msg("[MAIN] non una cartella: %s\n", argv[argc-1]);
    struct tdata *t = (struct tdata *)malloc(sizeof(struct tdata));
    size_t arglen = strlen(argv[argc-1]);
    if (argv[argc-1][arglen-1] != '/') {
        tofree[n] = (char *)malloc(sizeof(char)*(arglen+2));
        memcpy(tofree[n], argv[argc-1], sizeof(char)*arglen);
        tofree[n][arglen] = '/';
        tofree[n][arglen+1] = '\0';
    } else tofree[n] = strdup(argv[argc-1]);
    t->path = tofree[n];
    t->s = s;
    t->n = n;
    int err = pthread_create(tids+n, NULL, writer_tfunc, t);
    if (err != 0) exit_with_err("pthread_create", err);

    for (size_t i = 0; i < n; i++) {
        int err = pthread_join(*(tids+i), NULL);
        if (err != 0) exit_with_err("pthread_join", err);
    }

    printf("[MAIN] duplicazione dei %zu file completata\n", n);
}
