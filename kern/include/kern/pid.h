#include <types.h>
#include <kern/limits.h>
#include <spinlock.h>

struct pid {

    pid_t parent_pid; //parent pid
    pid_t current_pid; //pid of the actual process
    int exit_status; //process' exit status
    bool exit; //Process' exited?
    struct lock *p_lock; //Process' lock (maybe for a contemporary acces from waitpid() and the process
                         //itself that has to terminate).

};

struct processtable {
    struct proc *proc_ptr  [__PROC_MAX];
    pid_t *used_pid[__PID_MAX]; //If in the i position there's a 1, the position is occupied; otherwise
                               //, if it's 0, the position is free.
    struct spinlock pt_lock; //for contemporary acces for the allocation of a new process' pid
    int n_active_processes; //variable used to know how many processes are now active (we can't use
                            //the __PID_MAX variable because maybe we reached it as pid's value of
                            //a process but we have holes in the mean).
};

extern struct processtable *pt;

struct pid *pid_init( bool is_proc ); //Here we can also put the initialization of the queue for the pid to re-use
struct processtable *proctable_init(void);
void processtable_placeproc(struct proc *p, pid_t pid);
void processtable_remproc(pid_t pid);
pid_t get_newpid(void);
struct proc *proc_search_pid( pid_t pid );
int proc_wait( struct proc *process );
void pid_destroy(struct pid *);