#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <libcgroup.h>
#include <pthread.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <errno.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>




pthread_t yielders[32];
int yielder_args[32];
struct counter_construct{
		unsigned int counter;
		char		pad[64-sizeof(unsigned int)];
}counters[32];
int quit = 0;

void *yielder(void *targs) {
	int index, i = 0, ret;
	pthread_t myself = pthread_self();
	struct sched_param param;
	struct timespec gettime_start, gettime_end;
	int fd;
	long len = 0, t, time = 0;

	fd=open("/proc/myprocfile", O_WRONLY);

	index = *((int *)targs);
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(index, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	param.sched_priority = 0;
	pthread_setschedparam(myself, SCHED_IDLE, &param);
	counters[index].counter = 0;
	while (!quit){
		i++;
		len += write(fd, &fd, 8192);
		if(i%1000==0) {
	    	        t = clock_gettime(CLOCK_MONOTONIC_RAW, &gettime_end);
			time = time + ((gettime_end.tv_sec - gettime_start.tv_sec)* 1000000000
				      + (gettime_end.tv_nsec - gettime_start.tv_nsec));
			if(len==0) continue;
  			printf("time is %ld ns\n", time/len);
			time = 0;
			len = 0;
	    	        t = clock_gettime(CLOCK_MONOTONIC_RAW, &gettime_start);
		}
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
