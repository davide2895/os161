/*
 * Declarations for file handle and file table management.
 * New for SOL2.
 */

#ifndef _FILE_H_
#define _FILE_H_

#include <limits.h>
#include <filetable.h>

struct lock;
struct vnode;

/*** openfile section ***/

/* 
 * openfile struct 
 * note that there's not too much to keep track of, since the vnode does most
 * of that.  note that it does require synchronization, because a single
 * openfile can be shared between processes (filetable inheritance).
 */

/* opens a file (must be kernel pointers in the args) */
int file_open(char *filename, int flags, int mode, int *retfd);

/* closes a file */
int file_close(int fd);


/*** file table section ***/

/*
 * filetable struct
 * just an array of open files.  nice and simple.  doesn't require
 * synchronization, because a table can only be owned by a single process (on
 * inheritance in fork, the table is copied).
 */


/* these all have an implicit arg of the curthread's filetable */
int filetable_init(const char *inpath, const char *outpath, const char *errpath);
int filetable_copy(struct filetable **copy);
int filetable_placefile(openfile *file, int *fd);
int filetable_findfile(int fd, openfile **file);
int filetable_dup2file(int oldfd, int newfd);
void filetable_destroy(struct filetable *ft);

#endif /* _FILE_H_ */