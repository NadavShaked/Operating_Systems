#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

int nexttid = 1;
struct spinlock tid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void procinit(void)
{
  struct proc *p;
  struct thread *t;

  initlock(&pid_lock, "nextpid");
  initlock(&tid_lock, "nexttid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
    {
      initlock(&t->lock, "thread");
    }
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// Return the current struct thread *, or zero if none.
struct thread *
mythread(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

int alloctid()
{
  int tid;

  acquire(&tid_lock);
  tid = nexttid;
  nexttid = nexttid + 1;
  release(&tid_lock);

  return tid;
}

static void
freethread(struct thread *t)
{
  t->state = T_UNUSED;
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->tid = 0;
  t->trapframe_index = 0;
  t->parent = 0;
  if (t->user_trap_backup)
    kfree((void *)t->user_trap_backup);
  t->user_trap_backup = 0;
  if (t->kstack)
    kfree((void *)t->kstack);
  t->kstack = 0;
  t->trapframe = 0;
}

static struct thread *
allocthread(struct proc *p)
{
  struct thread *t;
  int i = 0;
  for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
  {
    if (t != mythread())
    {
      acquire(&t->lock);
      if (t->state == T_UNUSED)
      {
        goto found;
      }
      else
      {
        release(&t->lock);
      }
    }
    i++;
  }
  return 0;

found:
  t->state = T_USED;
  t->killed = 0;
  t->tid = alloctid();
  t->trapframe_index = i;
  t->parent = p;
  t->trapframe = &p->t_trapframe[i];

  // Allocate a trapframe backup page.
  if ((t->user_trap_backup = (struct trapframe *)kalloc()) == 0)
  {
    freethread(t);
    release(&t->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;

  // Allocate kernel stack.
  if ((t->kstack = (uint64)kalloc()) == 0)
  {
    freethread(t);
    release(&t->lock);
    return 0;
  }
  t->context.sp = t->kstack + PGSIZE;

  return t;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == P_UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = P_USED;

  // Allocate a trapframe page.
  if ((p->t_trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  p->pending_signals = 0;
  p->signal_mask = 0;
  for (int i = 0; i < NUMOFSIGNALS; i++)
  {
    p->signal_handlers[i] = SIG_DFL;
    p->signal_handlers_mask[i] = 0;
  }
  p->signal_mask_backup = 0;
  p->is_stopped_signal_turnon = 0;

  // Allocate thread.
  struct thread *t;
  if ((t = allocthread(p)) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  release(&t->lock);

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->t_trapframe)
    kfree((void *)p->t_trapframe);
  p->t_trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = P_UNUSED;

  // ------------------------ Task 2.1.2 ------------------------
  p->pending_signals = 0;
  p->signal_mask = 0;
  for (int i = 0; i < NUMOFSIGNALS; i++)
  {
    p->signal_handlers[i] = 0;
    p->signal_handlers_mask[i] = 0;
  }
  p->signal_mask_backup = 0;
  p->is_stopped_signal_turnon = 0;
  // ------------------------------------------------------------
  // ------------------------- Task 3.1 -------------------------
  struct thread *t;
  for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
  {
    freethread(t);
  }
  // ------------------------------------------------------------
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if (mappages(pagetable, TRAPFRAME(0), PGSIZE,
               (uint64)(p->t_trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME(0), 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->t_trapframe->epc = 0;     // user program counter
  p->t_trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->p_threads[0].state = T_RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  acquire(&p->lock);
  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
    {
      release(&p->lock);
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  release(&p->lock);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  struct thread *nt;
  struct thread *t = mythread();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  nt = &np->p_threads[0];

  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  nt->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  // ------------------------ Task 2.1.2 ------------------------
  np->signal_mask = p->signal_mask;
  for (int i = 0; i < NUMOFSIGNALS; i++)
  {
    np->signal_handlers[i] = p->signal_handlers[i];
    np->signal_handlers_mask[i] = p->signal_handlers_mask[i];
  }
  // ------------------------------------------------------------

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&nt->lock);
  nt->state = T_RUNNABLE;
  release(&nt->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();
  struct thread *t = mythread();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = P_ZOMBIE;

  for (struct thread *t_iter = p->p_threads; t_iter < &p->p_threads[NTHREAD]; t_iter++)
  {
    if (t_iter->tid != t->tid && t_iter->state != T_UNUSED)
    {
      acquire(&t_iter->lock);
      t_iter->killed = 1;
      if (t_iter->state == T_SLEEPING)
      {
        t_iter->state = T_RUNNABLE;
      }
      release(&t_iter->lock);
      kthread_join(t_iter->tid, 0);
    }
  }
  release(&p->lock);

  acquire(&t->lock);
  t->state = T_ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == P_ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();

  c->proc = 0;
  c->thread = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for (p = proc; p < &proc[NPROC]; p++)
    {
      if (p->state == P_USED && (p->is_stopped_signal_turnon == 0 || (p->pending_signals & (1 << SIGKILL)) != 0 || ((p->pending_signals & (1 << SIGCONT)) != 0 && (p->signal_mask & (1 << SIGCONT)) == 0)))
      {
        // Switch to chosen thread.  It is the thread's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
        {
          acquire(&t->lock);
          if (t->state == T_RUNNABLE)
          {
            t->state = T_RUNNING;
            c->thread = t;
            c->proc = p;
            swtch(&c->context, &t->context);
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
            c->thread = 0;
          }
          release(&t->lock);
        }
      }
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct thread *t = mythread();

  if (!holding(&t->lock))
    panic("sched t->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (t->state == T_RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct thread *t = mythread();
  acquire(&t->lock);
  t->state = T_RUNNABLE;
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding t->lock from scheduler.
  release(&mythread()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();

  // Must acquire t->lock in order to
  // change t->state and then call sched.
  // Once we hold t->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks t->lock),
  // so it's okay to release lk.

  acquire(&t->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = T_SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;
  struct thread *t;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    for (t = p->p_threads; t < &p->p_threads[NTHREAD]; t++)
    {
      if (t != mythread())
      {
        acquire(&t->lock);
        if (t->state == T_SLEEPING && t->chan == chan)
        {
          t->state = T_RUNNABLE;
        }
        release(&t->lock);
      }
    }
  }
}

// ------------------------ Task 2.2.1 ------------------------

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid, int signum)
{
  struct proc *p;

  if (signum < 0 || signum > NUMOFSIGNALS)
    return -1;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      if (signum != SIGKILL && signum != SIGSTOP && p->signal_handlers[signum] == (void *)SIG_IGN)
      {
        release(&p->lock);
        return 0;
      }
      if (p->state == P_ZOMBIE)
      {
        release(&p->lock);
        return -1;
      }

      p->pending_signals |= 1 << signum;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
// ------------------------------------------------------------

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [P_UNUSED] "unused",
      [P_ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == P_UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
// ------------------------ Task 2.1.3 ------------------------
uint sigprocmask(uint sigmask)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  uint old_mask = p->signal_mask;
  p->signal_mask = sigmask;
  release(&p->lock);
  return old_mask;
}
// ------------------------------------------------------------
// ------------------------ Task 2.1.4 ------------------------
int sigaction(int signum, struct sigaction *act, struct sigaction *oldact)
{
  if (signum >= 0 && signum <= NUMOFSIGNALS && signum != SIGKILL && signum != SIGSTOP)
  {
    struct proc *p = myproc();
    acquire(&p->lock);

    if (oldact != 0)
    {
      // oldact->sa_handler = p->signal_handlers[signum];
      // oldact->sigmask = p->signal_handlers_mask[signum];

      // struct sigaction oldaction;
      // oldaction.sa_handler = p->signal_handlers[signum];
      // oldaction.sigmask = p->signal_handlers_mask[signum];
      // if (copyout(p->pagetable, oldact, (char *)&oldaction, sizeof(p->signal_handlers[signum])) < 0)
      // {
      //   release(&p->lock);
      //   return -1;
      // }

      struct sigaction oldaction;
      oldaction.sa_handler = p->signal_handlers[signum];
      oldaction.sigmask = p->signal_handlers_mask[signum];
      if (copyout(p->pagetable, (uint64)oldact, (char *)&oldaction, sizeof(struct sigaction)) < 0)
      {
        release(&p->lock);
        return -1;
      }
    }

    if (act != 0)
    {
      struct sigaction action;
      if (copyin(p->pagetable, (char *)&action, (uint64)act, sizeof(struct sigaction)) < 0)
      {
        release(&p->lock);
        return -1;
      }
      p->signal_handlers[signum] = action.sa_handler;
      p->signal_handlers_mask[signum] = action.sigmask;
    }

    release(&p->lock);
    return 0;
  }
  return -1;
}
// ------------------------------------------------------------
// ------------------------ Task 2.1.5 ------------------------
void sigret(void)
{
  struct thread *t = mythread();
  struct proc *p = myproc();

  acquire(&p->lock);
  acquire(&t->lock);

  memmove(t->trapframe, t->user_trap_backup, sizeof(struct trapframe));

  p->signal_mask = p->signal_mask_backup;

  release(&t->lock);
  release(&p->lock);
}
// ------------------------------------------------------------
// ------------------------- Task 3.2 -------------------------
int kthread_create(uint64 start_func, uint64 stack)
{
  struct proc *p = myproc();
  struct thread *t = mythread();
  struct thread *nt;

  if ((nt = allocthread(p)) == 0)
    return -1;

  int tid = nt->tid;
  nt->state = T_RUNNABLE;
  *nt->trapframe = *t->trapframe;
  nt->trapframe->epc = (uint64)start_func;
  nt->trapframe->sp = (uint64)(stack + STACK_SIZE - 16);

  release(&nt->lock);
  return tid;
}

int kthread_id()
{
  struct thread *t = mythread();

  if (t)
    return t->tid;

  return -1;
}

void kthread_exit(int status)
{
  struct proc *p = myproc();
  struct thread *t = mythread();

  acquire(&p->lock);
  int num_threads_running = 0;
  for (struct thread *t_iter = p->p_threads; t_iter < &p->p_threads[NTHREAD]; t_iter++)
  {
    acquire(&t_iter->lock);
    if (t_iter->tid != t->tid && t_iter->state != T_UNUSED && t_iter->state != T_ZOMBIE)
    {
      num_threads_running++;
    }
    release(&t_iter->lock);
  }

  if (num_threads_running == 0)
  {
    release(&p->lock);
    exit(status);
  }

  acquire(&t->lock);
  t->xstate = status;
  t->state = T_ZOMBIE;

  release(&p->lock);

  //wake up threads waiting for this thread to exit
  wakeup(t);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

int kthread_join(int thread_id, int *status)
{
  struct proc *p = myproc();

  for (struct thread *t_join = p->p_threads; t_join < &p->p_threads[NTHREAD]; t_join++)
  {
    acquire(&t_join->lock);
    if (thread_id == t_join->tid)
    {
      while (t_join->state != T_UNUSED && t_join->state != T_ZOMBIE)
        sleep(t_join, &t_join->lock);

      if (t_join->state == T_ZOMBIE)
      {
        if (status != 0 && copyout(p->pagetable, (uint64)status, (char *)&t_join->xstate, sizeof(t_join->xstate)) < 0)
        {
          release(&t_join->lock);
          return -1;
        }
        freethread(t_join);
      }

      release(&t_join->lock);
      return 0;
    }
    release(&t_join->lock);
  }

  return -1;
}
// ------------------------------------------------------------
// ------------------------- Task 4.1 -------------------------
struct bSemaphore bSemaphore_array[MAX_BSEM];

void initBSemaphoreArray()
{
  struct bSemaphore *bs;
  for (bs = bSemaphore_array; bs < &bSemaphore_array[MAX_BSEM]; bs++)
  {
    bs->state = S_UNUSED;
    bs->waiting = 0;
    initlock(&bs->lock, "bSemaphore");
  }
}

int bsem_alloc()
{
  struct bSemaphore *bs;
  int i = 0;
  for (bs = bSemaphore_array; bs < &bSemaphore_array[MAX_BSEM]; bs++)
  {
    acquire(&bs->lock);
    if (bs->state == S_UNUSED)
    {
      bs->state = S_USED;
      bs->waiting = 1;
      release(&bs->lock);
      return i;
    }
    release(&bs->lock);
    i++;
  }

  return -1;
}

void bsem_free(int descriptor)
{
  if (descriptor >= 0 && descriptor < MAX_BSEM)
  {
    acquire(&bSemaphore_array[descriptor].lock);
    bSemaphore_array[descriptor].state = S_UNUSED;
    bSemaphore_array[descriptor].waiting = 0;
    release(&bSemaphore_array[descriptor].lock);
  }
}

void bsem_down(int descriptor)
{
  acquire(&bSemaphore_array[descriptor].lock);
  if (bSemaphore_array[descriptor].state == S_USED)
  {
    while (bSemaphore_array[descriptor].waiting == 0)
      sleep(&bSemaphore_array[descriptor], &bSemaphore_array[descriptor].lock);
    bSemaphore_array[descriptor].waiting = 0;
  }
  release(&bSemaphore_array[descriptor].lock);
}

void bsem_up(int descriptor)
{
  acquire(&bSemaphore_array[descriptor].lock);
  bSemaphore_array[descriptor].waiting = 1;
  wakeup(&bSemaphore_array[descriptor]);
  release(&bSemaphore_array[descriptor].lock);
}
// ------------------------------------------------------------
