#include "safe-queue.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

struct dir_tdata {
    struct queue *p; // pathnames
    
    char *dirpath;
    size_t tag;
};
void *dir_tfunc(void *arg) {
    struct dir_tdata *t = (struct dir_tdata *)arg;
    struct queue *p = t->p;
    char *dirpath;

    size_t allocations = 0;

    size_t dirpath_len = strlen(t->dirpath);
    bool alloc = false;
    if (t->dirpath[dirpath_len-1] != '/') {
        alloc = true;
        dirpath = (char *)malloc((dirpath_len+2)*sizeof(char));
        assert(dirpath);
        memcpy(dirpath, t->dirpath, dirpath_len);
        dirpath_len++;
        dirpath[dirpath_len-1] = '/';
        dirpath[dirpath_len] = '\0';
    } else {
        dirpath = t->dirpath;
    }

    printf("[DIR-%zu] inizio scansione della cartella '%s'...\n", t->tag, dirpath);
    DIR *dp = opendir(dirpath);
    if (!dp) exit_with_sys_err("opendir");

    struct dirent *next = NULL;
    errno = 0;
    while (next = readdir(dp)) {
        
        if ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0)) continue;

        size_t d_name_len = strlen(next->d_name);

        char *new = (char *)malloc((d_name_len+dirpath_len+1)*sizeof(char));
        allocations++;
        new[d_name_len+dirpath_len] = '\0';
        memcpy(new, dirpath, dirpath_len);
        memcpy(new+dirpath_len, next->d_name, d_name_len);

        struct stat napoli;
        if (stat(new, &napoli) != 0) exit_with_sys_err("dirent");
        if (!S_ISREG(napoli.st_mode)) {printf("%s\n",new);free(new);continue;}

        queue_insert_str(p, new);

        errno = 0;
    }
    if (errno != 0) exit_with_sys_err("readdir");

    queue_decrease(p);
    printf("[DIR-%zu] fine scansione della cartella '%s'. %zu all\n", t->tag, dirpath, allocations);
    if (alloc) {free(dirpath);}
    if (closedir(dp) != 0) exit_with_sys_err("closedir");
    free(arg);
    return NULL;
}

size_t countf(char *word, FILE *fp) {
    assert(word && fp);
    size_t ret = 0;
    size_t word_len = strlen(word);
    size_t comp = 0;
    short status = 0;

    int nuccio;
    while (true) {
        nuccio = fgetc(fp);
        if (nuccio == EOF) break;
        if (isspace(nuccio)) {
            if (status == 2) ret++;
            comp = status = 0;
            continue;
        }
        if (status != 1) {
            if (tolower(nuccio) == tolower(word[comp])) {
                if (++comp == word_len) {
                    status = 2;
                } else {
                    comp++;
                }
            } else {
                status = 1;
            }
        }
        //printf("%c %d\n", (char)nuccio, status);
    }
    if (ferror(fp)) exit_with_sys_err("fgetc");

    return ret;
}

struct search_tdata {
    struct queue *p; // pathnames
    struct queue *e; // entries (pathnames + entries)

    char *word;
};
void *search_tfunc(void *arg) {
    struct search_tdata *t = (struct search_tdata *)arg;
    struct queue *p = t->p;
    struct queue *e = t->e;
    size_t allocations = 0;

    printf("[SEARCH] parola da cercare: '%s'\n", t->word);

    char *curpath = NULL;
    while ((curpath = queue_remove_str(p))) {
        FILE *fp = fopen(curpath, "r");
        if (!fp) exit_with_sys_err("fopen");

        size_t occ = countf(t->word, fp);
        if (occ > 0) {
            printf("[SEARCH] il file '%s' contiene %zu occorrenze\n", curpath, occ);
            queue_insert(e, curpath, occ);
        } else {
            printf("[SEARCH] il file '%s' non conteneva occorrenze\n", curpath);
            free(curpath);
        }
        allocations++;
            
        if (fclose(fp) != 0) exit_with_sys_err("fclose");
    }

    queue_decrease(e);
    free(arg);
    printf("[SEARCH] esco. %zu all\n", allocations);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) exit_with_err_msg("Invalid args\n");
    int err;
    size_t nprod = (size_t)argc-2;
    
    for (size_t i = 2; i < nprod; i++) {
        struct stat napoli;
        if (stat(argv[i], &napoli) == -1) exit_with_sys_err("stat");
        if (!S_ISDIR(napoli.st_mode)) exit_with_err_msg("%s not a directory\n", argv[i]);
    }

    struct queue *p = (struct queue *)malloc(sizeof(struct queue));
    struct queue *e = (struct queue *)malloc(sizeof(struct queue));
    assert(p && e);
    queue_init(p, nprod, 1);
    queue_init(e, 1, 1);

    struct search_tdata *sdata = (struct search_tdata *)malloc(sizeof(struct search_tdata));
    assert(sdata);
    sdata->p = p;
    sdata->e = e;
    sdata->word = argv[1];

    struct dir_tdata *ddatas[nprod];
    assert(ddatas);
    for (size_t i = 0; i < nprod; i++) {
        ddatas[i] = (struct dir_tdata *)malloc(sizeof(struct dir_tdata));
        ddatas[i]->p = p;
        ddatas[i]->dirpath = argv[i+2];
        ddatas[i]->tag = i+1;
        
        pthread_t pid;
        err = pthread_create(&pid, NULL, dir_tfunc, ddatas[i]);
        if (err) exit_with_err("pthread_create dir", err);
        err = pthread_detach(pid);
        if (err) exit_with_err("pthread_detach dir", err);
    }

    pthread_t pid;
    err = pthread_create(&pid, NULL, search_tfunc, sdata);
    if (err) exit_with_err("pthread_create search", err);
    err = pthread_detach(pid);
    if (err) exit_with_err("pthread_detach search", err);

    size_t glob = 0;
    struct entry cur;
    while ((cur = queue_remove(e)).path) {
        printf("[MAIN] con il file '%s' il totale parziale è di %zu occorrenze\n", cur.path, cur.occ);
        glob += cur.occ;
        free(cur.path);
    }

    printf("[MAIN] il totale finale è di %zu occorrenze\n", glob);
}