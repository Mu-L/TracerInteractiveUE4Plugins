/*
    Copyright 2005-2012 Intel Corporation.  All Rights Reserved.

    The source code contained or described herein and all documents related
    to the source code ("Material") are owned by Intel Corporation or its
    suppliers or licensors.  Title to the Material remains with Intel
    Corporation or its suppliers and licensors.  The Material is protected
    by worldwide copyright laws and treaty provisions.  No part of the
    Material may be used, copied, reproduced, modified, published, uploaded,
    posted, transmitted, distributed, or disclosed in any way without
    Intel's prior express written permission.

    No license under any patent, copyright, trade secret or other
    intellectual property right is granted to or conferred upon you by
    disclosure or delivery of the Materials, either expressly, by
    implication, inducement, estoppel or otherwise.  Any license under such
    intellectual property rights must be express and approved by Intel in
    writing.
*/

#define HARNESS_CUSTOM_MAIN 1
#define HARNESS_NO_PARSE_COMMAND_LINE 1

#include <cstdlib>
#include <cmath>
#include <queue>
#include "tbb/tbb_stddef.h"
#include "tbb/spin_mutex.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/tick_count.h"
#include "tbb/cache_aligned_allocator.h"
#include "tbb/concurrent_priority_queue.h"
#include "../test/harness.h"
#pragma warning(disable: 4996)

#define IMPL_SERIAL 0
#define IMPL_STL 1
#define IMPL_CPQ 2

using namespace tbb;

// test parameters & defaults
int impl; // which implementation to test
int contention = 1; // busywork between operations in us
int preload = 0; // # elements to pre-load queue with
double throughput_window = 30.0; // in seconds
int ops_per_iteration = 20; // minimum: 2 (1 push, 1 pop)
const int sample_operations = 1000; // for timing checks
int min_threads = 1;
int max_threads;

// global data & types
int pushes_per_iter;
int pops_per_iter;
tbb::atomic<unsigned int> operation_count;
tbb::tick_count start;

// a non-trivial data element to use in the priority queue
const int padding_size = 15;  // change to get cache line size for test machine
class padding_type {
public:
    int p[padding_size];
    padding_type& operator=(const padding_type& other) {
        if (this != &other) {
            for (int i=0; i<padding_size; ++i) {
                p[i] = other.p[i];
            }
        }
        return *this;
    }
};

class my_data_type {
public:
    int priority;
    padding_type padding;
    my_data_type() : priority(0) {}
};

class my_less {
public:
    bool operator()(my_data_type d1, my_data_type d2) {
        return d1.priority<d2.priority;
    }
};

// arrays to get/put data from/to to generate non-trivial accesses during busywork
my_data_type *input_data;
my_data_type *output_data;
size_t arrsz;

// Serial priority queue
std::priority_queue<my_data_type, std::vector<my_data_type>, my_less > *serial_cpq;

// Coarse-locked priority queue
spin_mutex *my_mutex;
std::priority_queue<my_data_type, std::vector<my_data_type>, my_less > *stl_cpq;

// TBB concurrent_priority_queue
concurrent_priority_queue<my_data_type, my_less > *agg_cpq;

// Busy work and calibration helpers
unsigned int one_us_iters = 345; // default value

// if user wants to calibrate to microseconds on particular machine, call 
// this at beginning of program; sets one_us_iters to number of iters to 
// busy_wait for approx. 1 us
void calibrate_busy_wait() {
    tbb::tick_count t0, t1;

    t0 = tbb::tick_count::now();
    for (volatile unsigned int i=0; i<1000000; ++i) continue;
    t1 = tbb::tick_count::now();
    
    one_us_iters = (unsigned int)((1000000.0/(t1-t0).seconds())*0.000001);
    printf("one_us_iters: %d\n", one_us_iters);
}

void busy_wait(int us)
{
    unsigned int iter = us*one_us_iters;
    for (volatile unsigned int i=0; i<iter; ++i) continue;
}

// Push to priority queue, depending on implementation
void do_push(my_data_type elem, int nThr, int impl) {
    if (impl == IMPL_SERIAL) {
        serial_cpq->push(elem);
    }
    else if (impl == IMPL_STL) {
        tbb::spin_mutex::scoped_lock myLock(*my_mutex);
        stl_cpq->push(elem);
    }
    else if (impl == IMPL_CPQ) {
        agg_cpq->push(elem);
    }
}

// Pop from priority queue, depending on implementation
my_data_type do_pop(int nThr, int impl) {
    my_data_type elem;
    if (impl == IMPL_SERIAL) {
        if (!serial_cpq->empty()) {
            elem = serial_cpq->top();
            serial_cpq->pop();
            return elem;
        }
    }
    else if (impl == IMPL_STL) {
        tbb::spin_mutex::scoped_lock myLock(*my_mutex);
        if (!stl_cpq->empty()) {
            elem = stl_cpq->top();
            stl_cpq->pop();
            return elem;
        }
    }
    else if (impl == IMPL_CPQ) {
        if (agg_cpq->try_pop(elem)) {
            return elem;
        }
    }
    return elem;
}


struct TestThroughputBody : NoAssign {
    int nThread;
    int implementation;

    TestThroughputBody(int nThread_, int implementation_) : 
        nThread(nThread_), implementation(implementation_) {}
    
    void operator()(const int threadID) const {
        tbb::tick_count now;
        int pos_in = threadID, pos_out = threadID;
        my_data_type elem;
        while (1) {
            for (int i=0; i<sample_operations; i+=ops_per_iteration) {
                // do pushes
                for (int j=0; j<pushes_per_iter; ++j) {
                    elem = input_data[pos_in];
                    do_push(elem, nThread, implementation);
                    busy_wait(contention);
                    pos_in += nThread;
                    if (pos_in >= arrsz) pos_in = pos_in % arrsz;
                }
                // do pops
                for (int j=0; j<pops_per_iter; ++j) {
                    output_data[pos_out] = do_pop(nThread, implementation);
                    busy_wait(contention);
                    pos_out += nThread;
                    if (pos_out >= arrsz) pos_out = pos_out % arrsz;
                }
            }
            now = tbb::tick_count::now();
            operation_count += sample_operations;
            if ((now-start).seconds() >= throughput_window) break;
        }
    }
};

void TestSerialThroughput() {
    tbb::tick_count now;

    serial_cpq = new std::priority_queue<my_data_type, std::vector<my_data_type>, my_less >;        
    for (int i=0; i<preload; ++i) do_push(input_data[i], 1, IMPL_SERIAL);

    TestThroughputBody my_serial_test(1, IMPL_SERIAL);
    start = tbb::tick_count::now();
    NativeParallelFor(1, my_serial_test);
    now = tbb::tick_count::now();
    delete serial_cpq;

    printf("SERIAL 1 %10d\n", int(operation_count/(now-start).seconds()));
}

void TestThroughputCpqOnNThreads(int nThreads) {
    tbb::tick_count now;

    if (impl == IMPL_STL) {
        stl_cpq = new std::priority_queue<my_data_type, std::vector<my_data_type>, my_less >;
        for (int i=0; i<preload; ++i) do_push(input_data[i], nThreads, IMPL_STL);

        TestThroughputBody my_stl_test(nThreads, IMPL_STL);
        start = tbb::tick_count::now();
        NativeParallelFor(nThreads, my_stl_test);
        now = tbb::tick_count::now();
        delete stl_cpq;
        
        printf("STL  %3d %10d\n", nThreads, int(operation_count/(now-start).seconds()));
    }
    else if (impl == IMPL_CPQ) {
        agg_cpq = new concurrent_priority_queue<my_data_type, my_less >;
        for (int i=0; i<preload; ++i) do_push(input_data[i], nThreads, IMPL_CPQ);

        TestThroughputBody my_cpq_test(nThreads, IMPL_CPQ);
        start = tbb::tick_count::now();
        NativeParallelFor(nThreads, my_cpq_test);
        now = tbb::tick_count::now();
        delete agg_cpq;
        
        printf("CPQ  %3d %10d\n", nThreads, int(operation_count/(now-start).seconds()));
    }
}

void printCommandLineErrorMsg() {
    fprintf(stderr,
            "Usage: a.out <min_threads>[:<max_threads>] "
            "contention(us) queue_type pre-load batch duration"
            "\n   where queue_type is one of 0(SERIAL), 1(STL), 2(CPQ).\n");
}

void ParseCommandLine(int argc, char *argv[]) {
    // Initialize defaults
    max_threads = 1;
    impl = IMPL_SERIAL;
    int i = 1;
    if (argc > 1) {
        // read n_thread range
        char* endptr;
        min_threads = strtol( argv[i], &endptr, 0 );
        if (*endptr == ':')
            max_threads = strtol( endptr+1, &endptr, 0 );
        else if (*endptr == '\0')
            max_threads = min_threads;
        if (*endptr != '\0') {
            printCommandLineErrorMsg();
            exit(1);
        }
        if (min_threads < 1) {
            printf("ERROR: min_threads must be at least one.\n");
            exit(1);
        }
        if (max_threads < min_threads) {
            printf("ERROR: max_threads should not be less than min_threads\n");
            exit(1);
        }
        ++i;
        if (argc > 2) {
            // read contention
            contention = strtol( argv[i], &endptr, 0 );
            if( *endptr!='\0' ) {
                printf("ERROR: contention is garbled\n");
                printCommandLineErrorMsg();
                exit(1);
            }
            ++i;
            if (argc > 3) {
                // read impl
                impl = strtol( argv[i], &endptr, 0 );
                if( *endptr!='\0' ) {
                    printf("ERROR: impl is garbled\n");
                    printCommandLineErrorMsg();
                    exit(1);
                }
                if ((impl != IMPL_SERIAL) && (impl != IMPL_STL) && (impl != IMPL_CPQ)) {
                    
                    printf("ERROR: impl of %d is invalid\n", impl);
                    printCommandLineErrorMsg();
                    exit(1);
                }
                ++i;
                if (argc > 4) {
                    // read pre-load
                    preload = strtol( argv[i], &endptr, 0 );
                    if( *endptr!='\0' ) {
                        printf("ERROR: pre-load is garbled\n");
                        printCommandLineErrorMsg();
                        exit(1);
                    }
                    ++i;
                    if (argc > 5) {
                        //read batch
                        ops_per_iteration = strtol( argv[i], &endptr, 0 );
                        if( *endptr!='\0' ) {
                            printf("ERROR: batch size is garbled\n");
                            printCommandLineErrorMsg();
                            exit(1);
                        }
                        ++i;
                        if (argc > 6) {
                            // read duration
                            if (argc != 7)  {
                                printf("ERROR: maximum of six args\n");
                                printCommandLineErrorMsg();
                                exit(1);
                            }
                            throughput_window = strtol( argv[i], &endptr, 0 );
                            if( *endptr!='\0' ) {
                                printf("ERROR: duration is garbled\n");
                                printCommandLineErrorMsg();
                                exit(1);
                            }
                        }
                    }
                }
            }
        }
    }
    printf("Priority queue performance test %d will run with %dus contention "
           "using %d:%d threads, %d batch size, %d pre-loaded elements, for %d seconds.\n",
           (int)impl, (int)contention, (int)min_threads, (int)max_threads, 
           (int)ops_per_iteration, (int) preload, (int)throughput_window);
}

int main(int argc, char *argv[]) {
    ParseCommandLine(argc, argv);
    srand(42);
    arrsz = 100000;
    input_data = new my_data_type[arrsz];
    output_data = new my_data_type[arrsz];
    for (int i=0; i<arrsz; ++i) {
       input_data[i].priority = rand()%100;
    }
    //calibrate_busy_wait();
    pushes_per_iter = ops_per_iteration/2;
    pops_per_iter = ops_per_iteration/2;
    operation_count = 0;

    // Initialize mutex for Coarse-locked priority_queue
    cache_aligned_allocator<spin_mutex> my_mutex_allocator;
    my_mutex = (spin_mutex *)my_mutex_allocator.allocate(1);

    if (impl == IMPL_SERIAL) {
        TestSerialThroughput();
    }
    else {
        for (int p = min_threads; p <= max_threads; ++p) {
            TestThroughputCpqOnNThreads(p);
        }
    }
    return Harness::Done;
}
