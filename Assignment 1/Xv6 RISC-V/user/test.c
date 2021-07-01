#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

struct perf
{
    int ctime;             // process creation time
    int ttime;             // process termination time
    int stime;             // the total time the process spent in the SLEEPING state
    int retime;            // the total time the process spent in the RUNNABLE state
    int rutime;            // the total time the process spent in the RUNNING state
    int bursttime;         // process burst time
    int average_bursttime; // approximate estimated burst time
};

int main(int argc, char **argv)
{
    int pid2;
    set_priority(5);

    if ((pid2 = fork()) == 0)
    {
        printf("child is running: %d\n", pid2);
        int i = 0;
        while (i < 400)
        {
            // sleep(10);
            printf("child is running: %d - %d\n", getpid(), i);
            i++;
        }
    }
    else
    {
        set_priority(2);

        // wait(&pid2);
        printf("parent is running\n");
        printf("parent is running\n");
        printf("parent is running\n");
        printf("parent is running\n");
        printf("parent is running\n");

        int pid3;
        if ((pid3 = fork()) == 0)
        {
            set_priority(1);

            printf("child is running2: %d\n", pid3);
            int i = 0;
            while (i < 400)
            {
                // sleep(1);
                printf("child is running2: %d - %d\n", getpid(), i);
                i++;
            }
        }
        else
        {
            wait(&pid2);

            wait(&pid3);
            printf("parent is done\n");
        }
    }
    printf("%d is done\n", pid2);

    exit(0);

    // printf("started\n");

    // int pid2;
    // if ((pid2 = fork()) == 0)
    // {
    //     int c = 0;
    //     while (c < 3)
    //     {
    //         printf("child is running\n");
    //         sleep(10);
    //         c++;
    //     }
    //     // while (c < 15000)
    //     // {
    //     //     printf("%d", c);
    //     //     c++;
    //     // }
    //     while (1)
    //     {
    //         ;
    //     }

    //     printf("\n");
    // }
    // else
    // {
    //     int status;
    //     struct perf p;

    //     int x = wait_stat(&status, &p);

    //     printf("ret val: %d ", x);
    //     printf("ctime: %d ", p.ctime);
    //     printf("ttime: %d ", p.ttime);
    //     printf("stime: %d ", p.stime);
    //     printf("retime: %d ", p.retime);
    //     printf("rutime: %d\n", p.rutime);
    // }

    // sleep(1);
    // sbrk(4096);

    // exit(0);

    // printf("started\n");
    // int pid = getpid();
    // int mask = (1 << 13 | 1 << 1 | 1 << 6 | 1 << 12);
    // trace(mask, pid);
    // printf("traced\n");

    // int pid2;
    // if ((pid2 = fork()) == 0)
    // {
    //     while (1)
    //     {
    //         sleep(10);
    //     }
    // }
    // else
    // {
    //     kill(pid2);
    // }

    // sleep(1);
    // sbrk(4096);

    // exit(0);
}