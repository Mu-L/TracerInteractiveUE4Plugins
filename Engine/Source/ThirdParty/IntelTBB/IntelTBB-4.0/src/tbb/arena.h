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

#ifndef _TBB_arena_H
#define _TBB_arena_H

#include "tbb/tbb_stddef.h"
#include "tbb/atomic.h"

#include "tbb/tbb_machine.h"

#if !__TBB_CPU_CTL_ENV_PRESENT
    #include <fenv.h>
    typedef fenv_t __TBB_cpu_ctl_env_t;
#endif /* !__TBB_CPU_CTL_ENV_PRESENT */

#include "scheduler_common.h"
#include "intrusive_list.h"
#include "task_stream.h"
#include "../rml/include/rml_tbb.h"
#include "mailbox.h"
#include "observer_proxy.h"

namespace tbb {

namespace interface6 {
class task_arena;
}
class task_group_context;
class allocate_root_with_context_proxy;

namespace internal {

class task_scheduler_observer_v3;
class governor;
class arena;
template<typename SchedulerTraits> class custom_scheduler;

class market;

//! arena data except the array of slots
/** Separated in order to simplify padding. 
    Intrusive list node base class is used by market to form a list of arenas. **/
struct arena_base : intrusive_list_node {
    //! Market owning this arena
    market* my_market;

    //! Maximal currently busy slot.
    atomic<unsigned> my_limit;

    //! Number of slots in the arena
    unsigned my_num_slots;

    //! Number of workers requested by the master thread owning the arena
    unsigned my_max_num_workers;

    //! Number of workers that are currently requested from the resource manager
    int my_num_workers_requested;

    //! Number of workers that have been marked out by the resource manager to service the arena
    unsigned my_num_workers_allotted;

    //! References of the arena
    /** Counts workers and master references separately. Bit 0 indicates reference from implicit
        master or explicit task_arena; the next bits contain number of workers servicing the arena.*/
    atomic<unsigned> my_references;

    //! ABA prevention marker
    uintptr_t my_aba_epoch;

    //! FPU control settings of arena's master thread captured at the moment of arena instantiation.
    __TBB_cpu_ctl_env_t my_cpu_ctl_env;

#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    int my_num_workers_present;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */

    //! Current task pool state and estimate of available tasks amount.
    /** The estimate is either 0 (SNAPSHOT_EMPTY) or infinity (SNAPSHOT_FULL). 
        Special state is "busy" (any other unsigned value). 
        Note that the implementation of arena::is_busy_or_empty() requires 
        my_pool_state to be unsigned. */
    tbb::atomic<uintptr_t> my_pool_state;

#if __TBB_TASK_GROUP_CONTEXT
    //! Default task group context.
    /** Used by root tasks allocated directly by the master thread (not from inside
        a TBB task) without explicit context specification. **/
    task_group_context* my_default_ctx;
#endif /* __TBB_TASK_GROUP_CONTEXT */

#if __TBB_TASK_PRIORITY
    //! Highest priority of recently spawned or enqueued tasks.
    volatile intptr_t my_top_priority;

    //! Lowest normalized priority of available spawned or enqueued tasks.
    intptr_t my_bottom_priority;

    //! Tracks events that may bring tasks in offload areas to the top priority level.
    /** Incremented when arena top priority changes or a task group priority
        is elevated to the current arena's top level. **/
    uintptr_t my_reload_epoch;

    //! List of offloaded tasks abandoned by workers revoked by the market
    task* my_orphaned_tasks;

    //! Counter used to track the occurrence of recent orphaning and re-sharing operations.
    tbb::atomic<uintptr_t> my_abandonment_epoch;

    //! Task pool for the tasks scheduled via task::enqueue() method
    /** Such scheduling guarantees eventual execution even if
        - new tasks are constantly coming (by extracting scheduled tasks in 
          relaxed FIFO order);
        - the enqueuing thread does not call any of wait_for_all methods. **/
    task_stream my_task_stream[num_priority_levels];

    //! Highest priority level containing enqueued tasks
    /** It being greater than 0 means that high priority enqueued tasks had to be
        bypassed because all workers were blocked in nested dispatch loops and
        were unable to progress at then current priority level. **/
    tbb::atomic<intptr_t> my_skipped_fifo_priority;
#else /* !__TBB_TASK_PRIORITY */

    //! Task pool for the tasks scheduled via task::enqueue() method
    /** Such scheduling guarantees eventual execution even if
        - new tasks are constantly coming (by extracting scheduled tasks in 
          relaxed FIFO order);
        - the enqueuing thread does not call any of wait_for_all methods. **/
    task_stream my_task_stream;
#endif /* !__TBB_TASK_PRIORITY */

#if __TBB_SCHEDULER_OBSERVER
    //! List of local observers attached to this arena.
    observer_list my_observers;
#endif /* __TBB_SCHEDULER_OBSERVER */

    //! Indicates if there is an oversubscribing worker created to service enqueued tasks.
    bool my_mandatory_concurrency;

#if TBB_USE_ASSERT
    //! Used to trap accesses to the object after its destruction.
    uintptr_t my_guard;
#endif /* TBB_USE_ASSERT */
}; // struct arena_base

class arena
#if (__GNUC__<4 || __GNUC__==4 && __GNUC_MINOR__==0) && !__INTEL_COMPILER
    : public padded<arena_base>
#else
    : private padded<arena_base>
#endif
{
private:
    friend class generic_scheduler;
    template<typename SchedulerTraits> friend class custom_scheduler;
    friend class governor;
    friend class task_scheduler_observer_v3;
    friend class market;
    friend class tbb::task_group_context;
    friend class allocate_root_with_context_proxy;
    friend class intrusive_list<arena>;
#if __TBB_TASK_ARENA
    friend class tbb::interface6::task_arena; // included through in scheduler_common.h
    friend class interface6::wait_task;
#endif //__TBB_TASK_ARENA

    typedef padded<arena_base> base_type;

    //! Constructor
    arena ( market&, unsigned max_num_workers );

    //! Allocate an instance of arena.
    static arena& allocate_arena( market&, unsigned max_num_workers );

    static int unsigned num_slots_to_reserve ( unsigned max_num_workers ) {
        return max(2u, max_num_workers + 1);
    }

    static int allocation_size ( unsigned max_num_workers ) {
        return sizeof(base_type) + num_slots_to_reserve(max_num_workers) * (sizeof(mail_outbox) + sizeof(arena_slot));
    }

#if __TBB_TASK_GROUP_CONTEXT
    //! Finds all contexts affected by the state change and propagates the new state to them.
    /** The propagation is relayed to the market because tasks created by one 
        master thread can be passed to and executed by other masters. This means 
        that context trees can span several arenas at once and thus state change
        propagation cannot be generally localized to one arena only. **/
    template <typename T>
    bool propagate_task_group_state ( T task_group_context::*mptr_state, task_group_context& src, T new_state );
#endif /* __TBB_TASK_GROUP_CONTEXT */

    //! Get reference to mailbox corresponding to given affinity_id.
    mail_outbox& mailbox( affinity_id id ) {
        __TBB_ASSERT( 0<id, "affinity id must be positive integer" );
        __TBB_ASSERT( id <= my_num_slots, "affinity id out of bounds" );

        return ((mail_outbox*)this)[-(int)id];
    }

    //! Completes arena shutdown, destructs and deallocates it.
    void free_arena ();

    typedef uintptr_t pool_state_t;

    //! No tasks to steal since last snapshot was taken
    static const pool_state_t SNAPSHOT_EMPTY = 0;

    //! At least one task has been offered for stealing since the last snapshot started
    static const pool_state_t SNAPSHOT_FULL = pool_state_t(-1);

    //! No tasks to steal or snapshot is being taken.
    static bool is_busy_or_empty( pool_state_t s ) { return s < SNAPSHOT_FULL; }

    //! The number of workers active in the arena.
    unsigned num_workers_active( ) {
        return my_references >> 1;
    }

    //! If necessary, raise a flag that there is new job in arena.
    template<bool Spawned> void advertise_new_work();

    //! Check if there is job anywhere in arena.
    /** Return true if no job or if arena is being cleaned up. */
    bool is_out_of_work();

    //! enqueue a task into starvation-resistance queue
#if __TBB_TASK_PRIORITY
    void enqueue_task( task&, priority_t, unsigned & );
#else /* !__TBB_TASK_PRIORITY */
    void enqueue_task( task&, unsigned & );
#endif /* !__TBB_TASK_PRIORITY */

    //! Registers the worker with the arena and enters TBB scheduler dispatch loop
    void process( generic_scheduler& );

    //! Notification that worker or master leaves its arena
    template<bool is_master>
    inline void on_thread_leaving ( );

#if __TBB_STATISTICS
    //! Outputs internal statistics accumulated by the arena
    void dump_arena_statistics ();
#endif /* __TBB_STATISTICS */

#if __TBB_TASK_PRIORITY
    //! Check if recent priority changes may bring some tasks to the current priority level soon
    /** /param tasks_present indicates presence of tasks at any priority level. **/
    inline bool may_have_tasks ( generic_scheduler*, arena_slot&, bool& tasks_present, bool& dequeuing_possible );
#endif /* __TBB_TASK_PRIORITY */

#if __TBB_COUNT_TASK_NODES
    //! Returns the number of task objects "living" in worker threads
    intptr_t workers_task_node_count();
#endif

    /** Must be the last data field */
    arena_slot my_slots[1];
}; // class arena

} // namespace internal
} // namespace tbb

#include "market.h"
#include "scheduler_common.h"
#include "governor.h"

namespace tbb {
namespace internal {

template<bool is_master>
inline void arena::on_thread_leaving ( ) {
    //
    // Implementation of arena destruction synchronization logic contained various
    // bugs/flaws at the different stages of its evolution, so below is a detailed
    // description of the issues taken into consideration in the framework of the
    // current design.
    //
    // In case of using fire-and-forget tasks (scheduled via task::enqueue())
    // master thread is allowed to leave its arena before all its work is executed,
    // and market may temporarily revoke all workers from this arena. Since revoked
    // workers never attempt to reset arena state to EMPTY and cancel its request
    // to RML for threads, the arena object is destroyed only when both the last
    // thread is leaving it and arena's state is EMPTY (that is its master thread
    // left and it does not contain any work).
    //
    // A worker that checks for work presence and transitions arena to the EMPTY
    // state (in snapshot taking procedure arena::is_out_of_work()) updates
    // arena::my_pool_state first and only then arena::my_num_workers_requested.
    // So the check for work absence must be done against the latter field.
    //
    // In a time window between decrementing the active threads count and checking
    // if there is an outstanding request for workers. New worker thread may arrive,
    // finish remaining work, set arena state to empty, and leave decrementing its
    // refcount and destroying. Then the current thread will destroy the arena
    // the second time. To preclude it a local copy of the outstanding request
    // value can be stored before decrementing active threads count.
    //
    // But this technique may cause two other problem. When the stored request is
    // zero, it is possible that arena still has threads and they can generate new
    // tasks and thus re-establish non-zero requests. Then all the threads can be
    // revoked (as described above) leaving this thread the last one, and causing
    // it to destroy non-empty arena.
    //
    // The other problem takes place when the stored request is non-zero. Another
    // thread may complete the work, set arena state to empty, and leave without
    // arena destruction before this thread decrements the refcount. This thread
    // cannot destroy the arena either. Thus the arena may be "orphaned".
    //
    // In both cases we cannot dereference arena pointer after the refcount is
    // decremented, as our arena may already be destroyed.
    //
    // If this is the master thread, market can be concurrently destroyed.
    // In case of workers market's liveness is ensured by the RML connection
    // rundown protocol, according to which the client (i.e. the market) lives
    // until RML server notifies it about connection termination, and this
    // notification is fired only after all workers return into RML.
    //
    // Thus if we decremented refcount to zero we ask the market to check arena
    // state (including the fact if it is alive) under the lock.
    //
    uintptr_t aba_epoch = my_aba_epoch;
    market* m = my_market;
    __TBB_ASSERT(my_references > int(!is_master), "broken arena reference counter");
    if ( (my_references -= is_master? 1:2 ) == 0 ) // worker's counter starts from bit 1
        market::try_destroy_arena( m, this, aba_epoch, is_master );
}

template<bool Spawned> void arena::advertise_new_work() {
    if( !Spawned ) { // i.e. the work was enqueued
        if( my_max_num_workers==0 ) {
            my_max_num_workers = 1;
            __TBB_ASSERT(!my_mandatory_concurrency, "");
            my_mandatory_concurrency = true;
            __TBB_ASSERT(!num_workers_active(), "");
            my_pool_state = SNAPSHOT_FULL;
            my_market->adjust_demand( *this, 1 );
            return;
        }
        // Local memory fence is required to avoid missed wakeups; see the comment below.
        // Starvation resistant tasks require mandatory concurrency, so missed wakeups are unacceptable.
        atomic_fence(); 
    }
    // Double-check idiom that, in case of spawning, is deliberately sloppy about memory fences.
    // Technically, to avoid missed wakeups, there should be a full memory fence between the point we 
    // released the task pool (i.e. spawned task) and read the arena's state.  However, adding such a 
    // fence might hurt overall performance more than it helps, because the fence would be executed 
    // on every task pool release, even when stealing does not occur.  Since TBB allows parallelism, 
    // but never promises parallelism, the missed wakeup is not a correctness problem.
    pool_state_t snapshot = my_pool_state;
    if( is_busy_or_empty(snapshot) ) {
        // Attempt to mark as full.  The compare_and_swap below is a little unusual because the 
        // result is compared to a value that can be different than the comparand argument.
        if( my_pool_state.compare_and_swap( SNAPSHOT_FULL, snapshot )==SNAPSHOT_EMPTY ) {
            if( snapshot!=SNAPSHOT_EMPTY ) {
                // This thread read "busy" into snapshot, and then another thread transitioned 
                // my_pool_state to "empty" in the meantime, which caused the compare_and_swap above 
                // to fail.  Attempt to transition my_pool_state from "empty" to "full".
                if( my_pool_state.compare_and_swap( SNAPSHOT_FULL, SNAPSHOT_EMPTY )!=SNAPSHOT_EMPTY ) {
                    // Some other thread transitioned my_pool_state from "empty", and hence became
                    // responsible for waking up workers.
                    return;
                }
            }
            // This thread transitioned pool from empty to full state, and thus is responsible for
            // telling RML that there is work to do.
            if( Spawned ) {
                if( my_mandatory_concurrency ) {
                    __TBB_ASSERT(my_max_num_workers==1, "");
                    __TBB_ASSERT(!governor::local_scheduler()->is_worker(), "");
                    // There was deliberate oversubscription on 1 core for sake of starvation-resistant tasks.
                    // Now a single active thread (must be the master) supposedly starts a new parallel region
                    // with relaxed sequential semantics, and oversubscription should be avoided.
                    // Demand for workers has been decreased to 0 during SNAPSHOT_EMPTY, so just keep it.
                    my_max_num_workers = 0;
                    my_mandatory_concurrency = false;
                    return;
                }
            }
            my_market->adjust_demand( *this, my_max_num_workers );
        }
    }
}

} // namespace internal
} // namespace tbb

#endif /* _TBB_arena_H */
