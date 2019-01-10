#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
 
#define LOOPS 50000000
 
pthread_spinlock_t spinlock;
 
//Get the thread id
pid_t gettid() { return syscall( __NR_gettid ); }
 
void *consumer(void *ptr)
{
    int i;
 
    printf("Consumer TID %lun", (unsigned long)gettid());
 
    pthread_spin_lock(&spinlock);
 
}
 
int main()
{
    int i;
    pthread_t thr[17];
    struct timeval tv1, tv2;
 
    pthread_spin_init(&spinlock, 0);
 
    for(i=0;i<17;i++)
        pthread_create(&thr[i], NULL, consumer, NULL);
 
    for(i=0;i<17;i++)
        pthread_join(thr[i], NULL);
 
    pthread_spin_destroy(&spinlock);
 
    return 0;
}
