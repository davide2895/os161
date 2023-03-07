#include <syscall.h>
#include <types.h>
#include <vnode.h>
#include <vfs.h>
#include <proc.h>
#include <copyinout.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>    //contains limits for strings, etc.
#include <uio.h>
#include <kern/iovec.h>
#include <file.h>
#include <current.h>
#include <synch.h>
#include <stat.h>

struct filetable *filetable_init(void){
	struct filetable *ft;

    /*  Makes shure that the process isn't pointing to a filetable already */
    //KASSERT(ft == NULL);	//not sure of keeping this


    ft = kmalloc(sizeof(struct filetable));
	KASSERT(ft != NULL );	//This function returns a filetable type, it cannot return integers such as errors

	/* filetable lock initialization and entries cleaning */
	ft->ft_lock = lock_create("ft_lock");
	if (ft->ft_lock == NULL){
		kfree(ft);
	}
	KASSERT(ft->ft_lock != NULL );

    for (int fd = 0; fd < __OPEN_MAX; fd++) {
		ft->op_ptr[fd] = NULL;
	}

	ft_STD_init(ft);

	return ft;
}

void ft_STD_init(struct filetable *ft){
	int result;
	char buf1[] = {'c', 'o', 'n', ':', '\0'};
	char buf2[] = {'c', 'o', 'n', ':', '\0'};
	char buf3[] = {'c', 'o', 'n', ':', '\0'};
	//struct filetable *ft = curproc->p_filetable; 

	/* STIN attached to con:, with fd = 0 */
	//result = file_open(buf, O_CREAT|O_RDONLY, 0, NULL);
	result = file_open(buf1, O_RDONLY, 0, NULL, ft);

	if ( result ) {
		kfree(ft);
	}
	KASSERT(result == 0);

	/* STOUT and STDERR attached to con:, with fd = 1, 2, respectively*/
	//result = file_open(buf, O_CREAT|O_WRONLY, 0, NULL);
	result = file_open(buf2, O_WRONLY, 0, NULL, ft);

	if ( result ) {
		kfree(ft);
	}
	KASSERT(result == 0);

	//result = file_open(buf, O_CREAT|O_WRONLY, 0, NULL);
	result = file_open(buf3, O_WRONLY, 0, NULL, ft);

	if ( result ) {
		kfree(ft);
	}
	KASSERT(result == 0);
}

int file_open(char *filename, int flags, int mode, int *retfd, struct filetable *ft){
    struct vnode *vn;
	struct openfile *file;
	struct stat info;
	int result;

	//kprintf("Print filename: %s\n", filename);

	result = vfs_open(filename, flags, mode, &vn);
	if (result) {
		return result;
	}

	file = kmalloc(sizeof(struct openfile));
	if (file == NULL) {
		vfs_close(vn);
		return ENOMEM;
	}

	/* initialize the file struct */
	file->lock = lock_create("of_lock");
	if (file->lock == NULL) {
		vfs_close(vn);
		kfree(file);
		return ENOMEM;
	}

	/* checks for invalid access modes */
	//KASSERT(flags==O_RDONLY || flags==O_WRONLY || flags==O_RDWR || flags==O_APPEND);

	result = VOP_STAT(vn, &info); //Thanks to it I can take the information on the size

	if ( result ) {
		return result;
	}

	if ( file->mode==O_APPEND ) {
		file->offset = info.st_size;	//check if +1 is required
		file->mode = flags;
		//kprintf("Append\n");
	} else {
		file->offset = 0;
		file->mode = flags & O_ACCMODE;	//ACCMODE is a mask only for RDONLY, WRONLY and RDWR, so APPEND is not maintained
		//kprintf("Not append\n");
	}

	file->f_cwd = vn;
	file->reference_count = 1;

    /* place the file in the filetable, getting the file descriptor */
	result = filetable_placefile(file, retfd, ft);
	if (result) {
		lock_destroy(file->lock);
		kfree(file);
		vfs_close(vn);
		return result;
	}

	return 0;
}

int filetable_placefile(struct openfile *of, int *fd, struct filetable *ft) {
	lock_acquire(ft->ft_lock);	//it doesn't return nothing, so the error isn't checked.

    for( int i = 0; i < __OPEN_MAX; i++ ) {
        if ( ft->op_ptr[i] == NULL ) {
            ft->op_ptr[i] = of;
			if(fd != NULL)
            	*fd = i;
			lock_release(ft->ft_lock);
            return 0;
        }
    }
	lock_release(ft->ft_lock);
    return EMFILE; //Too many open files

}



int findFD ( int fd, struct openfile **of ) {

    struct filetable *ft = curproc->p_filetable; //probably curproc

    if ( fd < 0 || fd >= __OPEN_MAX ) { //Control if the file descriptor fd is into a valid range
        return EBADF; //Bad file descriptor
    }
	/* if (ft->op_ptr[fd] == NULL)
		kprintf("No open file associated to %d", fd);
		return EBADF; */
		
	*of = ft->op_ptr[fd]; //associate the openfile structure to the one pointed by
                            //the file descriptor in the System File Table
	//kprintf("findFD %d %d\n", fd, (int)*of);
    return 0;
}

int closeOpenFile ( struct openfile *of ) {
	lock_acquire(of->lock);
    if ( of->reference_count == 1 ) { //If this is the last opening of the file, we can 
                                      //destroy the openfile structure by the System File Table
        vfs_close(of->f_cwd);
		lock_release(of->lock);
		lock_destroy(of->lock);
		kfree(of);
        //free also memory if we use kmalloc with kfree(file)
    } else { //if it is not, we can only decrease the reference count, controlling the PANIC
             //with KASSERT
        KASSERT(of->reference_count > 1);
        of->reference_count--;
		lock_release(of->lock);
    }

    return 0;
}

void filetable_destroy(struct filetable * ft){
	lock_acquire(ft->ft_lock);
	for(int fd = 0; fd < __OPEN_MAX; fd++){
		if(ft->op_ptr[fd] != NULL){
			closeOpenFile(ft->op_ptr[fd]);
		}
	}
	lock_release(ft->ft_lock);
	lock_destroy(ft->ft_lock);
}

void filetable_copy(struct filetable * new_ft){
	lock_acquire(curproc->p_filetable->ft_lock);
	memcpy(new_ft, curproc->p_filetable, sizeof(struct filetable));
	
	//reference_count is used only in case of multiple threads under the same proc, since our filetable is contained in the process itself (so do the openfiles)
	for(int fd=0; fd<__OPEN_MAX; fd++){
		if(curproc->p_filetable->op_ptr[fd] != NULL)
			new_ft->op_ptr[fd]->reference_count++;
	}	
	lock_release(curproc->p_filetable->ft_lock);
}

