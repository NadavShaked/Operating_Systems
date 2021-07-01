#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "signals.h"
#include "myParam.h"

extern void *sigret_addr_start(void);
extern void *sigret_addr_end(void);

// ------------------------ Task 2.3 ------------------------
void sig_kill(int signum)
{
    struct proc *p = myproc();
    struct thread *t;
    p->killed = 1;
    for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
    {
        acquire(&t->lock);
        if (t->state == T_SLEEPING)
        {
            // Wake process from sleep().
            t->state = T_RUNNABLE;
        }
        release(&t->lock);
    }
    p->pending_signals &= ~(1 << signum);
    p->is_stopped_signal_turnon = 0;
}

void sig_stop(int signum)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->pending_signals &= ~(1 << signum);
    p->is_stopped_signal_turnon = 1;
    release(&p->lock);
}

void sig_cont(int signum)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->pending_signals &= ~(1 << signum);
    p->is_stopped_signal_turnon = 0;
    release(&p->lock);
}
// ----------------------------------------------------------
// ------------------------ Task 2.4 ------------------------
void kernel_signal_handler(int signum)
{
    if (signum == SIGSTOP)
        sig_stop(signum);
    else if (signum == SIGCONT)
        sig_cont(signum);
    else
        sig_kill(signum);
}

void user_signal_handler(int signum)
{
    struct proc *p = myproc();
    struct thread *t = mythread();

    p->signal_mask_backup = p->signal_mask;
    p->signal_mask = p->signal_handlers_mask[signum];
    memmove(t->user_trap_backup, t->trapframe, sizeof(struct trapframe));
    uint sig_ret_size = sigret_addr_end - sigret_addr_start;
    t->trapframe->sp -= sig_ret_size;
    copyout(p->pagetable, (uint64)t->trapframe->sp, (char *)&sigret_addr_start, sig_ret_size);
    t->trapframe->a0 = signum;
    t->trapframe->ra = t->trapframe->sp;
    p->pending_signals = p->pending_signals & ~(1 << signum);
    t->trapframe->epc = (uint64)p->signal_handlers[signum];
}

void signal_handler()
{
    struct proc *p = myproc();
    for (int i = 0; i < NUMOFSIGNALS; i++)
    {
        if ((p->pending_signals & (1 << i)) != 0 && (p->signal_mask & (1 << i)) == 0)
        {
            p->signal_mask_backup = p->signal_mask;
            p->signal_mask = p->signal_handlers_mask[i];
            p->signal_mask |= (1 << i); // Block nasted signals
            if (p->signal_handlers[i] == SIG_DFL || i == SIGCONT)
            {
                kernel_signal_handler(i);
                p->signal_mask = p->signal_mask_backup;
            }
            else
            {
                user_signal_handler(i);
                p->pending_signals &= ~(1 << i);
            }
        }
    }
}
// ----------------------------------------------------------