#include <syscall.h>
#include <types.h>
#include <vnode.h>
#include <vfs.h>
#include <proc.h>
#include <copyinout.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>    //contains limits for strings, etc.
#include <file.h>
#include <current.h>
#include <uio.h>
#include <synch.h>
#include <stat.h>
#include <kern/seek.h>

int sys_open(userptr_t filename, int flags, int mode, int* retval)
{
    char path[__PATH_MAX]; //the path name has a max length given in kern/limits.h
    int ret;

    ret = copyinstr(filename, path, __PATH_MAX, NULL); //The last is referred to the actual lenght of the string, if we have it. If not, we can put NULL here
    if ( ret ) {
        return ret; //ENAMETOOLONG should be the return value from copyinstr
    }
    
    ret = file_open(path, flags, mode, retval, curproc->p_filetable);
    if ( ret ) {
        return ret;
    }

    return 0;
}

int sys_close( int fd ) {

    struct openfile *of;
    int ret;

    ret = findFD(fd, &of);
    if ( ret ) {
        return ret;
    }
    if ( of == NULL ) { //If it is NULL, obviously we are not pointing any existent structure
        return EBADF;
    }

   ret = closeOpenFile(of);

   if ( ret ) {
        return ret;
   }

    curproc->p_filetable->op_ptr[fd] = NULL; //At the end, the structure is deleted by the array

    return 0;
}

int
sys__getcwd(char *buf, size_t buflen, int32_t *retval)
{
    struct iovec iov;
    struct uio myuio;
    int result;

    *retval = -1;

    if (buf == NULL)
        return EFAULT;

    uio_uinit( &iov, &myuio, buf, buflen, 0, UIO_READ );
    // iov.iov_ubase = (userptr_t)buf;
	// iov.iov_len = buflen;
	// myuio.uio_iov = &iov;
	// myuio.uio_iovcnt = 1;
	// myuio.uio_offset = 0;
	// myuio.uio_resid = buflen;
	// myuio.uio_segflg = UIO_USERSPACE;
	// myuio.uio_rw = UIO_READ;
	// //u->uio_space = curproc->p_addrspace; //or NULL
	// myuio.uio_space = proc_getas();

    result = vfs_getcwd( &myuio );
    if (result)
        return result;

    *retval = buflen - myuio.uio_offset;
    return 0;
}

int
sys_chdir(const char *pathname, int32_t *retval) {

    char *path;
    int result;
    size_t len;

    KASSERT (curproc != NULL);

    *retval = -1;

    // if (pathname == NULL)
    //     return EFAULT;

     if (pathname == NULL)     //|| (!as_is_addr_valid(curproc->p_addrspace, (vaddr_t) pathname))
        return EFAULT;
    // result = check_userptr((const_userptr_t) pathname);
    // if (result)
    //     return result;

    len = strlen(pathname) + 1;

    path = kmalloc(len);
    if (path == NULL)
        return ENOMEM;

    result = copyinstr((const_userptr_t)pathname, path, len, NULL);
    if (result)
        goto err;

    result = vfs_chdir(path);
    if (result)
        goto err;

    *retval = 0;
    return 0;

err:
    kfree(path);
    return result;

}

int sys_dup2( int oldfd, int newfd, int *outfd ) {

    struct filetable *ft = curproc->p_filetable;
    struct openfile *of, *temp_of;
    int ret;

    ret = findFD( oldfd, &of );
    if ( ret ) {
        return ret;
    }
    //if (of == NULL){
    //    return EBADF;
    //}

    ret = findFD( newfd, &temp_of);
    if ( ret ) {
        return ret;
    }
    //if (temp_of == NULL){
    //    return EBADF;
    //}
    
    //If the file descriptors are the same, the dup2 finishes without errors
    if ( oldfd == newfd ) {
        return 0;
    }
    
    if ( temp_of != NULL ) {
        
        ret = closeOpenFile(temp_of);
        kprintf("Try this boi\n");
        if ( ret ) {
            return ret;
        }
        ft->op_ptr[newfd] = NULL; //At the end, the structure is deleted by the array
    }

    //Increase the refcount
    lock_acquire(of->lock);
    of->reference_count++;
    lock_release(of->lock);

    ft->op_ptr[newfd] = of; //In this way I am putting in the address pointed by the new fd the address
                            //of the structure pointed by the old fd. So, the refcount need to be incremented
                            //because we have two file descriptors that are referring to the same file.

    *outfd = newfd;
    return 0;
}

int sys_lseek( int fd, off_t offset, int whence, int *retval, int *retval1) {

    struct stat info;
    struct openfile *of;
    int ret;
    off_t seek;

    ret = findFD( fd, &of );
    if ( ret ) {
        return ret;
    }
    if ( of == NULL ) { //If it is NULL, obviously we are not pointing any existent structure
        return EBADF;
    }

    lock_acquire(of->lock);

    //Depending on the type of seek, we need to set the retval to change the offset

    seek = of->offset; //default value

    switch( whence ) {
        case SEEK_SET: //Set the offset sent by the user
            seek = offset;
            break;
        case SEEK_CUR: //Set the offset as the current position plus offset variable
            seek += offset;
            break;
        case SEEK_END: //Set the offset as the size of the file plus offset variable
            ret = VOP_STAT(of->f_cwd, &info); //In this way I populate the info structure with
                                              //Informations of this opened file.
            if ( ret ) {
                lock_release(of->lock);
                return ret;
            }

            seek = info.st_size + offset;
            break;
        default: //whence is not valid
            lock_release(of->lock);
            return EINVAL;
    }

    //We can also put the error for a resulting negative offset (or beyond the end of a seekable device)

    of->offset = seek;
    
    lock_release(of->lock);

    *retval = seek >> 32;
    *retval1 = seek & 0xFFFFFFFF;

    return 0;
}

int sys_read( int fd, userptr_t buf, size_t size, int *retval ) {

    struct openfile *of;

    struct iovec iov;
    struct uio myuio;

    int ret;

    ret = findFD(fd, &of);
    if ( ret ) {
        return ret;
    }
    if ( of == NULL ) { //If it is NULL, obviously we are not pointing any existent structure
        return EBADF;
    }

    lock_acquire(of->lock);

    if (of->mode == O_WRONLY){
        return EBADF;
    }

    //Initialize a uio suitable for I/O from a kernel buffer.

    uio_uinit(&iov, &myuio, buf, size, of->offset, UIO_READ);

    //VOP_READ is used to perform the effective read from the io

    ret = VOP_READ(of->f_cwd, &myuio);

    if ( ret ) {
        lock_release(of->lock);
        return ret;
    }

    //Now we replace the offset with the updated one in the uio

    of->offset = myuio.uio_offset;

    //Here we can set retval to the amount read. In the uio struct there is a data that
    //take into account the residual amount of data to transfer. Thanks to it, we can obtain
    //the effective data read (and we can return it with retval).

    lock_release(of->lock);

    *retval = size - myuio.uio_resid;

    return 0;

}

int sys_write( int fd, userptr_t buf, size_t size, int *retval ) {

    struct openfile *of;

    struct iovec iov;
    struct uio myuio;

    int ret;

    ret = findFD(fd, &of);  //now "of" contains the pointer to the openfile structure corresponding to fd
    if ( ret ) {
        return ret;
    }
    if ( of == NULL ) { //If it is NULL, obviously we are not pointing any existent structure
        return EBADF;
    }
    lock_acquire(of->lock);
    
    if (of->mode == O_RDONLY){
        return EBADF;
    }

    //Initialize a uio suitable for I/O from a kernel buffer.

    uio_uinit(&iov, &myuio, buf, size, of->offset, UIO_WRITE);
    ret = VOP_WRITE(of->f_cwd, &myuio);

    if ( ret ) {
        lock_release(of->lock);
        return ret;
    }

    //Now we replace the offset with the updated one in the uio

    of->offset = myuio.uio_offset;

    lock_release(of->lock);

    //Here we can set retval to the amount read. In the uio struct there is a data that
    //take into account the residual amount of data to transfer. Thanks to it, we can obtain
    //the effective data read (and we can return it with retval).

    *retval = size - myuio.uio_resid;;

    return 0;
}

int sys_remove(char *pathname, int *retval){
    kprintf("Inside remove\n");
    int ret;
    ret = vfs_remove(pathname);
    if(ret != 0) *retval = -1;
    return ret;
}
int sys_rmdir(char *pathname, int *retval){
    kprintf("Inside rmdir\n");
    int ret;
    ret = vfs_rmdir(pathname);
    if(ret != 0) *retval = -1;
    return ret;
}
int sys_mkdir(char *pathname, mode_t mode, int *retval){
    kprintf("Inside mkdir\n");
    int ret;
    ret = vfs_mkdir(pathname, mode);
    if(ret != 0) *retval = -1;
    return ret;
}
