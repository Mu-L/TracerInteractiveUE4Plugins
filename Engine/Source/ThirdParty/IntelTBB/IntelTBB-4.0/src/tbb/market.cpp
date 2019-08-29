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

#include "tbb/tbb_stddef.h"

#include "market.h"
#include "tbb_main.h"
#include "governor.h"
#include "scheduler.h"
#include "itt_notify.h"

namespace tbb {
namespace internal {

void market::insert_arena_into_list ( arena& a ) {
#if __TBB_TASK_PRIORITY
    arena_list_type &arenas = my_priority_levels[a.my_top_priority].arenas;
    arena_list_type::iterator &next = my_priority_levels[a.my_top_priority].next_arena;
#else /* !__TBB_TASK_PRIORITY */
    arena_list_type &arenas = my_arenas;
    arena_list_type::iterator &next = my_next_arena;
#endif /* !__TBB_TASK_PRIORITY */
    arenas.push_front( a );
    if ( arenas.size() == 1 )
        next = arenas.begin();
}

void market::remove_arena_from_list ( arena& a ) {
#if __TBB_TASK_PRIORITY
    arena_list_type &arenas = my_priority_levels[a.my_top_priority].arenas;
    arena_list_type::iterator &next = my_priority_levels[a.my_top_priority].next_arena;
#else /* !__TBB_TASK_PRIORITY */
    arena_list_type &arenas = my_arenas;
    arena_list_type::iterator &next = my_next_arena;
#endif /* !__TBB_TASK_PRIORITY */
    __TBB_ASSERT( next != arenas.end(), NULL );
    if ( &*next == &a )
        if ( ++next == arenas.end() && arenas.size() > 1 )
            next = arenas.begin();
    arenas.remove( a );
}

//------------------------------------------------------------------------
// market
//------------------------------------------------------------------------

market::market ( unsigned max_num_workers, size_t stack_size )
    : my_ref_count(1)
    , my_stack_size(stack_size)
    , my_max_num_workers(max_num_workers)
#if __TBB_TASK_PRIORITY
    , my_global_top_priority(normalized_normal_priority)
    , my_global_bottom_priority(normalized_normal_priority)
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    , my_lowest_populated_level(normalized_normal_priority)
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
#endif /* __TBB_TASK_PRIORITY */
{
#if __TBB_TASK_PRIORITY
    __TBB_ASSERT( my_global_reload_epoch == 0, NULL );
    my_priority_levels[normalized_normal_priority].workers_available = max_num_workers;
#endif /* __TBB_TASK_PRIORITY */

    // Once created RML server will start initializing workers that will need 
    // global market instance to get worker stack size
    my_server = governor::create_rml_server( *this );
    __TBB_ASSERT( my_server, "Failed to create RML server" );
}


market& market::global_market ( unsigned max_num_workers, size_t stack_size ) {
    global_market_mutex_type::scoped_lock lock( theMarketMutex );
    market *m = theMarket;
    if ( m ) {
        ++m->my_ref_count;
        if ( m->my_stack_size < stack_size )
            runtime_warning( "Newer master request for larger stack cannot be satisfied\n" );
    }
    else {
        max_num_workers = max( governor::default_num_threads() - 1, max_num_workers );
        // at least 1 worker is required to support starvation resistant tasks
        if( max_num_workers==0 ) max_num_workers = 1;
        // Create the global market instance
        size_t size = sizeof(market);
#if __TBB_TASK_GROUP_CONTEXT
        __TBB_ASSERT( __TBB_offsetof(market, my_workers) + sizeof(generic_scheduler*) == sizeof(market),
                      "my_workers must be the last data field of the market class");
        size += sizeof(generic_scheduler*) * (max_num_workers - 1);
#endif /* __TBB_TASK_GROUP_CONTEXT */
        __TBB_InitOnce::add_ref();
        void* storage = NFS_Allocate(size, 1, NULL);
        memset( storage, 0, size );
        // Initialize and publish global market
        m = new (storage) market( max_num_workers, stack_size );
        theMarket = m;
    }
    return *m;
}

void market::destroy () {
#if __TBB_COUNT_TASK_NODES
    if ( my_task_node_count )
        runtime_warning( "Leaked %ld task objects\n", (long)my_task_node_count );
#endif /* __TBB_COUNT_TASK_NODES */
    this->~market();
    NFS_Free( this );
    __TBB_InitOnce::remove_ref();
}

void market::release () {
    __TBB_ASSERT( theMarket == this, "Global market instance was destroyed prematurely?" );
    bool do_release = false;
    {
        global_market_mutex_type::scoped_lock lock(theMarketMutex);
        if ( --my_ref_count == 0 ) {
            do_release = true;
            theMarket = NULL;
        }
    }
    if( do_release )
        my_server->request_close_connection();
}

arena& market::create_arena ( unsigned max_num_workers, size_t stack_size ) {
    market &m = global_market( max_num_workers, stack_size ); // increases market's ref count
    arena& a = arena::allocate_arena( m, min(max_num_workers, m.my_max_num_workers) );
    // Add newly created arena into the existing market's list.
    spin_mutex::scoped_lock lock(m.my_arenas_list_mutex);
    m.insert_arena_into_list(a);
    return a;
}

/** This method must be invoked under my_arenas_list_mutex. **/
void market::detach_arena ( arena& a ) {
    __TBB_ASSERT( theMarket == this, "Global market instance was destroyed prematurely?" );
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    __TBB_ASSERT( !a.my_num_workers_present, NULL );
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
    __TBB_ASSERT( !a.my_slots[0].my_scheduler, NULL );
    remove_arena_from_list(a);
    if ( a.my_aba_epoch == my_arenas_aba_epoch )
        ++my_arenas_aba_epoch;
}

void market::try_destroy_arena ( arena* a, uintptr_t aba_epoch ) {
    __TBB_ASSERT ( a, NULL );
    spin_mutex::scoped_lock lock(my_arenas_list_mutex);
    assert_market_valid();
#if __TBB_TASK_PRIORITY
    for ( int p = my_global_top_priority; p >= my_global_bottom_priority; --p ) {
        priority_level_info &pl = my_priority_levels[p];
        arena_list_type &my_arenas = pl.arenas;
#endif /* __TBB_TASK_PRIORITY */
        arena_list_type::iterator it = my_arenas.begin();
        for ( ; it != my_arenas.end(); ++it ) {
            if ( a == &*it ) {
                if ( it->my_aba_epoch == aba_epoch ) {
                    // Arena is alive
                    if ( !a->my_num_workers_requested && !a->my_references ) {
                        __TBB_ASSERT( !a->my_num_workers_allotted && (a->my_pool_state == arena::SNAPSHOT_EMPTY || !a->my_max_num_workers), "Inconsistent arena state" );
                        // Arena is abandoned. Destroy it.
                        detach_arena( *a );
                        lock.release();
                        a->free_arena();
                    }
                }
                return;
            }
        }
#if __TBB_TASK_PRIORITY
    }
#endif /* __TBB_TASK_PRIORITY */
}

void market::try_destroy_arena ( market* m, arena* a, uintptr_t aba_epoch, bool master ) {
    // Arena may have been orphaned. Or it may have been destroyed.
    // Thus we cannot dereference the pointer to it until its liveness is verified.
    // Arena is alive if it is found in the market's list.

    if ( m != theMarket ) {
        // The market has already been emptied.
        return;
    }
    else if ( master ) {
        // If this is a master thread, market can be destroyed at any moment.
        // So protect it with an extra refcount.
        global_market_mutex_type::scoped_lock lock(theMarketMutex);
        if ( m != theMarket )
            return;
        ++m->my_ref_count;
    }
    m->try_destroy_arena( a, aba_epoch );
    if ( master )
        m->release();
}

/** This method must be invoked under my_arenas_list_mutex. **/
arena* market::arena_in_need ( arena_list_type &arenas, arena_list_type::iterator& next ) {
    if ( arenas.empty() )
        return NULL;
    __TBB_ASSERT( next != arenas.end(), NULL );
    arena_list_type::iterator it = next;
    do {
        arena& a = *it;
        if ( ++it == arenas.end() )
            it = arenas.begin();
        if ( a.num_workers_active() < a.my_num_workers_allotted ) {
            a.my_references+=2; // add a worker
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
            ++a.my_num_workers_present;
            ++my_priority_levels[a.my_top_priority].workers_present;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
            next = it;
            return &a;
        }
    } while ( it != next );
    return NULL;
}

void market::update_allotment ( arena_list_type& arenas, int workers_demand, int max_workers ) {
    __TBB_ASSERT( workers_demand, NULL );
    max_workers = min(workers_demand, max_workers);
    int carry = 0;
#if TBB_USE_ASSERT
    int assigned = 0;
#endif /* TBB_USE_ASSERT */
    arena_list_type::iterator it = arenas.begin();
    for ( ; it != arenas.end(); ++it ) {
        arena& a = *it;
        if ( a.my_num_workers_requested <= 0 ) {
            __TBB_ASSERT( !a.my_num_workers_allotted, NULL );
            continue;
        }
        int tmp = a.my_num_workers_requested * max_workers + carry;
        int allotted = tmp / workers_demand;
        carry = tmp % workers_demand;
        // a.my_num_workers_requested may temporarily exceed a.my_max_num_workers
        a.my_num_workers_allotted = min( allotted, (int)a.my_max_num_workers );
#if TBB_USE_ASSERT
        assigned += a.my_num_workers_allotted;
#endif /* TBB_USE_ASSERT */
    }
    __TBB_ASSERT( assigned <= workers_demand, NULL );
}

#if __TBB_TASK_PRIORITY
inline void market::update_global_top_priority ( intptr_t newPriority ) {
    GATHER_STATISTIC( ++governor::local_scheduler_if_initialized()->my_counters.market_prio_switches );
    my_global_top_priority = newPriority;
    my_priority_levels[newPriority].workers_available = my_max_num_workers;
    advance_global_reload_epoch();
}

inline void market::reset_global_priority () {
    my_global_bottom_priority = normalized_normal_priority;
    update_global_top_priority(normalized_normal_priority);
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    my_lowest_populated_level = normalized_normal_priority;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
}

arena* market::arena_in_need (
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
                              arena* prev_arena
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
                             )
{
    spin_mutex::scoped_lock lock(my_arenas_list_mutex);
    assert_market_valid();
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    if ( prev_arena ) {
        priority_level_info &pl = my_priority_levels[prev_arena->my_top_priority];
        --prev_arena->my_num_workers_present;
        --pl.workers_present;
        if ( !--prev_arena->my_references && !prev_arena->my_num_workers_requested ) {
            detach_arena( *a );
            lock.release();
            a->free_arena();
            lock.acquire();
        }
    }
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
    int p = my_global_top_priority;
    arena *a = NULL;
    do {
        priority_level_info &pl = my_priority_levels[p];
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
        __TBB_ASSERT( p >= my_lowest_populated_level, NULL );
        if ( pl.workers_present >= pl.workers_requested )
            continue;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
        a = arena_in_need( pl.arenas, pl.next_arena );
    } while ( !a && --p >= my_global_bottom_priority );
    return a;
}

void market::update_allotment ( intptr_t highest_affected_priority ) {
    intptr_t i = highest_affected_priority;
    int available = my_priority_levels[i].workers_available;
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    my_lowest_populated_level = my_global_bottom_priority;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
    for ( ; i >= my_global_bottom_priority; --i ) {
        priority_level_info &pl = my_priority_levels[i];
        pl.workers_available = available;
        if ( pl.workers_requested ) {
            update_allotment( pl.arenas, pl.workers_requested, available );
            available -= pl.workers_requested;
            if ( available < 0 ) {
                available = 0;
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
                my_lowest_populated_level = i;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
                break;
            }
        }
    }
    __TBB_ASSERT( i <= my_global_bottom_priority || !available, NULL );
    for ( --i; i >= my_global_bottom_priority; --i ) {
        priority_level_info &pl = my_priority_levels[i];
        pl.workers_available = 0;
        arena_list_type::iterator it = pl.arenas.begin();
        for ( ; it != pl.arenas.end(); ++it ) {
            __TBB_ASSERT( it->my_num_workers_requested || !it->my_num_workers_allotted, NULL );
            it->my_num_workers_allotted = 0;
        }
    }
}
#endif /* __TBB_TASK_PRIORITY */

void market::adjust_demand ( arena& a, int delta ) {
    __TBB_ASSERT( theMarket, "market instance was destroyed prematurely?" );
    if ( !delta )
        return;
    my_arenas_list_mutex.lock();
    int prev_req = a.my_num_workers_requested;
    a.my_num_workers_requested += delta;
    if ( a.my_num_workers_requested <= 0 ) {
        a.my_num_workers_allotted = 0;
        if ( prev_req <= 0 ) {
            my_arenas_list_mutex.unlock();
            return;
        }
        delta = -prev_req;
    }
#if __TBB_TASK_ARENA
    else if ( prev_req < 0 ) {
        delta = a.my_num_workers_requested;
    }
#else  /* __TBB_TASK_ARENA */
    __TBB_ASSERT( prev_req >= 0, "Part-size request to RML?" );
#endif /* __TBB_TASK_ARENA */
#if __TBB_TASK_PRIORITY
    intptr_t p = a.my_top_priority;
    priority_level_info &pl = my_priority_levels[p];
    pl.workers_requested += delta;
    __TBB_ASSERT( pl.workers_requested >= 0, NULL );
#if !__TBB_TASK_ARENA
    __TBB_ASSERT( a.my_num_workers_requested >= 0, NULL );
#else
    //TODO: understand the assertion and modify
#endif
    if ( a.my_num_workers_requested <= 0 ) {
        if ( a.my_top_priority != normalized_normal_priority ) {
            GATHER_STATISTIC( ++governor::local_scheduler_if_initialized()->my_counters.arena_prio_resets );
            update_arena_top_priority( a, normalized_normal_priority );
        }
        a.my_bottom_priority = normalized_normal_priority;
    }
    if ( p == my_global_top_priority ) {
        if ( !pl.workers_requested ) {
            while ( --p >= my_global_bottom_priority && !my_priority_levels[p].workers_requested )
                continue;
            if ( p < my_global_bottom_priority )
                reset_global_priority();
            else
                update_global_top_priority(p);
        }
        update_allotment( my_global_top_priority );
    }
    else if ( p > my_global_top_priority ) {
#if !__TBB_TASK_ARENA
        __TBB_ASSERT( pl.workers_requested > 0, NULL );
#else
        //TODO: understand the assertion and modify
#endif
        update_global_top_priority(p);
        a.my_num_workers_allotted = min( (int)my_max_num_workers, a.my_num_workers_requested );
        my_priority_levels[p - 1].workers_available = my_max_num_workers - a.my_num_workers_allotted;
        update_allotment( p - 1 );
    }
    else if ( p == my_global_bottom_priority ) {
        if ( !pl.workers_requested ) {
            while ( ++p <= my_global_top_priority && !my_priority_levels[p].workers_requested )
                continue;
            if ( p > my_global_top_priority )
                reset_global_priority();
            else {
                my_global_bottom_priority = p;
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
                my_lowest_populated_level = max( my_lowest_populated_level, p );
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
            }
        }
        else
            update_allotment( p );
    }
    else if ( p < my_global_bottom_priority ) {
        __TBB_ASSERT( a.my_num_workers_requested > 0, NULL );
        int prev_bottom = my_global_bottom_priority;
        my_global_bottom_priority = p;
        update_allotment( prev_bottom );
    }
    else {
        __TBB_ASSERT( my_global_bottom_priority < p && p < my_global_top_priority, NULL );
        update_allotment( p );
    }
    assert_market_valid();
#else /* !__TBB_TASK_PRIORITY */
    my_total_demand += delta;
    update_allotment();
#endif /* !__TBB_TASK_PRIORITY */
    my_arenas_list_mutex.unlock();
    // Must be called outside of any locks
    my_server->adjust_job_count_estimate( delta );
    GATHER_STATISTIC( governor::local_scheduler_if_initialized() ? ++governor::local_scheduler_if_initialized()->my_counters.gate_switches : 0 );
}

void market::process( job& j ) {
    generic_scheduler& s = static_cast<generic_scheduler&>(j);
    __TBB_ASSERT( governor::is_set(&s), NULL );
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    arena *a = NULL;
    while ( (a = arena_in_need(a)) )
#else
    while ( arena *a = arena_in_need() )
#endif
        a->process(s);
    GATHER_STATISTIC( ++s.my_counters.market_roundtrips );
}

void market::cleanup( job& j ) {
    __TBB_ASSERT( theMarket != this, NULL );
    generic_scheduler& s = static_cast<generic_scheduler&>(j);
    generic_scheduler* mine = governor::local_scheduler_if_initialized();
    __TBB_ASSERT( !mine || mine->my_arena_index!=0, NULL );
    if( mine!=&s ) {
        governor::assume_scheduler( &s );
        generic_scheduler::cleanup_worker( &s, mine!=NULL );
        governor::assume_scheduler( mine );
    } else {
        generic_scheduler::cleanup_worker( &s, true );
    }
}

void market::acknowledge_close_connection() {
    destroy();
}

::rml::job* market::create_one_job() {
    unsigned index = ++my_num_workers;
    __TBB_ASSERT( index > 0, NULL );
    ITT_THREAD_SET_NAME(_T("TBB Worker Thread"));
    // index serves as a hint decreasing conflicts between workers when they migrate between arenas
    generic_scheduler* s = generic_scheduler::create_worker( *this, index );
#if __TBB_TASK_GROUP_CONTEXT
    __TBB_ASSERT( !my_workers[index - 1], NULL );
    my_workers[index - 1] = s;
#endif /* __TBB_TASK_GROUP_CONTEXT */
    governor::sign_on(s);
    return s;
}

#if __TBB_TASK_PRIORITY
void market::update_arena_top_priority ( arena& a, intptr_t new_priority ) {
    GATHER_STATISTIC( ++governor::local_scheduler_if_initialized()->my_counters.arena_prio_switches );
    __TBB_ASSERT( a.my_top_priority != new_priority, NULL );
    priority_level_info &prev_level = my_priority_levels[a.my_top_priority],
                        &new_level = my_priority_levels[new_priority];
    remove_arena_from_list(a);
    a.my_top_priority = new_priority;
    insert_arena_into_list(a);
    ++a.my_reload_epoch;
#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    // Arena's my_num_workers_present may remain positive for some time after its
    // my_num_workers_requested becomes zero. Thus the following two lines are
    // executed unconditionally.
    prev_level.workers_present -= a.my_num_workers_present;
    new_level.workers_present += a.my_num_workers_present;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
    prev_level.workers_requested -= a.my_num_workers_requested;
    new_level.workers_requested += a.my_num_workers_requested;
    __TBB_ASSERT( prev_level.workers_requested >= 0 && new_level.workers_requested >= 0, NULL );
}

bool market::lower_arena_priority ( arena& a, intptr_t new_priority, intptr_t old_priority ) {
    spin_mutex::scoped_lock lock(my_arenas_list_mutex);
    if ( a.my_top_priority != old_priority ) {
        assert_market_valid();
        return false;
    }
    __TBB_ASSERT( a.my_top_priority > new_priority, NULL );
    __TBB_ASSERT( my_global_top_priority >= a.my_top_priority, NULL );
    intptr_t p = a.my_top_priority;
    update_arena_top_priority( a, new_priority );
    if ( a.my_num_workers_requested > 0 ) {
        if ( my_global_bottom_priority > new_priority ) {
            my_global_bottom_priority = new_priority;
        }
        if ( p == my_global_top_priority && !my_priority_levels[p].workers_requested ) {
            // Global top level became empty
            for ( --p; !my_priority_levels[p].workers_requested; --p ) continue;
            __TBB_ASSERT( p >= my_global_bottom_priority, NULL );
            update_global_top_priority(p);
        }
        update_allotment( p );
    }
    assert_market_valid();
    return true;
}

bool market::update_arena_priority ( arena& a, intptr_t new_priority ) {
    spin_mutex::scoped_lock lock(my_arenas_list_mutex);
    if ( a.my_top_priority == new_priority ) {
        assert_market_valid();
        return false;
    }
    else if ( a.my_top_priority > new_priority ) {
        if ( a.my_bottom_priority > new_priority )
            a.my_bottom_priority = new_priority;
        assert_market_valid();
        return false;
    }
    intptr_t p = a.my_top_priority;
    intptr_t highest_affected_level = max(p, new_priority);
    update_arena_top_priority( a, new_priority );
    if ( a.my_num_workers_requested > 0 ) {
        if ( my_global_top_priority < new_priority ) {
            update_global_top_priority(new_priority);
        }
        else if ( my_global_top_priority == new_priority ) {
            advance_global_reload_epoch();
        }
        else {
            __TBB_ASSERT( new_priority < my_global_top_priority, NULL );
            __TBB_ASSERT( new_priority > my_global_bottom_priority, NULL );
            if ( p == my_global_top_priority && !my_priority_levels[p].workers_requested ) {
                // Global top level became empty
                __TBB_ASSERT( my_global_bottom_priority < p, NULL );
                for ( --p; !my_priority_levels[p].workers_requested; --p ) continue;
                __TBB_ASSERT( p >= new_priority, NULL );
                update_global_top_priority(p);
                highest_affected_level = p;
            }
        }
        if ( p == my_global_bottom_priority ) {
            // Arena priority was increased from the global bottom level.
            __TBB_ASSERT( p < new_priority, NULL );                     // n
            __TBB_ASSERT( new_priority <= my_global_top_priority, NULL );
            while ( !my_priority_levels[my_global_bottom_priority].workers_requested )
                ++my_global_bottom_priority;
            __TBB_ASSERT( my_global_bottom_priority <= new_priority, NULL );
            __TBB_ASSERT( my_priority_levels[my_global_bottom_priority].workers_requested > 0, NULL );
        }
        update_allotment( highest_affected_level );
    }
    assert_market_valid();
    return true;
}
#endif /* __TBB_TASK_PRIORITY */

#if __TBB_COUNT_TASK_NODES 
intptr_t market::workers_task_node_count() {
    intptr_t result = 0;
    ForEachArena(a) {
        result += a.workers_task_node_count();
    } EndForEach();
    return result;
}
#endif /* __TBB_COUNT_TASK_NODES */

} // namespace internal
} // namespace tbb
