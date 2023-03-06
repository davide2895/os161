/*
 * File handles and file tables.
 * New for SOL2.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <synch.h>
#include <uio.h>
#include <filetable.h>
#include <proc.h>
#include <current.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>

/*** openfile functions ***/

/*
 * file_open
 * opens a file, places it in the filetable, sets RETFD to the file
 * descriptor. the pointer arguments must be kernel pointers.
 * NOTE -- the passed in filename must be a mutable string.
 */
int
file_open(char *filename, int flags, int mode, int *retfd)
{
	struct vnode *vn;
	openfile *file;
	int result;
	
	result = vfs_open(filename, flags, mode, &vn);
	if (result) {
		return result;
	}

	file = kmalloc(sizeof(openfile));
	if (file == NULL) {
		vfs_close(vn);
		return ENOMEM;
	}

	/* initialize the file struct */
	file->lock = lock_create("file lock");
	if (file->lock == NULL) {
		vfs_close(vn);
		kfree(file);
		return ENOMEM;
	}
	file->vn = vn;
	file->offset = 0;
	file->mode = flags & O_ACCMODE;
	file->refs = 1;

	/* vfs_open checks for invalid access modes */
	KASSERT(file->mode==O_RDONLY ||
	        file->mode==O_WRONLY ||
	        file->mode==O_RDWR);

	/* place the file in the filetable, getting the file descriptor */
	result = filetable_placefile(file, retfd);
	if (result) {
		lock_destroy(file->lock);
		kfree(file);
		vfs_close(vn);
		return result;
	}

	return 0;
}

/*
 * file_doclose
 * shared code for file_close and filetable_destroy
 */
static
int
file_doclose(openfile *file)
{
	lock_acquire(file->lock);

	/* if this is the last close of this file, free it up */
	if (file->refs == 1) {
		vfs_close(file->vn);
		lock_release(file->lock);
		lock_destroy(file->lock);
		kfree(file);
	}
	else {
		KASSERT(file->refs > 1);
		file->refs--;
		lock_release(file->lock);
	}

	return 0;
}

/* 
 * file_close
 * knock off the refcount, freeing the memory if it goes to 0.
 */
int
file_close(int fd)
{
	openfile *file;
	int result;

	/* find the file in the filetable */
	result = filetable_findfile(fd, &file);
	if (result) {
		return result;
	}

	result = file_doclose(file);
	if (result) {
		/* leave file open for possible retry */
		return result;
	}
	curproc->files->handles[fd] = NULL;

	return 0;
}

/*** filetable functions ***/

/* 
 * filetable_init
 * pretty straightforward -- allocate the space, initialize to NULL.
 * note that the one careful thing is to open the std i/o in order to
 * get
 * stdin  == 0
 * stdout == 1
 * stderr == 2
 */
int
filetable_init(const char *inpath, const char *outpath, const char *errpath)
{
	/* the filenames come from the kernel; assume reasonable length */
	char path[32];
	int result;
	int fd;

	/* make sure we can fit these */
	KASSERT(strlen(inpath) < sizeof(path));
	KASSERT(strlen(outpath) < sizeof(path));
	KASSERT(strlen(errpath) < sizeof(path));
	
	/* catch memory leaks, repeated calls */
	KASSERT(curproc->files == NULL);

	curproc->files = kmalloc(sizeof(struct filetable));
	if (curproc->files == NULL) {
		return ENOMEM;
	}
	
	/* NULL-out the table */
	for (fd = 0; fd < OPEN_MAX; fd++) {
		curproc->files->handles[fd] = NULL;
	}

	/*
	 * open the std fds.  note that the names must be copied into
	 * the path buffer so that they're mutable.
	 */
	strcpy(path, inpath);
	result = file_open(path, O_RDONLY, 0, &fd);
	if (result) {
		return result;
	}

	strcpy(path, outpath);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	strcpy(path, errpath);
	result = file_open(path, O_WRONLY, 0, &fd);
	if (result) {
		return result;
	}

	return 0;
}

/*
 * filetable_copy
 * again, pretty straightforward.  the subtle business here is that instead of
 * copying the openfile structure, we just increment the refcount.  this means
 * that openfile structs will, in fact, be shared between processes, as in
 * Unix.
 */
int
filetable_copy(struct filetable **copy)
{
	struct filetable *ft = curproc->files;
	int fd;

	/* waste of a call, really */
	if (ft == NULL) {
		*copy = NULL;
		return 0;
	}
	
	*copy = kmalloc(sizeof(struct filetable));
	
	if (*copy == NULL) {
		return ENOMEM;
	}

	/* copy over the entries */
	for (fd = 0; fd < OPEN_MAX; fd++) {
		if (ft->handles[fd] != NULL) {
			lock_acquire(ft->handles[fd]->lock);
			ft->handles[fd]->refs++;
			lock_release(ft->handles[fd]->lock);
			(*copy)->handles[fd] = ft->handles[fd];
		} 
		else {
			(*copy)->handles[fd] = NULL;
		}
	}

	return 0;
}

/*
 * filetable_destroy
 * closes the files in the file table, frees the table.
 */
void
filetable_destroy(struct filetable *ft)
{
	int fd, result;

	KASSERT(ft != NULL);

	for (fd = 0; fd < OPEN_MAX; fd++) {
		if (ft->handles[fd]) {
			result = file_doclose(ft->handles[fd]);
			KASSERT(result==0);
		}
	}
	
	kfree(ft);
}	

/* 
 * filetable_placefile
 * finds the smallest available file descriptor, places the file at the point,
 * sets FD to it.
 */
int
filetable_placefile(openfile *file, int *fd)
{
	struct filetable *ft = curproc->files;
	int i;
	
	for (i = 0; i < OPEN_MAX; i++) {
		if (ft->handles[i] == NULL) {
			ft->handles[i] = file;
			*fd = i;
			return 0;
		}
	}

	return EMFILE;
}

/*
 * filetable_findfile
 * verifies that the file descriptor is valid and actually references an
 * open file, setting the FILE to the file at that index if it's there.
 */
int
filetable_findfile(int fd, openfile **file)
{
	struct filetable *ft = curproc->files;

	if (fd < 0 || fd >= OPEN_MAX) {
		return EBADF;
	}
	
	*file = ft->handles[fd];
	if (*file == NULL) {
		return EBADF;
	}

	return 0;
}

/*
 * filetable_dup2file
 * verifies that both file descriptors are valid, and that the OLDFD is
 * actually an open file.  then, if the NEWFD is open, it closes it.
 * finally, it sets the filetable entry at newfd, and ups its refcount.
 */
int
filetable_dup2file(int oldfd, int newfd)
{
	struct filetable *ft = curproc->files;
	openfile *file;
	int result;

	if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
		return EBADF;
	}

	file = ft->handles[oldfd];
	if (file == NULL) {
		return EBADF;
	}

	/* dup2'ing an fd to itself automatically succeeds (BSD semantics) */
	if (oldfd == newfd) {
		return 0;
	}

	/* closes the newfd if it's open */
	if (ft->handles[newfd] != NULL) {
		result = file_close(newfd);
		if (result) {
			return result;
		}
	}

	/* up the refcount */
	lock_acquire(file->lock);
	file->refs++;
	lock_release(file->lock);

	/* doesn't need to be synchronized because it's just changing the ft */
	ft->handles[newfd] = file;

	return 0;
}
