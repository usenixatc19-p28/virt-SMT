#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


pthread_t yielders[32];
int yielder_args[32];
struct counter_construct{
		unsigned int counter;
		char		pad[64-sizeof(unsigned int)];
}counters[32];
int quit = 0;

static inline void rep_nop(void)
//static void rep_nop(void)
{
	__asm__ __volatile__ ("pause");
//	asm volatile("rep; pause" ::: "memory");
}

void *yielder(void *targs) {
	int index, i, ret;
	pthread_t myself = pthread_self();
	struct sched_param param;
	int fd;

	fd=open("/proc/myprocfile", O_WRONLY);

	index = *((int *)targs);
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(index, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	param.sched_priority = 0;
	pthread_setschedparam(myself, SCHED_IDLE, &param);
	counters[index].counter = 0;
	while (1){
		write(fd, &i, 8192);
	}
}

int main(int argc, char **argv)
{
	int nr_yielders, i;
	unsigned long total_counter = 0;

	if(argc != 2 || sscanf(argv[1],"%d", &nr_yielders) != 1){
		fprintf(stderr, "%s #yielders\n", argv[0]);
		exit(0);
	}


	for(i=0;i<nr_yielders;i++) {
		yielder_args[i] = i;
		pthread_create(&yielders[i], NULL, yielder, &yielder_args[i]);
	}
	sleep(2000);
	quit = 1;
	for(i=0;i<nr_yielders;i++) {
		pthread_join(yielders[i], NULL);
		total_counter += counters[i].counter;
	}
	
	printf("%lu\n", total_counter/nr_yielders/20);
	return 0;
}
