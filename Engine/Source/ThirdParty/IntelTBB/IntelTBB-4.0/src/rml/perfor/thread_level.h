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

// Thread level recorder
#ifndef __THREAD_LEVEL_H
#define __THREAD_LEVEL_H
#include <cstdio>
#include <omp.h>
#include <assert.h>
#include "tbb/atomic.h"
#include "tbb/tick_count.h"

//#define LOG_THREADS // use this to ifdef out calls to this class 
//#define NO_BAIL_OUT // continue execution after detecting oversubscription

using namespace tbb;

typedef enum {tbb_outer, tbb_inner, omp_outer, omp_inner} client_t;

class ThreadLevelRecorder {
  tbb::atomic<int> tbb_outer_level;
  tbb::atomic<int> tbb_inner_level;
  tbb::atomic<int> omp_outer_level;
  tbb::atomic<int> omp_inner_level;
  struct record {
    tbb::tick_count time;
    int n_tbb_outer_thread;
    int n_tbb_inner_thread;
    int n_omp_outer_thread;
    int n_omp_inner_thread;
  };
  tbb::atomic<unsigned> next;
  /** Must be power of two */
  static const unsigned max_record_count = 1<<20;
  record array[max_record_count];
  int max_threads;
  bool fail;
 public:
  void change_level(int delta, client_t whichClient);
  void dump();
  void init();
};

void ThreadLevelRecorder::change_level(int delta, client_t whichClient) {
  int tox=tbb_outer_level, tix=tbb_inner_level, oox=omp_outer_level, oix=omp_inner_level;
  if (whichClient == tbb_outer) {
    tox = tbb_outer_level+=delta;
  } else if (whichClient == tbb_inner) {
    tix = tbb_inner_level+=delta;
  } else if (whichClient == omp_outer) {
    oox = omp_outer_level+=delta;
  } else if (whichClient == omp_inner) {
    oix = omp_inner_level+=delta;
  } else {
    printf("WARNING: Bad client type; ignoring.\n");
    return;
  }
  // log non-negative entries
  tbb::tick_count t = tbb::tick_count::now();
  unsigned k = next++;
  if (k<max_record_count) {
    record& r = array[k];
    r.time = t;
    r.n_tbb_outer_thread = tox>=0?tox:0;
    r.n_omp_outer_thread = oox>=0?oox:0;
    r.n_tbb_inner_thread = tix>=0?tix:0;
    r.n_omp_inner_thread = oix>=0?oix:0;
  }
  char errStr[100];
  int tot_threads;
  tot_threads = tox+tix+oox+oix;
  sprintf(errStr, "ERROR: Number of threads (%d+%d+%d+%d=%d) in use exceeds maximum (%d).\n", 
	  tox, tix, oox, oix, tot_threads, max_threads);
  if (tot_threads > max_threads) {
#ifdef NO_BAIL_OUT
    if (!fail) {
      printf("%sContinuing...\n", errStr);
      fail = true;
    }
#else
    dump();
    printf("%s\n", errStr);
    assert(tot_threads <= max_threads);
#endif
  }
}

void ThreadLevelRecorder::dump() {
  FILE* f = fopen("time.txt","w");
  if (!f) {
    perror("fopen(time.txt)\n");
    exit(1);
  }
  unsigned limit = next;
  if (limit>max_record_count) { // Clip
    limit = max_record_count;
  }
  for (unsigned i=0; i<limit; ++i) {
    fprintf(f,"%f\t%d\t%d\t%d\t%d\n",(array[i].time-array[0].time).seconds(), array[i].n_tbb_outer_thread,
	    array[i].n_tbb_inner_thread, array[i].n_omp_outer_thread, array[i].n_omp_inner_thread);
  }
  fclose(f);
  int tox=tbb_outer_level, tix=tbb_inner_level, oox=omp_outer_level, oix=omp_inner_level;
  int tot_threads;
  tot_threads = tox+tix+oox+oix;
  if (!fail) printf("INFO: Passed.\n");
  else printf("INFO: Failed.\n");
}

void ThreadLevelRecorder::init() {
  fail = false;
  max_threads = omp_get_max_threads();
  printf("INFO: Getting maximum hardware threads... %d.\n", max_threads);
}

ThreadLevelRecorder TotalThreadLevel;
#endif
