
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/myParam.h"

struct sigaction
{
    void (*sa_handler)(int);
    uint sigmask;
};

void handler1(int n)
{
    printf("test 2: passed\n");
    return;
}

void handler2(int n)
{
    printf("test 2: passed\n");
    return;
}

// set signal process mask test
void sigprocmask_test()
{
    printf("sigprocmask_test\n");
    int mask1 = 16;
    if (sigprocmask(mask1) == 0)
        printf("test 1: passed\n");
    else
        printf("test 1: failed\n");

    int pid;
    if ((pid = fork()) == 0)
    { // child process
        if (sigprocmask(0) == 16)
            printf("test 2: passed\n");
        else
            printf("test 2: failed\n");
    }
    else
    {
        int w;
        wait(&w);
        if (sigprocmask(0) == 16)
            printf("test 3: passed\n");
        else
            printf("test 3: failed\n");
    }
}

// kernel signals test
// should stop till continue signal, print C* P stopped continue C* killed
void kernel_signal_test()
{
    printf("kernel_signal_test\n");
    int pid;
    if ((pid = fork()) == 0)
    { // child process
        while (1)
        {
            printf("C");
            sleep(5);
        }
    }
    else
    {
        sleep(50);
        printf("P\n");
        kill(pid, SIGSTOP);
        printf("stopped\n");
        sleep(50);
        kill(pid, SIGCONT);
        printf("continued\n");
        sleep(50);
        kill(pid, SIGKILL);
        printf("killed\n");
        wait(&pid);
    }
    printf("tests done\n");
}

// ignore signals from signal mask test
// should ignore continue signal, print C* P stopped C* killed
void signalmask_test()
{
    printf("signalmask_test\n");
    int pid;
    if ((pid = fork()) == 0)
    { // child process
        uint mask = (1 << SIGCONT);
        sigprocmask(mask);
        while (1)
        {
            printf("C");
            sleep(5);
        }
    }
    else
    {
        sleep(50);
        printf("P\n");
        kill(pid, SIGSTOP);
        printf("stopped\n");
        sleep(50);
        kill(pid, SIGCONT);
        sleep(50);
        kill(pid, SIGKILL);
        printf("killed\n");
        wait(&pid);
    }
    printf("tests done\n");
}

// sigaction test
void sigaction_test()
{
    printf("sigaction_test\n", handler1, handler2);
    struct sigaction signal_action1;
    struct sigaction signal_action2;
    struct sigaction signal_action3;
    signal_action1.sa_handler = handler2;
    signal_action1.sigmask = (1 << 5);
    signal_action2.sa_handler = 0;
    signal_action2.sigmask = 0;
    signal_action3.sa_handler = 0;
    signal_action3.sigmask = 0;
    sigaction(2, &signal_action1, &signal_action2);
    if (signal_action2.sa_handler == SIG_DFL && signal_action2.sigmask == 0)
        printf("test 1: passed\n");
    else
        printf("test 1: failed\n");

    kill(getpid(), 2);
    sleep(20);

    sigaction(2, &signal_action2, &signal_action3);
    if (signal_action3.sa_handler == handler2 && signal_action3.sigmask == (1 << 5))
        printf("test 3: passed\n");
    else
        printf("test 3: failed\n");

    exit(0);
}

int main(void)
{
    sigprocmask_test();
    // kernel_signal_test();
    // signalmask_test();
    // sigaction_test();

    exit(0);
}