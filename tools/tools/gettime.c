#define _GNU_SOURCE
#include<stdio.h>
#include<string.h>
#include<pthread.h>
#include<stdlib.h>
#include<unistd.h>
#include <time.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <errno.h>

pthread_t tid;
int counter;
pthread_mutex_t lock;
struct timespec gettime_start, gettime_end;



void* doSomeThing(void *arg)
{
    int t;
    cpu_set_t cpuset;
    pthread_t thread;

    thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    pthread_mutex_lock(&lock);
    t = clock_gettime(CLOCK_MONOTONIC_RAW, &gettime_end);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    int i = 0, t;
    int err;

    cpu_set_t cpuset;
    pthread_t thread;

    thread = pthread_self();
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    pthread_mutex_lock(&lock);
    err = pthread_create(&tid, NULL, &doSomeThing, NULL);
    if (err != 0)
        printf("\ncan't create thread :[%s]", strerror(err));

    sleep(1);
    t = clock_gettime(CLOCK_MONOTONIC_RAW, &gettime_start);
    pthread_mutex_unlock(&lock);

    pthread_join(tid, NULL);

    printf("t1 is %ld ns\n", gettime_start.tv_sec * 1000000000 + gettime_start.tv_nsec);
    printf("t2 is %ld ns\n", gettime_end.tv_sec * 1000000000 + gettime_end.tv_nsec);

    printf("time is %ld ns\n", (gettime_end.tv_sec - gettime_start.tv_sec)* 1000000000
			      + (gettime_end.tv_nsec - gettime_start.tv_nsec));

    pthread_mutex_destroy(&lock);

    return 0;
}
