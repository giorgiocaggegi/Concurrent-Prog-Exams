#include "../lib-misc.h"
#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

// Initial dynamic array size
#define INITIALSIZ 10
// Added threads required by assignment
#define NADD 2


// Safe cond/mutex operation wrappers
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
#define signalc(c) \
    do { \
        int err_macro = pthread_cond_signal(&(c)); \
        if (err_macro) exit_with_err("pthread_cond_signal", err_macro); \
    } while (0)
#define broadc(c) \
    do { \
        int err_macro = pthread_cond_broadcast(&(c)); \
        if (err_macro) exit_with_err("pthread_cond_broadcast", err_macro); \
    } while (0)
#define waitc(c, m) \
    do { \
        int err_macro = pthread_cond_wait(&(c), &(m)); \
        if (err_macro) exit_with_err("pthread_cond_signal", err_macro); \
    } while (0)


// Thread-safe dynamic array
struct number_set {
    size_t *ar; // array of size_t
    size_t arsiz; // its current size

    size_t count; // the index of the next free position

    size_t nprod; // number of active producers

    pthread_mutex_t mut;
    pthread_cond_t add;
};

// Function which initializes the structure passed as pointer
void ns_init(struct number_set *s, size_t nprod) {
    assert(s);

    s->ar = (size_t *)malloc(sizeof(size_t)*INITIALSIZ);
    assert(s->ar);
    s->nprod = nprod;
    s->count = 0;
    s->arsiz = INITIALSIZ;

    int err; 
    if ((err = pthread_mutex_init(&s->mut, NULL))) 
        exit_with_err("pthread_mutex_init", err);
    if ((err = pthread_cond_init(&s->add, NULL))) 
        exit_with_err("pthread_cond_init", err);
}

// Function called by a producer everytime
// it ended its work. The last who calls it
// will wake up every consumer which then will
// be aware of the fact that no more items will
// be inserted in the structure
void ns_decrease_nprod(struct number_set *s) {
    lockm(s->mut);

    if (s->nprod > 0) s->nprod--;
    if (s->nprod == 0) broadc(s->add);

    unlockm(s->mut);
}

size_t ns_insert(struct number_set *s, size_t new) {
    assert(s);
    size_t ret = 0;

    // the array will grow in size if it's full
    lockm(s->mut);
    if (s->count == s->arsiz) {
        s->ar = (size_t *)realloc(s->ar, s->arsiz*sizeof(size_t)*2);
        if (!s->ar) exit_with_sys_err("reallocarray");
        s->arsiz = s->arsiz*2;
    }
    s->ar[s->count++] = new;
    ret = s->count;
    
    // Since POSIX follow Mesa conds semantics,
    // threads sleeping on 'add' will be woken up
    // only at the unlocking of the mutex.
    // If there are many items in the structure, 
    // if for instance many DIR thread acquired the mutex,
    // it does make sense to wake them all.
    // (two in case of exam assignment)
    // If more efficience is required, a chain of wakes with
    // single signals could be chosen
    if (s->count >= 2) broadc(s->add);
    unlockm(s->mut);

    return ret; // current first free position will be returned
}

struct dir_tdata {
    size_t i;
    char *path; 
    struct number_set *s;
};
void *dir_tfunc(void *a) {
    struct dir_tdata *t = (struct dir_tdata *)a;
    char *dirpath = t->path;
    size_t dirlen = strlen(t->path);
    bool tofree = false;
    // I verify if t->path already ends with '/'.
    // If not, I dynallocate a new path string
    // and check a flag that will let the thread
    // free it if needed 
    if (t->path[dirlen-1] != '/') {
        dirpath = (char *)malloc(dirlen+2);
        memcpy(dirpath, t->path, dirlen);
        dirpath[dirlen] = '/';
        dirpath[dirlen+1] = '\0';
        dirlen++;
        tofree = true;
    }

    printf("[DIR-%zu] scansione della cartella \'%s\'\n", t->i, dirpath);

    DIR *dp = opendir(dirpath);
    // Error handling and exiting, after showing
    // the latest dir that failed by locking both 
    // streams preventing other threads to interlace info 
    if (!dp) {
        flockfile(stdout);
        flockfile(stderr);
        printf("\'%s\': ", dirpath);
        exit_with_sys_err("opendir");
    }

    struct dirent *dent = NULL;
    errno = 0; // readdir error handling
    // Scanning each element of directory
    while ((dent = readdir(dp))) {
        // Excluding '.' and '..' directory links
        if ((strcmp(dent->d_name,".")+strcmp(dent->d_name,".."))==0) {
            errno = 0;
            continue;
        }

        // Composing filepath
        size_t filenamelen = strlen(dent->d_name);
        char filepath[dirlen+filenamelen+1];
        memcpy(filepath, dirpath, dirlen);
        memcpy(filepath+dirlen, dent->d_name, filenamelen);
        filepath[dirlen+filenamelen] = '\0';

        // Examining file stats. Excluding it if it's not
        // a regular file or if the size is negative
        // for corruption or system reasons
        // (I'm going to insert them in unsigned size_t)
        struct stat napoli;
        if (lstat(filepath, &napoli) != 0) exit_with_sys_err("lstat");
        if (!S_ISREG(napoli.st_mode) || napoli.st_size < 0) {
            errno = 0;
            continue;
        }

        printf("[DIR-%zu] trovato il file \'%s\' di %zu byte\n",
            t->i, dent->d_name, (size_t)napoli.st_size
        );
        
        // Insert the size into the dynamic array
        size_t now = ns_insert(t->s, (size_t)napoli.st_size);

        printf("[DIR-%zu] inserito \'%s\'; l'insieme ha adesso %zu elementi(o)\n",
            t->i, dent->d_name, now
        );

        errno = 0;
    }
    if (errno) exit_with_sys_err("readdir");

    ns_decrease_nprod(t->s); // decreasing nprod if DIR has ended its work

    if (closedir(dp) != 0) perror("closedir");

    if (tofree) free(dirpath);
    printf("[DIR-%zu] terminazione...\n", t->i);
    return NULL;
}

bool ns_remove(struct number_set *s, size_t i) {
    assert(s);
    bool removed = false;

    // Acquiring the mutex...
    lockm(s->mut);
    while (s->count <= 1 && s->nprod > 0) {
        // ... releasing it and sleeping on 'add'
        //     if there aren't items and there are active producers
        waitc(s->add, s->mut); 
    }

    // Joining min and max elements and reinserting it
    // into the structure
    if (s->count >= 2) {
        size_t max_i = 0;
        size_t min_i = 0;

        for (size_t i = 0; i < s->count; i++) {
            if (s->ar[i] < s->ar[min_i]) min_i = i;
            if (s->ar[i] > s->ar[max_i]) max_i = i;
        }

        // The new item will be at the lower position
        // [i0, i1, max, i2, min, i3, i4]
        size_t new_i = (min_i < max_i) ? min_i : max_i;
        size_t start_i = (min_i >= max_i) ? min_i : max_i;
        size_t min = s->ar[min_i];
        size_t max = s->ar[max_i];
        s->ar[new_i] = s->ar[min_i] + s->ar[max_i];
        // -> [i0, i1, max+min, i2, min, i3, i4]

        // Shifting the array
        memmove(&s->ar[start_i], &s->ar[start_i + 1], (s->count - start_i - 1) * sizeof(size_t));
        // -> [i0, i1, max+min, i2, i3, i4]

        s->count--;
        // I will wake other producers.
        // I don't have to wait for DIR threads.
        // A signal could be enough: it will let the program more
        // efficient by avoiding unnecessary wakeups. But special care
        // in designing wakeups chain is required
        if (s->count >= 2) broadc(s->add);

        printf("[ADD-%zu] min (%zu) e max (%zu) -> %zu; l'insieme ha adesso %zu elementi\n",
            i, min, max, s->ar[new_i], s->count
        );

        removed = true;
    }

    unlockm(s->mut); // Releasing the mutex

    return removed;
}

struct add_tdata {
    size_t i;
    struct number_set *s;
};
// Simple ADD threads function
void *add_tfunc(void *a) {
    struct add_tdata *t = (struct add_tdata *)a;

    while (ns_remove(t->s, t->i));

    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) exit_with_err_msg("USO: \'%s\' <dir-1> <dir-2> ... <dir-n>\n", argv[0]);

    int err;
    size_t n = (size_t)argc-1; // number of directories
    struct dir_tdata dts[n]; // structures to be passed to DIR threads 
    struct add_tdata ats[NADD]; // structures to be passed to ADD threads 
    pthread_t tids[n+NADD];

    struct number_set s; // shared structure
    ns_init(&s, n);

    // DIR thread creation
    for (size_t i = 0; i < n; i++) {
        dts[i].i = i;
        dts[i].path = argv[i+1];
        dts[i].s = &s;
        
        if ((err = pthread_create(tids+i, NULL, dir_tfunc, dts+i)))
            exit_with_err("pthread_create dir", err);
    }

    // ADD threads creation
    for (size_t i = 0; i < NADD; i++) {
        ats[i].i = i;
        ats[i].s = &s;
        
        if ((err = pthread_create(tids+i+n, NULL, add_tfunc, ats+i)))
            exit_with_err("pthread_create dir", err);
    }

    // Joining the threads
    for (size_t i = 0; i < NADD+n; i++) {
        if ((err = pthread_join(tids[i], NULL)))
            exit_with_err("pthread_join", err);
    }

    // All threads terminated if the code below is executed

    if (s.count != 1) 
        printf("[MAIN] anomalia\n");
    else {
        printf("[MAIN] i thread secondari hanno terminato e il totale finale è di %zu byte\n", s.ar[0]);
        free(s.ar);
    }
}
