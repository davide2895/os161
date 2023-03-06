#ifndef _FILE_SYSCALL_H_
#define _FILE_SYSCALL_H_

/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_open(userptr_t filename, int flags, int mode, int *retval);
int sys_read(int fd, userptr_t buf, size_t size, int *retval);
int sys_write(int fd, userptr_t buf, size_t size, int *retval);
int sys_close(int fd);
int sys_lseek(int fd, off_t offset, int32_t whence, off_t *retval);
int sys_dup2(int oldfd, int newfd, int *retval);
int sys_chdir(userptr_t path);
int sys___getcwd(userptr_t buf, size_t buflen, int *retval);
 
#endif /* _FILE_SYSCALL_H_ */