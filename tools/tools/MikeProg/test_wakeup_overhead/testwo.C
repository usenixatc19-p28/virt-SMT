#include <iostream>
#include <utility>
#include <thread>
#include <chrono>
#include <functional>
#include <atomic>
#include <mutex>

#include <string.h>

#include <pthread.h>

long int sleeptime = 10;
long int num_iter = 1000;
long int onecore = 0;

std::mutex L;

// RW shared variables
volatile long int iter = 1; // start at 1 because we will overshoot by 1 (the waiting thread will reenter the loop before iter is incremented by the active thread)
volatile int active_thread_num = 1; // start as if thread 1 just ended an iteration

void sleep_by_adding(void) {int i=0; while(i < sleeptime) ++i;}
 
// We want to test the sensitivity of execution time to thread waiting time.
//   Two threads will pass a single lock back and forth, and we'll measure the performance based on the think time of the lock holding thread.

void f(int thread_num)
{
  pthread_t pthr = pthread_self(); // std::this_thread::native_handle();
  cpu_set_t cpuset;

  if(pthread_getaffinity_np(pthr, sizeof(cpuset), &cpuset)) {
    std::cerr << "Call to pthread_getaffinity_np() failed." << std::endl;
  }
  CPU_ZERO(&cpuset);
  CPU_SET( ( onecore ? 1 : thread_num ), &cpuset); // all run on core one if onecore!=0
  if(pthread_setaffinity_np(pthr, sizeof(cpuset), &cpuset)) {
    std::cerr << "Call to pthread_setaffinity_np() failed." << std::endl;
  }


  while(iter < num_iter) {
    // wait until next lock holder has lock
    while(active_thread_num == thread_num) {
      // if(onecore)
	std::this_thread::yield();
    }

    // grab lock
    L.lock();

    // update shared variables
    active_thread_num = thread_num;
    ++ iter;

    // sleep
    // std::this_thread::sleep_for(std::chrono::microseconds(sleeptime));
    sleep_by_adding();

    // release lock
    L.unlock();
  }
}
 
int main(int argc, char* argv[])
{
  if(argc == 4) {
    sleeptime = strtol(argv[1], nullptr, 0);
    num_iter = strtol(argv[2], nullptr, 0);
    onecore = strtol(argv[3], nullptr, 0);
  } else {
    std::cout << "usage: " << argv[0] << " <sleeptime> <iterations> <onecore(0|1)" << std::endl;
    std::cout << "\t(invoke as \"testwo_baseline\" to get a non-threaded run." <<  std::endl;
    return 0;
  }

  if(strncmp("testwo_baseline", argv[0], 20) != 0) {
    std::thread t1(f, 1); 
    std::thread t2(f, 2); 
    t1.join();
    t2.join();
  } else {
    // Do a baseline measurement
    while(iter < num_iter) {
      ++ iter;
      sleep_by_adding();
    }
  }

  std::cout << "Iterations: " << (iter-1) << std::endl;
}
