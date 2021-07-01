struct counting_semaphore
{
    // struct spinlock lock;

    int state;
    int init_value;
    int value;
    int bS1_descriptor;
    int bS2_descriptor;
};

int csem_alloc(struct counting_semaphore *sem, int initial_value);
void csem_free(struct counting_semaphore *sem);
void csem_down(struct counting_semaphore *sem);
void csem_up(struct counting_semaphore *sem);

// struct counting_semaphore{
//     int state;
//     int numOfProc;
//     int S1;
//     int S2;
//     int wake;
// };

// int csem_alloc(struct counting_semaphore *sem, int initial_value);
// void csem_free(struct counting_semaphore *sem);
// void csem_down(struct counting_semaphore *sem);
// void csem_up(struct counting_semaphore *sem);