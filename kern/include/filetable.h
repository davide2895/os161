#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <lib.h>
#include <array.h>
#include <synch.h>
#include <types.h>
#include <vnode.h>

#define MAX_FILES 50

typedef struct
{
        struct vnode *vn;
        unsigned int offset;
        mode_t mode;
        unsigned int refs;
        struct lock *lock;
} openfile;

struct filetable
{
        openfile *handles[MAX_FILES];
        unsigned int last;
        struct lock *lock;
};

/* Creates new file table.
   Returns file table, or NULL on error. */
struct filetable * ft_create (void);

/* Attach file table to STDIN, STDOUT, and STDERR.
   Returns 0 on success, or -1 on error. */
int ft_init (struct filetable *ft);

/* Adds file to file table.
   Returns the index of where the file was added, or -1 on error. */
int ft_add (struct filetable *ft, openfile *file, int init, int *err);

/* Removes file at index.
   Returns 1 if removed from tail, 0 from elsewhere, or -1 on error. */
int ft_remove (struct filetable *ft, unsigned int fd);

/* Returns file at index, or NULL on error. */
openfile * ft_get (struct filetable *ft, unsigned int fd);

/* Sets file at index to file.
   Returns 0 on success, or -1 on error. */
int ft_set (struct filetable *ft, unsigned int fd, openfile *file);

/* Copies file table.
   Returns 1, or -1 on error. */
int ft_copy (struct filetable *copyfrom, struct filetable *copyto);

/* Removes and frees memory for file table.
   Returns 0 on success, or -1 on error. */
int ft_destroy (struct filetable *ft);

#endif