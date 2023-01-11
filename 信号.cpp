#include <signal.h>
#include <unistd.h>
#include <stdio.h>
int stop = 0;
void sigcb(int signo) {
    switch (signo) {
    case SIGHUP:
        printf("Get a signal -- SIGHUP\n");
        break;
    case SIGINT:
        printf("Get a signal -- SIGINT\n");
        stop = 1;
        break;
    case SIGQUIT:
        printf("Get a signal -- SIGQUIT\n");
        break;
    }
    return;
}

int main() {
    signal(SIGHUP, sigcb);
    signal(SIGINT, sigcb);
    signal(SIGQUIT, sigcb);
    while(stop==0) {
        sleep(1);
    }
}