/*
 *command to compile this source code: 
 *gcc -o controller controller.c -lpthread -lcgroup -lrt
 */
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

#define CT_BENCHMARK "bench"
#define CT_YIELDER "yielder"
#define NR_YIELDERS 16

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

//#define COLLECT_TRACE

#ifdef COLLECT_TRACE
FILE *f_log = NULL;
#endif

struct cgroup *bench_group;
struct cgroup *yielder_group;
struct cgroup *root_group;
pthread_t yielders[32];
struct yielder_arg_struct{
	int index;
	int should_sleep;
	pthread_mutex_t lock;
	pthread_cond_t cond;
#ifdef USE_YIELD_COUNTER
	unsigned long counter;
	char pad[128 - sizeof(pthread_mutex_t) - sizeof(pthread_cond_t) - sizeof(unsigned long) - 2 * sizeof(int)];  //do not ping pong cache line
#else
	char pad[128 - sizeof(pthread_mutex_t) - sizeof(pthread_cond_t) - 2 * sizeof(int)];  //do not ping pong cache line
#endif

}yielder_args[32];

int quit = 0;
int nr_active_cores=NR_YIELDERS;

#define BUFLEN 4000
unsigned char buffer[BUFLEN];

void *yielder(void *targs) {
	struct yielder_arg_struct *yielder_arg = (struct yielder_arg_struct *) targs;
	pthread_mutex_t		*lock = &(yielder_arg->lock);
	pthread_cond_t		*cond = &(yielder_arg->cond);
	int index = yielder_arg->index;
	int *should_sleep = &(yielder_arg->should_sleep);
#ifdef USE_YIELD_COUNTER
	unsigned long *counter = &(yielder_arg->counter);
#endif
	int ret;
	struct sched_param param;
	pthread_t myself = pthread_self();

	//attach this yielder to yielder cgroup
	ret = cgroup_attach_task(yielder_group);
	//ret = cgroup_attach_task(bench_group);
	if(ret != 0) {
		fprintf(stderr, "Yielder #%d cannot join the yielder cgroup.\n", index);
		quit = 1;
	}

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(index, &cpuset);
	ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if(ret != 0 ) {
		fprintf(stderr, "Yielder #%d cannot set affinity.\n", index);
		quit = 1;
	}
	param.sched_priority = 0;
	if (pthread_setschedparam(myself, SCHED_IDLE, &param)!= 0){
		perror("setschedparam");
		quit = 1;
	}

	while (!quit) {
		sched_yield();
#ifdef USE_YIELD_COUNTER
		*counter = *counter + 1;
#endif
		if (unlikely(*should_sleep)) {
			pthread_mutex_lock(lock);
			if(*should_sleep)
				pthread_cond_wait(cond,lock);
			pthread_mutex_unlock(lock);
		}
	}
}

int create_yielders()
{//create yielding threads
	int i;

	for(i=0;i<NR_YIELDERS;i++) {
		yielder_args[i].index = i;
		yielder_args[i].should_sleep = 0;
#ifdef USE_YIELD_COUNTER
		yielder_args[i].counter = 0;
#endif
		pthread_mutex_init(&(yielder_args[i].lock), NULL);
		pthread_cond_init(&(yielder_args[i].cond), NULL);
		pthread_create(&yielders[i], NULL, yielder, &yielder_args[i]);
	}
}

static int copy_string_from_parent(struct cgroup_controller *controller,
				   struct cgroup_controller *pcont, const char *file)
{
	char *ptr = NULL;
	int ret;

	ret = cgroup_get_value_string(pcont, file, &ptr);
	if (ret)
		goto out;
	ret = cgroup_set_value_string(controller, file, ptr);
out:
	free(ptr);
	return ret;
}

static int controller_apply_config(struct cgroup *ct, struct cgroup *parent,
				   struct cgroup_controller *controller,
				   const char *name)
{
	int ret;
	if (!strcmp("cpuset", name)) {
		struct cgroup_controller *pcont = cgroup_get_controller(parent, name);
		if (!pcont)
			return 0;

		if ((ret = copy_string_from_parent(controller, pcont, "cpuset.cpus")))
			return ret;

		if ((ret = copy_string_from_parent(controller, pcont, "cpuset.mems")))
			return ret;

	} 
/* else if (!strcmp("memory", name)) {
		if ((ret = cgroup_set_value_string(controller, "memory.use_hierarchy", "1")))
			return ret;
	} else if (!strcmp("devices", name)) {
		if ((ret = cgroup_set_value_string(controller, "devices.deny", "a")))
			return ret;
	} 
*/	
	return 0;
}

static int do_create_cgroup(struct cgroup *ct, struct cgroup *parent)
{
	struct cgroup_mount_point mnt;
	struct cgroup_controller *controller;
	void *handle;
	int ret;

	ret = cgroup_get_controller_begin(&handle, &mnt);

	do {
		controller = cgroup_add_controller(ct, mnt.name);
		ret = controller_apply_config(ct, parent, controller, mnt.name);
		if (!ret)
			ret = cgroup_get_controller_next(&handle, &mnt);
	} while (!ret);

	cgroup_get_controller_end(&handle);

	if (ret == ECGEOF)
		ret = cgroup_create_cgroup(ct, 0);

	return ret;
}

int create_cgroups()
{
	int ret;

	root_group = cgroup_new_cgroup("/");
	cgroup_get_cgroup(root_group);

	bench_group = cgroup_new_cgroup(CT_BENCHMARK);
	yielder_group = cgroup_new_cgroup(CT_YIELDER);
	ret = do_create_cgroup(bench_group, root_group);
	if (ret!=0)
		fprintf(stderr, "Err code:%d\nMsg: %s.\n",ret, cgroup_strerror(ret));

	ret = do_create_cgroup(yielder_group, root_group);
	if (ret!=0)
		fprintf(stderr, "Err code:%d\nMsg: %s.\n",ret, cgroup_strerror(ret));

	cgroup_free_controllers(bench_group);
//	cgroup_free(&bench_group);
	cgroup_free_controllers(yielder_group);
//	cgroup_free(&yielder_group);
	return ret;
}

FILE * f_cpuset_cpus = NULL;
FILE * f_cpuacct_usage = NULL;

int set_cpushares(void)
{
	FILE *f_cpu_shares;

	f_cpu_shares = fopen("/sys/fs/cgroup/cpu/bench/cpu.shares", "r+e");
	if(f_cpu_shares == NULL) {
		fprintf(stderr, "Cannot open cpu.shares cgroup file for cgroup bench.\n");
		return 1;
	}

	fprintf(stderr,"maximize cpu.shares of cgroup bench.\n");
	if (fprintf(f_cpu_shares, "%d", 262144) < 0) {
		return 1;
	}
	fclose(f_cpu_shares);

	f_cpu_shares = fopen("/sys/fs/cgroup/cpu/yielder/cpu.shares", "r+e");
	if(f_cpu_shares == NULL) {
		fprintf(stderr, "Cannot open cpu.shares cgroup file for cgroup yielders.\n");
		return 1;
	}

	fprintf(stderr,"minimizing cpushares of cgroup yielder.\n");
	if (fprintf(f_cpu_shares, "%d", 2) < 0) {
		return 1;
	}
	fclose(f_cpu_shares);
	return;
}

/*
 * Initialize libcgroup and mount the controllers
 *
 * Returns 0 on success, -1 otherwise.
 */
int initialize()
{
	void *handle;
	struct controller_data info;
	pid_t ppid, pid;
	int ret;

#ifdef COLLECT_TRACE
	f_log = fopen("/tmp/controller.log","w+");
#endif
	cgroup_init();
	create_cgroups();


	// add the shell into the benchmark cgroup
	ppid = getppid();
//	bench_group = cgroup_new_cgroup(CT_BENCHMARK);
	ret = cgroup_get_cgroup(bench_group);
	if (ret!=0) {
		fprintf(stderr, "Cannot get bench cgroup\n");
		fprintf(stderr, "Err code:%d\nMsg: %s.\n",ret, cgroup_strerror(ret));
		quit = 1;
	}

	ret = cgroup_attach_task_pid(bench_group, ppid);
	if(ret != 0) {
		fprintf(stderr, "cannot attach the shell %d to the benchmark cgroup.\n", ppid);
		return 1;
	}


	// add controller yielding threads into the yielder cgroup
//	yielder_group = cgroup_new_cgroup(CT_YIELDER);
	ret = cgroup_get_cgroup(yielder_group);
	if (ret!=0) {
		fprintf(stderr, "Cannot get yielder cgroup\n");
		fprintf(stderr, "Err code:%d\nMsg: %s.\n",ret, cgroup_strerror(ret));
		quit = 1;
	}

	ret = cgroup_attach_task(bench_group);
	if(ret != 0) {
		fprintf(stderr, "cannot attach the controller to the yielder_cgroup.\n");
		return 1;
	}

	create_yielders();

/*
 * No need to call cgroup_modify_cgroup here. cgroup_attach_tasks directly opens cgroup
 * files and changes contents. But changing other cgroup files may need to directly handle files.
 */
	f_cpuset_cpus = fopen("/sys/fs/cgroup/cpuset/bench/cpuset.cpus", "r+e");
	if(f_cpuset_cpus == NULL) {
		fprintf(stderr, "Cannot open cpuset.cpus cgroup file.\n");
		return 1;
	}

	f_cpuacct_usage = fopen("/sys/fs/cgroup/cpuacct/bench/cpuacct.usage", "r");
	if(f_cpuacct_usage == NULL) {
		fprintf(stderr, "Cannot open cpuset.cpus cgroup file.\n");
		return 1;
	}

	if(set_cpushares())
		return 1;
	return 0;
}

void sig_handler(int signo)
{
	if (signo == SIGHUP) {
		fprintf(stderr, "received SIGHUP. Exiting...\n");
		quit = 1;
	}
}

/*
 * FIXME: This is a hack required by libcgroup. At some point in time, this can go away.
 *
 * Problem is that libcgroup works with buffered writes. If we write to a cgroup file and 
 * want it to be seen in the filesystem, we need to call cgroup_modify_cgroup().
 *
 * However, all versions up to 0.38 will fail that operation for already existent cgroups, 
 * due to a bug in the way they handle modifications in the presence of read-only files 
 * (whether or not that specific file was being modified). Because of that, we need to bypass
 * libcgroup to commit changes.
 */

int change_cpuset_cpus()
{
	char buff[64];

	if(nr_active_cores == 1){
		buff[0]='0';
		buff[1]=0;
	}
	else
		sprintf(buff, "0-%d", nr_active_cores - 1);

	if (fprintf(f_cpuset_cpus, "%s", buff) < 0) {
		return 1;
	}
	fflush(f_cpuset_cpus);
	return 0;
}

int reduce_active_cores(int delta)
{
	struct yielder_arg_struct *p_yielder;
	int i = 0;

	if(unlikely(nr_active_cores <= delta))
		delta = nr_active_cores - 1;

	//put yielders into sleep
	while( i < delta ){
		nr_active_cores--;
		i++;
		p_yielder = yielder_args + nr_active_cores;
		pthread_mutex_lock(&p_yielder->lock);
		p_yielder->should_sleep = 1;
		pthread_mutex_unlock(&p_yielder->lock);
	}

	change_cpuset_cpus();
}

int increase_active_cores(int delta)
{
	struct yielder_arg_struct *p_yielder;
	int i = 0;

	if(unlikely(nr_active_cores + delta > NR_YIELDERS))
		delta = NR_YIELDERS - nr_active_cores;

	//wakeup yielders from sleep
	while( i < delta ){
		p_yielder = yielder_args + nr_active_cores;
		pthread_mutex_lock(&p_yielder->lock);
		p_yielder->should_sleep = 0;
		pthread_cond_signal(&p_yielder->cond);
		pthread_mutex_unlock(&p_yielder->lock);
		nr_active_cores++;
		i++;
	}

	change_cpuset_cpus();
}

/* in microseconds */
unsigned long get_cpuacct_usage()
{
	unsigned long usage;

	f_cpuacct_usage = fopen("/sys/fs/cgroup/cpuacct/bench/cpuacct.usage", "r");
	if(f_cpuacct_usage == NULL) {
		fprintf(stderr, "Cannot open cpuset.cpus cgroup file.\n");
		return 1;
	}

	if(fscanf(f_cpuacct_usage, "%lu", &usage)!=1) {
		fprintf(stderr, "cannot read cpuacct.usage.\n");
		quit = 1;
	}
	fclose(f_cpuacct_usage);
	return usage;
}

/* old implementation. High cost
unsigned long get_cpuacct_usage() 
{
	unsigned long usage;
	struct cgroup_controller *bench_cg_ctrl;

	cgroup_free_controllers(bench_group);
	ret = cgroup_get_cgroup(bench_group);
	bench_cg_ctrl = cgroup_get_controller(bench_group, "cpuacct");
	ret = cgroup_get_value_uint64(bench_cg_ctrl, "cpuacct.usage", &usage);
	if( ret != 0 ) {
		fprintf(stderr, "cannot get cpu usage information.\n");
		fprintf(stderr, "Err code:%d\nMsg: %s.\n",ret, cgroup_strerror(ret));
		quit = 1;
	}

	return usage;
}
*/

void get_total_cs(unsigned long *total_cs)
{
    int fid;
    ssize_t size;
    unsigned char *pos;

    fid = open("/proc/stat", O_RDONLY);
    size = read(fid, buffer, BUFLEN);
    buffer[size] = 0;
    pos = strstr(buffer, "ctxt");
    pos += 5; 
    sscanf(pos, "%lu", total_cs);
    close(fid);
}


/*
 * The main body of the controller algorithm
 */
void controller_alg()
{
    int ret;
    unsigned long usage, last_usage, yielder_usage;
    int i;
    unsigned long buf1[NR_YIELDERS], buf2[NR_YIELDERS];
#ifdef USE_YIELD_COUNTER
    unsigned long counter, *prev_counters=buf1, *curr_counters=buf2, *tmp;
#endif
    struct sched_param param;
    int origpolicy;
    struct timespec timestamp, last_timestamp;
    float wall_time; //in microseconds
    int adjusting = 0;
    static sample = 0;
    struct rusage ru;
    long last_cs = 0, cs, yielder_cs;
    unsigned long last_total_cpu_time, total_cpu_time, overall_cpu_time, app_cpu_time;
    unsigned long last_total_ctxt, total_ctxt, overall_ctxt;
    float idle_gran, comp_gran, comp_len;
    unsigned int duration = 1;

    origpolicy = sched_getscheduler((pid_t) 0);
    sched_getparam ((pid_t) 0, &param);
    param.sched_priority = sched_get_priority_max(origpolicy);
    if( sched_setscheduler((pid_t) 0, origpolicy, &param ) == -1 ) {
        fprintf(stderr,"Cannot set scheduler/param of the controller ... are you root?\n");
        perror("msg:");
        exit(1);
    }

#ifdef USE_YIELD_COUNTER
    for( i = 0; i < NR_YIELDERS; i++) buf1[i] = yielder_args[i].counter;
    prev_counters = buf1;
    curr_counters = buf2;
#endif
//  last_usage=get_cpuacct_usage();
    ret = getrusage(RUSAGE_SELF, &ru);
    if(ret < 0) {
        perror("getrusage:");
    }
    else
        last_usage = ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec+
                     ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;

    last_cs = ru.ru_nivcsw + ru.ru_nvcsw;
    ret = clock_gettime(CLOCK_MONOTONIC_RAW, &last_timestamp);
    get_total_cs(&total_ctxt);
    last_total_cpu_time = total_cpu_time;
    last_total_ctxt = total_ctxt;
    last_total_cpu_time = get_cpuacct_usage();

    while(!quit) {
        usleep(4000);
        total_cpu_time=get_cpuacct_usage(); 
        ret = getrusage(RUSAGE_SELF, &ru);
        if(ret < 0) 
            perror("getrusage:");
        else
            usage = ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec+
                    ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;

        cs = ru.ru_nivcsw + ru.ru_nvcsw;
        ret = clock_gettime(CLOCK_MONOTONIC_RAW, &timestamp);

        wall_time = (timestamp.tv_sec - last_timestamp.tv_sec) * 1000000.0 
                    + (timestamp.tv_nsec - last_timestamp.tv_nsec) / 1000.0;

        get_total_cs(&total_ctxt);
        overall_cpu_time = (total_cpu_time - last_total_cpu_time) / 1000;
        app_cpu_time = overall_cpu_time;
        overall_ctxt = total_ctxt - last_total_ctxt;

#ifdef COLLECT_TRACE
//		if(((sample++)%1000) == 0) 
//			fprintf(f_log, "cs: %ld, usage: %lu, last_usage: %lu, wall_time: %f\n", 
//				cs - last_cs, usage, last_usage, wall_time);
//		fprintf(f_log, "cs: %ld\n", cs - last_cs);
#endif

        yielder_usage = usage - last_usage;
	yielder_cs = cs - last_cs;
/*
        if(wall_time * nr_active_cores < curr_usage)
            curr_usage = 0;
        else
            curr_usage = wall_time * nr_active_cores - curr_usage;
*/

#ifdef COLLECT_TRACE
        fprintf(f_log, "%lu %f %ld %lu %lu\n", yielder_usage, wall_time, yielder_cs, overall_cpu_time, overall_ctxt);
#endif

	if(yielder_cs < nr_active_cores)
            idle_gran = 4000;
        else
            idle_gran = yielder_usage * 1.0 / yielder_cs;
        if( overall_cpu_time <= yielder_usage ) {
            comp_gran = 0;
            comp_len = 0;
	}
	else {
            comp_gran =  app_cpu_time * 1.0 / (overall_ctxt - yielder_cs);
            comp_len = app_cpu_time * 1.0 / yielder_cs;
        }
#ifdef COLLECT_TRACE
        fprintf(f_log, "%f %f %f\n", idle_gran, comp_gran, comp_len);
#endif

// POLICIES BEGIN HERE
        if(comp_len > 1000){
            increase_active_cores(NR_YIELDERS);
            duration = 3;
            goto next_round;
        }

        if(comp_len > 0.9 * (comp_len + idle_gran) && nr_active_cores < NR_YIELDERS){
            increase_active_cores(4);
            duration = 3;
            goto next_round;
        }
        else if(comp_len > 0.8 * (comp_len + idle_gran) && nr_active_cores < NR_YIELDERS) {
            increase_active_cores(1);
            goto next_round;
        }
        if(comp_gran > idle_gran)
            goto next_round;

        if(comp_len < 0.7 * (comp_len + idle_gran) * (nr_active_cores -1)) {
            if(duration--==0)
                reduce_active_cores(1);
        }

next_round:
        last_usage = usage;
        last_timestamp = timestamp;
        last_cs = cs;
        last_total_cpu_time = total_cpu_time;
        last_total_ctxt = total_ctxt;
#ifdef USE_YIELD_COUNTER
        for(i=0; i<NR_YIELDERS; i++)
            curr_counters[i] = yielder_args[i].counter;
        counter = 0;
        for(i=0;i<NR_YIELDERS; i++)
            counter = counter + (curr_counters[i] - prev_counters[i]);

        fprintf(f_log, "%lu %lu\n", counter, counter/nr_active_cores);
        tmp = curr_counters;
        curr_counters = prev_counters;
        prev_counters = tmp;
#endif			
    }
}

main()
{
    pid_t pid;

    pid = fork();
    if( pid == 0 ) { /* this is the controller */
        if(initialize()){
            fprintf(stderr, "Initialization fails.\n");
            exit(1);
        }
        if (signal(SIGHUP, sig_handler) == SIG_ERR)
            printf("\ncan't catch SIGHUP\n");
            prctl(PR_SET_PDEATHSIG, SIGHUP);  //exit when the parent process terminates
            sleep(8);
            controller_alg();
    }
    else {  /* this is to launch benchmarks */
        printf("launching a shell...\n");
        sleep(2);
        execlp("/bin/bash", "bash", NULL);
    }
}
