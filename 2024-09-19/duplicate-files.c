#include "safe-stack.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

struct reader_tdata {
    size_t tag;
    char *path;
    struct stack *s;
};
void *reader_tfunc(void *arg) {
    struct reader_tdata *t = (struct reader_tdata *)arg;
    struct stack *s = t->s;
    char *file;

    int fd = open(t->path, O_RDONLY);
    if (fd == -1) exit_with_sys_err("open");

    struct stat napoli;
    if (fstat(fd, &napoli) != 0) exit_with_sys_err("fstat");
    if (!S_ISREG(napoli.st_mode)) 
        exit_with_err_msg("[READER-%zu] Opened not a regular file: %s\n", t->tag, t->path);

    file = (char *)mmap(NULL, napoli.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if ((void *)file == MAP_FAILED) exit_with_sys_err("mmap");

    if (close(fd) != 0) exit_with_sys_err("close");

    printf("[READER-%zu] mappato il file '%s' di %zu byte\n", t->tag, t->path, napoli.st_size);

    struct record rec;
    rec.filename = t->path;
    rec.totsiz = napoli.st_size;
    rec.curpos = 0;
    rec.bcount = 0;
    rec.tag = t->tag;
    rec.eow = false;


    while (!rec.eow) {

        if (rec.curpos+BSIZ < rec.totsiz) {
            rec.bcount = BSIZ;
        } else {
            rec.bcount = rec.totsiz-rec.curpos;
            rec.eow = true;
        }

        printf("[READER-%zu] lettura di %zu byte con posizione corrente %zu\n",
            rec.tag, rec.bcount, rec.curpos
        );

        memcpy(rec.block, file+rec.curpos, rec.bcount);

        push(s, &rec);
        rec.curpos += rec.bcount;
    }

    printf("[READER-%zu] lettura completata\n", t->tag);
    return NULL;
}

struct writer_tdata {
    size_t nprod;
    char *dirpath;
    struct stack *s;
};
void *writer_tfunc(void *arg) {
    struct writer_tdata *t = (struct writer_tdata *)arg;
    char *dirpath = t->dirpath;
    size_t dp_len = strlen(t->dirpath);
    bool alloc;
    struct stack *s = t->s;
    char *files[t->nprod];
    for (size_t i = 0; i < t->nprod; i++) files[i] = NULL;
    size_t nprod = t->nprod;

    struct stat napoli;
    if (stat(dirpath, &napoli) != 0) {
        if (errno == ENOENT) {
            if (mkdir(dirpath, 0777) != 0) exit_with_sys_err("mkdir");
        } else 
            exit_with_sys_err("stat");
    } else {
        if (!S_ISDIR(napoli.st_mode)) 
            exit_with_err_msg("%s not a directory\n", dirpath);
    }

    if (dirpath[dp_len-1] != '/') {
        dirpath = (char *)malloc(sizeof(char)*(dp_len+2));
        memcpy(dirpath, t->dirpath, dp_len);
        dirpath[dp_len] = '/';
        dirpath[dp_len+1] = '\0';
        dp_len++;
        alloc = true;
    }
        
    struct record fresh;
    fresh.filename = 1;
    while (true) {
        pop(s, &fresh);

        if (!fresh.filename) break;

        if (!files[fresh.tag]) {
            char fname[strlen(fresh.filename)+1];
            memcpy(fname, fresh.filename, strlen(fresh.filename)+1);
            char *bname = basename(fname);
            size_t bn_len = strlen(bname);
            char compath[dp_len+bn_len+1];
            memcpy(compath, dirpath, dp_len);
            memcpy(compath+dp_len, bname, bn_len);
            compath[dp_len+bn_len] = '\0';

            printf("[WRITER] creazione del file '%s' di dimensione %zu byte(s)\n", bname, fresh.totsiz);
            int fd = open(compath, O_RDWR | O_CREAT | O_TRUNC, 0777);
            if (fd == -1) exit_with_sys_err("open 2");
            
            if (ftruncate(fd, fresh.totsiz) == -1) exit_with_sys_err("ftruncate");

            files[fresh.tag] = (char *)mmap(
                NULL, fresh.totsiz, PROT_WRITE, MAP_SHARED, fd, 0
            );
            if ((void *)files[fresh.tag] == MAP_FAILED) 
                exit_with_sys_err("mmap 2");

            if (close(fd) != 0) exit_with_sys_err("close");
        }

        printf("[WRITER] scrittura di %zu byte(s) in posizione %zu del file '%s', nella sua nuova copia\n",
            fresh.bcount, fresh.curpos, fresh.filename
        );
        memcpy((files[fresh.tag])+fresh.curpos, fresh.block, fresh.bcount);
        //printf("%zu\n", fresh.curpos+fresh.bcount);
        //(files[fresh.tag])[5260] = 'a';

        if (fresh.eow) {
            nprod--;
            printf("[WRITER] scrittura del file '%s' completata\n", fresh.filename);
        }
    }

    if (alloc) free(dirpath);
    // unmap all
    return NULL;
}

int main(int argc, char **argv) {

    if (argc < 3)  exit_with_err_msg("Invalid args\n");

    int err;
    size_t nprod = (size_t)argc-2;
    struct reader_tdata rtds[nprod];

    printf("[MAIN] duplicazione di %zu file\n", nprod);

    struct stack s;
    stack_init(&s, nprod);

    for (size_t i = 0; i < nprod; i++) {
        rtds[i].tag = i;
        rtds[i].path = argv[i+1];
        rtds[i].s = &s;

        pthread_t pid;
        err = pthread_create(&pid, NULL, reader_tfunc, &rtds[i]);
        if (err != 0) exit_with_err("pthread_create readers", err);
        err = pthread_detach(pid);
        if (err != 0) exit_with_err("pthread_detach readers", err);
    }

    pthread_t pid;
    struct writer_tdata wtd = {nprod, argv[argc-1], &s};
    err = pthread_create(&pid, NULL, writer_tfunc, &wtd);
    err = pthread_join(pid, NULL);
    if (err != 0) exit_with_err("pthread_join", err);

    printf("[MAIN] duplicazione dei %zu file completata\n", nprod);
}