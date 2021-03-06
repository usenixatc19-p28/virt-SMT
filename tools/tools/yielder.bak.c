#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>


pthread_t yielders[32];
int yielder_args[32];
struct counter_construct{
		unsigned int counter;
		char		pad[64-sizeof(unsigned int)];
}counters[32];
int quit = 0;

void *yielder(void *targs) {
	int index, i, ret;
	pthread_t myself = pthread_self();
	struct sched_param param;

	index = *((int *)targs);
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(index, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	param.sched_priority = 0;
	pthread_setschedparam(myself, SCHED_IDLE, &param);
	counters[index].counter = 0;
	while (!quit){
		ret = sched_yield();
		if(ret==-1)
			printf("error\n");
		counters[index].counter++;
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
	sleep(200);
	quit = 1;
	for(i=0;i<nr_yielders;i++) {
		pthread_join(yielders[i], NULL);
		total_counter += counters[i].counter;
	}
	
	printf("%lu\n", total_counter/nr_yielders/20);
	return 0;
}
