#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "kernel/spinlock.h"
#include "kernel/proc.h"
#include "kernel/defs.h"

struct counting_semaphore
{
    int value;
    int bS1_descriptor;
    int bS2_descriptor;
};

void csem_free(struct counting_semaphore *sem)
{
    sem->value = 0;
    bsem_free(sem->bS1_descriptor);
    bsem_free(sem->bS2_descriptor);
}

int csem_alloc(struct counting_semaphore *sem, int initial_value)
{
    if (initial_value < 0)
        return -1;

    if ((sem->bS1_descriptor = bsem_alloc()) < 0)
    {
        return -1;
    }
    if ((sem->bS2_descriptor = bsem_alloc()) < 0)
    {
        bsem_free(sem->bS1_descriptor);
        return -1;
    }

    if (initial_value < 1)
    {
        bsem_down(sem->bS2_descriptor);
    }

    sem->value = initial_value;

    return 0;
}

void csem_down(struct counting_semaphore *sem)
{
    bsem_down(sem->bS2_descriptor);
    bsem_down(sem->bS1_descriptor);

    sem->value -= 1;
    if (sem->value > 0)
        bsem_up(sem->bS2_descriptor);
    bsem_up(sem->bS1_descriptor);
}

void csem_up(struct counting_semaphore *sem)
{
    bsem_down(sem->bS1_descriptor);
    sem->value += 1;
    if (sem->value == 1)
        bsem_up(sem->bS2_descriptor);
    bsem_up(sem->bS1_descriptor);
}
