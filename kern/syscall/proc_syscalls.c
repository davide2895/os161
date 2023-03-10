#include <pid.h>
#include <types.h>
#include <kern/limits.h>
#include <kern/syscall.h>
#include <syscall.h>
#include <lib.h>
#include <addrspace.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>
#include <file.h>
#include <thread.h>
#include <../arch/mips/include/trapframe.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <synch.h>
#include <vnode.h>
#include <kern/wait.h>

//intermediate enter_forked_process function 
void
efp_mips_thread(void *data1, unsigned long data2){

	(void)data2;

    enter_forked_process((struct trapframe *)data1);

}

int sys_fork( struct trapframe *tf, pid_t* retval ) {

    struct trapframe *tf_new;
    //struct addrspace *addr_new = NULL;
    struct proc *new_proc = NULL;
    int ret;

    //spinlock_acquire(&curproc->p_lock);	//already done in proc_create_runprogram

    /* From here */

    if ( pt->n_active_processes >= __PID_MAX ) { //Too many active processes
        return ENPROC;
    }

    // To here, there's a search for an error but it need to be re evaluated when we find a final
    // place for the processtable.

    // Need to understande if the name can be equal for all processes that we want to create
    //Maybe we can use curproc->p_name.

    new_proc = proc_create_runprogram(curproc->p_name);
    if ( new_proc == NULL ) {
		return ENOMEM;
    }

    /* Here there is a control related to the creation of the new pid
    // and new associations. */

	spinlock_acquire(&curproc->p_lock);
    new_proc->p_pidinfo->parent_pid = curproc->p_pidinfo->current_pid;
	spinlock_release(&curproc->p_lock);

	// Create a new address space and copy the old one (parent process)

    ret = as_copy(curproc->p_addrspace, &new_proc->p_addrspace);
    if ( ret ) {
        proc_destroy(new_proc);
        return ret;
    }

	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		new_proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

    // We allocate a new trapframe and copy it into the new one

    tf_new = kmalloc(sizeof(struct trapframe));
    if ( tf_new == NULL ) {
		as_destroy(new_proc->p_addrspace);
        proc_destroy(new_proc);
        return ENOMEM;
    }

    memcpy(tf_new, tf, sizeof(struct trapframe));
	tf_new->tf_v0 = 0;	//retval
	tf_new->tf_v1 = 0;	//retval1		   
	tf_new->tf_a3 = 0;	//signal no error
	tf_new->tf_epc += 4;	//advances the program counter to avoid restarting the syscall over and over again.

    // Here we copy filetable and update the process table//

    filetable_copy(new_proc->p_filetable);
    processtable_placeproc(new_proc, new_proc->p_pidinfo->current_pid);

    //This is the return for the parent

    *retval = new_proc->p_pidinfo->current_pid;
	
	// Create a new thread and attach it to the new proc (copying the trapframe
    // of the old process to it).

    ret = thread_fork( curthread->t_name, new_proc, efp_mips_thread, (void *)tf_new, (unsigned long) 1);
    if ( ret ) {
		as_destroy(new_proc->p_addrspace);
        kfree(tf_new);
        proc_destroy(new_proc);
        return ret;
    }
	
    // This is the return for the child
    return 0;

}

int
loadexec(char *progname, vaddr_t *entrypoint, vaddr_t *stackptr)
{
	struct addrspace *old_as, *new_as;
    
	struct vnode *v;
	int result;
    char *newname;

    /* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* Create a new address space. */
	new_as = as_create();
	if (new_as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	old_as = proc_setas(new_as);    //sets the new as and returns the old one in case of errors
	as_activate();	//it is a control on the new set new_as

	/* Load the executable. */
	result = load_elf(v, entrypoint);
	if (result) {
        vfs_close(v);
        proc_setas(old_as); 
		as_activate();
		as_destroy(new_as);
        return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(new_as, stackptr);
	if (result) {
        proc_setas(old_as); 
		as_activate();
		as_destroy(new_as);
		return result;
	}

    if (old_as) {
		as_destroy(old_as);
	}	//maybe this is not needed

	/* new name for thread */   
    newname = kstrdup(progname);    //strcpy with a kmalloc to have the same dimension of the argument
	if (newname == NULL) {
		return ENOMEM;
	}
    kfree(curthread->t_name);
	curthread->t_name = newname;

	return 0;
}

int sys_execv(char * progname, char ** argv){

	vaddr_t entrypoint, stackptr;
    char path[__PATH_MAX];
	int argc;
    int result;
	char *commands[10];
	//int *pointer;
	size_t actual;
	char **argvptr;
	//unsigned int curaddr;
	int len;
    argc = 0;
	int i = 0;
	//int j = 0;

    result = copyinstr((const_userptr_t)progname, path, __PATH_MAX, NULL);    //copy the progname from userspace
	//copyinstr copies only for the length of progname, __PATH_MAX is used only inside it to verify that the actual length doesn't exceed this value.
    if (result){
        return result;
    }
	
	while(argv[i] != NULL) {

		//a control for i to not exceed __ARG_MAX is needed here

		commands[i] = (char *)kmalloc(100*sizeof(char));	//pointer to a heap space for a single command of 100 chars
		if (commands[i] == NULL){
        	return ENOMEM;
    	}
		
		result = copyinstr((userptr_t)argv[i], commands[i], (100*sizeof(char)), &actual);
		if ( result ) {
			return EFAULT;
		}

		i++;
		//j = j+1;
		argc = argc + 1;
	}

	commands[i+1] = NULL;

	result = loadexec(path, &entrypoint, &stackptr);
	if ( result ) {
		return result;
	}

	argvptr  = (char **) kmalloc(sizeof(char *) * (argc+1));	//+1 for the NULL pointer
	if(argvptr == NULL)	
		return ENOMEM;

	
	for(int i=0; i< argc; i++){	

			len = strlen(commands[i])+1;
			stackptr = stackptr - len;	//with the following copyoutstr the address increases toward the end of the stack
			if(stackptr & 0x3)
				stackptr -= (stackptr & 0x3); //masks all bits except the first 2 LSBs
			
			result = copyoutstr(commands[i], (userptr_t) stackptr, len,NULL);
			if(result){
				kfree(argvptr);
				return result;
			}	
			argvptr[i] = (char *) stackptr;	
		}

	argvptr[argc] = NULL;
	stackptr = stackptr - sizeof(char *)*(argc+1) ;																					/*- ((stackptr-stackoffset)%8)*/
	result = copyout (argvptr, (userptr_t) stackptr, sizeof(char *)*(argc+1));
	if(result){
		kfree(argvptr);
		return result;
	}
			
	kfree(argvptr);


	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr/*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

void sys__exit( int status ) {
    struct proc *p = curproc;
    curproc->p_pidinfo->exit_status = _MKWAIT_EXIT(status);
    curproc->p_pidinfo->exit = true;
	as_destroy(curproc->p_addrspace);
    
    V(p->p_sem); // This semaphore is put high to be used by the waitpid() system call
	//kprintf("sem value: %d\n", (unsigned int)curproc->p_sem->sem_count);
	thread_exit();
}

int sys_getpid( pid_t *retval )
{
    *retval = curproc->p_pidinfo->current_pid;

    return 0;
}

int sys_waitpid( pid_t pid, userptr_t status, int option, pid_t *retval ) {

    // We need to recover the proc structure using the pid associated to the process that
    // we are waiting. So we need a processtable that take into account all the processes
    // (basically the same we did for filetable)
	if (pid <= 0 || pid > __PID_MAX){
		return ECHILD;
	}
    struct proc *p = proc_search_pid(pid);
    int p_status;
	int result;

    if ( p == NULL ) {
        return ENOMEM;
    }

	//if (status == NULL){
	//	return EINVAL;
	//}

	if ( option != 0 ) {
		return EINVAL;
	}
	
    p_status = proc_wait(p);
	
    result = copyout((const void *) &p_status, status, sizeof(int));
    if (result) {
        return result;
    }

	
	//here something breaks the filetable (discovered from miniforktest)
	
    *retval = pid;
    return 0;
}