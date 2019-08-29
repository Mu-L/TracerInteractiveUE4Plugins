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

#ifndef __TBB_queuing_rw_mutex_H
#define __TBB_queuing_rw_mutex_H

#include "tbb_config.h"

#if !TBB_USE_EXCEPTIONS && _MSC_VER
    // Suppress "C++ exception handler used, but unwind semantics are not enabled" warning in STL headers
    #pragma warning (push)
    #pragma warning (disable: 4530)
#endif

#include <cstring>

#if !TBB_USE_EXCEPTIONS && _MSC_VER
    #pragma warning (pop)
#endif

#include "atomic.h"
#include "tbb_profiling.h"

namespace tbb {

//! Queuing reader-writer mutex with local-only spinning.
/** Adapted from Krieger, Stumm, et al. pseudocode at
    http://www.eecg.toronto.edu/parallel/pubs_abs.html#Krieger_etal_ICPP93
    @ingroup synchronization */
class queuing_rw_mutex {
public:
    //! Construct unacquired mutex.
    queuing_rw_mutex() {
        q_tail = NULL;
#if TBB_USE_THREADING_TOOLS
        internal_construct();
#endif
    }

    //! Destructor asserts if the mutex is acquired, i.e. q_tail is non-NULL
    ~queuing_rw_mutex() {
#if TBB_USE_ASSERT
        __TBB_ASSERT( !q_tail, "destruction of an acquired mutex");
#endif
    }

    //! The scoped locking pattern
    /** It helps to avoid the common problem of forgetting to release lock.
        It also nicely provides the "node" for queuing locks. */
    class scoped_lock: internal::no_copy {
        //! Initialize fields to mean "no lock held".
        void initialize() {
            my_mutex = NULL;
#if TBB_USE_ASSERT
            my_state = 0xFF; // Set to invalid state
            internal::poison_pointer(my_next);
            internal::poison_pointer(my_prev);
#endif /* TBB_USE_ASSERT */
        }

    public:
        //! Construct lock that has not acquired a mutex.
        /** Equivalent to zero-initialization of *this. */
        scoped_lock() {initialize();}

        //! Acquire lock on given mutex.
        scoped_lock( queuing_rw_mutex& m, bool write=true ) {
            initialize();
            acquire(m,write);
        }

        //! Release lock (if lock is held).
        ~scoped_lock() {
            if( my_mutex ) release();
        }

        //! Acquire lock on given mutex.
        void acquire( queuing_rw_mutex& m, bool write=true );

        //! Acquire lock on given mutex if free (i.e. non-blocking)
        bool try_acquire( queuing_rw_mutex& m, bool write=true );

        //! Release lock.
        void release();

        //! Upgrade reader to become a writer.
        /** Returns whether the upgrade happened without releasing and re-acquiring the lock */
        bool upgrade_to_writer();

        //! Downgrade writer to become a reader.
        bool downgrade_to_reader();

    private:
        //! The pointer to the mutex owned, or NULL if not holding a mutex.
        queuing_rw_mutex* my_mutex;

        //! The pointer to the previous and next competitors for a mutex
        scoped_lock *__TBB_atomic my_prev, *__TBB_atomic my_next;

        typedef unsigned char state_t;

        //! State of the request: reader, writer, active reader, other service states
        atomic<state_t> my_state;

        //! The local spin-wait variable
        /** Corresponds to "spin" in the pseudocode but inverted for the sake of zero-initialization */
        unsigned char __TBB_atomic my_going;

        //! A tiny internal lock
        unsigned char my_internal_lock;

        //! Acquire the internal lock
        void acquire_internal_lock();

        //! Try to acquire the internal lock
        /** Returns true if lock was successfully acquired. */
        bool try_acquire_internal_lock();

        //! Release the internal lock
        void release_internal_lock();

        //! Wait for internal lock to be released
        void wait_for_release_of_internal_lock();

        //! A helper function
        void unblock_or_wait_on_internal_lock( uintptr_t );
    };

    void __TBB_EXPORTED_METHOD internal_construct();

    // Mutex traits
    static const bool is_rw_mutex = true;
    static const bool is_recursive_mutex = false;
    static const bool is_fair_mutex = true;

private:
    //! The last competitor requesting the lock
    atomic<scoped_lock*> q_tail;

};

__TBB_DEFINE_PROFILING_SET_NAME(queuing_rw_mutex)

} // namespace tbb

#endif /* __TBB_queuing_rw_mutex_H */
