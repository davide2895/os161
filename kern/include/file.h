/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Definition of the opened files.
 */

//#include <vnode.h> //Added but I'm not sure because vnode.h is not added
					   //but is only added a struct below
#include <types.h>
#include <kern/limits.h>


struct vnode; //for the same discussion above
struct lock;
/*
 * At the moment, the lock variable is not inserted because we need to understand if we
 * have to face with the problems related to the synchronization.
 */

struct openfile {

    struct vnode *f_cwd; /*current working directory*/

    mode_t mode; /*Read, Write, Read-Write ecc...*/

    off_t offset;

    unsigned int reference_count;

    struct lock *lock;   //By using a lock, multiple processes can use the openfile structure one at time, without conflicts.

};

struct filetable {
    struct openfile *op_ptr[__OPEN_MAX];
    struct lock *ft_lock;    //this locks the entire filetable, in case other threads try to operate concurrently.
};

struct filetable *filetable_init(void);
void ft_STD_init(struct filetable *ft);
int file_open(char *filename, int flags, int mode, int *retfd, struct filetable *ft);

int filetable_placefile(struct openfile *of, int *fd, struct filetable *ft);

int findFD ( int fd, struct openfile** of );

int closeOpenFile ( struct openfile *of);
void filetable_destroy(struct filetable * ft);
void filetable_copy(struct filetable * new_ft);





#endif /* _FILE_H_ */
