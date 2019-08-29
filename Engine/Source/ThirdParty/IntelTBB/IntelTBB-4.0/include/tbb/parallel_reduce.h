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

#ifndef __TBB_parallel_reduce_H
#define __TBB_parallel_reduce_H

#include <new>
#include "task.h"
#include "aligned_space.h"
#include "partitioner.h"
#include "tbb_profiling.h"

namespace tbb {

namespace interface6 {
//! @cond INTERNAL
namespace internal {

    using namespace tbb::internal;

    /** Values for reduction_context. */
    enum {
        root_task, left_child, right_child
    };

    /** Represented as a char, not enum, for compactness. */
    typedef char reduction_context;

    //! Task type used to combine the partial results of parallel_reduce.
    /** @ingroup algorithms */
    template<typename Body>
    class finish_reduce: public flag_task {
        //! Pointer to body, or NULL if the left child has not yet finished.
        bool has_right_zombie;
        const reduction_context my_context;
        Body* my_body;
        aligned_space<Body,1> zombie_space;
        finish_reduce( reduction_context context_ ) :
            has_right_zombie(false), // TODO: substitute by flag_task::child_stolen?
            my_context(context_),
            my_body(NULL)
        {
        }
        task* execute() {
            if( has_right_zombie ) {
                // Right child was stolen.
                Body* s = zombie_space.begin();
                my_body->join( *s );
                s->~Body();
            }
            if( my_context==left_child )
                itt_store_word_with_release( static_cast<finish_reduce*>(parent())->my_body, my_body );
            return NULL;
        }
        template<typename Range,typename Body_, typename Partitioner>
        friend class start_reduce;
    };

    //! Task type used to split the work of parallel_reduce.
    /** @ingroup algorithms */
    template<typename Range, typename Body, typename Partitioner>
    class start_reduce: public task {
        typedef finish_reduce<Body> finish_type;
        Body* my_body;
        Range my_range;
        typename Partitioner::task_partition_type my_partition;
        reduction_context my_context; // TODO: factor out into start_reduce_base
        /*override*/ task* execute();
        template<typename Body_>
        friend class finish_reduce;

public:
        //! Constructor used for root task
        start_reduce( const Range& range, Body* body, Partitioner& partitioner ) :
            my_body(body),
            my_range(range),
            my_partition(partitioner),
            my_context(root_task)
        {
        }
        //! Splitting constructor used to generate children.
        /** parent_ becomes left child.  Newly constructed object is right child. */
        start_reduce( start_reduce& parent_, split ) :
            my_body(parent_.my_body),
            my_range(parent_.my_range,split()),
            my_partition(parent_.my_partition,split()),
            my_context(right_child)
        {
            my_partition.set_affinity(*this);
            parent_.my_context = left_child;
        }
        //! Construct right child from the given range as response to the demand.
        /** parent_ remains left child.  Newly constructed object is right child. */
        start_reduce( start_reduce& parent_, const Range& r, depth_t d ) :
            my_body(parent_.my_body),
            my_range(r),
            my_partition(parent_.my_partition,split()),
            my_context(right_child)
        {
            my_partition.set_affinity(*this);
            my_partition.align_depth( d );
            parent_.my_context = left_child;
        }
        //! Update affinity info, if any
        /*override*/ void note_affinity( affinity_id id ) {
            my_partition.note_affinity( id );
        }
        static void run( const Range& range, Body& body, Partitioner& partitioner ) {
            if( !range.empty() ) {
#if !__TBB_TASK_GROUP_CONTEXT || TBB_JOIN_OUTER_TASK_GROUP
                task::spawn_root_and_wait( *new(task::allocate_root()) start_reduce(range,&body,partitioner) );
#else
                // Bound context prevents exceptions from body to affect nesting or sibling algorithms,
                // and allows users to handle exceptions safely by wrapping parallel_for in the try-block.
                task_group_context context;
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_reduce(range,&body,partitioner) );
#endif /* __TBB_TASK_GROUP_CONTEXT && !TBB_JOIN_OUTER_TASK_GROUP */
            }
        }
#if __TBB_TASK_GROUP_CONTEXT
        static void run( const Range& range, Body& body, Partitioner& partitioner, task_group_context& context ) {
            if( !range.empty() )
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_reduce(range,&body,partitioner) );
        }
#endif /* __TBB_TASK_GROUP_CONTEXT */
        //! create a continuation task, serve as callback for partitioner
        finish_type *create_continuation() {
            return new( allocate_continuation() ) finish_type(my_context);
        }
        //! Run body for range
        void run_body( Range &r ) { (*my_body)( r ); }
    };
    template<typename Range, typename Body, typename Partitioner>
    task* start_reduce<Range,Body,Partitioner>::execute() {
        my_partition.check_being_stolen( *this );
        if( my_context==right_child ) {
            finish_type* parent_ptr = static_cast<finish_type*>(parent());
            if( !itt_load_word_with_acquire(parent_ptr->my_body) ) { // TODO: replace by is_stolen_task() or by parent_ptr->ref_count() == 2???
                my_body = new( parent_ptr->zombie_space.begin() ) Body(*my_body,split());
                parent_ptr->has_right_zombie = true;
            }
        } else __TBB_ASSERT(my_context==root_task,NULL);// because left leaf spawns right leafs without recycling
        my_partition.execute(*this, my_range);
        if( my_context==left_child ) {
            finish_type* parent_ptr = static_cast<finish_type*>(parent());
            __TBB_ASSERT(my_body!=parent_ptr->zombie_space.begin(),NULL);
            itt_store_word_with_release(parent_ptr->my_body, my_body );
        }
        return NULL;
    }

    //! Task type used to combine the partial results of parallel_deterministic_reduce.
    /** @ingroup algorithms */
    template<typename Body>
    class finish_deterministic_reduce: public task {
        Body &my_left_body;
        Body my_right_body;

        finish_deterministic_reduce( Body &body ) :
            my_left_body( body ),
            my_right_body( body, split() )
        {
        }
        task* execute() {
            my_left_body.join( my_right_body );
            return NULL;
        }
        template<typename Range,typename Body_>
        friend class start_deterministic_reduce;
    };

    //! Task type used to split the work of parallel_deterministic_reduce.
    /** @ingroup algorithms */
    template<typename Range, typename Body>
    class start_deterministic_reduce: public task {
        typedef finish_deterministic_reduce<Body> finish_type;
        Body &my_body;
        Range my_range;
        /*override*/ task* execute();

        //! Constructor used for root task
        start_deterministic_reduce( const Range& range, Body& body ) :
            my_body( body ),
            my_range( range )
        {
        }
        //! Splitting constructor used to generate children.
        /** parent_ becomes left child.  Newly constructed object is right child. */
        start_deterministic_reduce( start_deterministic_reduce& parent_, finish_type& c ) :
            my_body( c.my_right_body ),
            my_range( parent_.my_range, split() )
        {
        }

public:
        static void run( const Range& range, Body& body ) {
            if( !range.empty() ) {
#if !__TBB_TASK_GROUP_CONTEXT || TBB_JOIN_OUTER_TASK_GROUP
                task::spawn_root_and_wait( *new(task::allocate_root()) start_deterministic_reduce(range,&body) );
#else
                // Bound context prevents exceptions from body to affect nesting or sibling algorithms,
                // and allows users to handle exceptions safely by wrapping parallel_for in the try-block.
                task_group_context context;
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_deterministic_reduce(range,body) );
#endif /* __TBB_TASK_GROUP_CONTEXT && !TBB_JOIN_OUTER_TASK_GROUP */
            }
        }
#if __TBB_TASK_GROUP_CONTEXT
        static void run( const Range& range, Body& body, task_group_context& context ) {
            if( !range.empty() )
                task::spawn_root_and_wait( *new(task::allocate_root(context)) start_deterministic_reduce(range,body) );
        }
#endif /* __TBB_TASK_GROUP_CONTEXT */
    };

    template<typename Range, typename Body>
    task* start_deterministic_reduce<Range,Body>::execute() {
        if( !my_range.is_divisible() ) {
            my_body( my_range );
            return NULL;
        } else {
            finish_type& c = *new( allocate_continuation() ) finish_type( my_body );
            recycle_as_child_of(c);
            c.set_ref_count(2);
            start_deterministic_reduce& b = *new( c.allocate_child() ) start_deterministic_reduce( *this, c );
            task::spawn(b);
            return this;
        }
    }
} // namespace internal
//! @endcond
} //namespace interfaceX

//! @cond INTERNAL
namespace internal {
    using interface6::internal::start_reduce;
    using interface6::internal::start_deterministic_reduce;
    //! Auxiliary class for parallel_reduce; for internal use only.
    /** The adaptor class that implements \ref parallel_reduce_body_req "parallel_reduce Body"
        using given \ref parallel_reduce_lambda_req "anonymous function objects".
     **/
    /** @ingroup algorithms */
    template<typename Range, typename Value, typename RealBody, typename Reduction>
    class lambda_reduce_body {

//FIXME: decide if my_real_body, my_reduction, and identity_element should be copied or referenced
//       (might require some performance measurements)

        const Value&     identity_element;
        const RealBody&  my_real_body;
        const Reduction& my_reduction;
        Value            my_value;
        lambda_reduce_body& operator= ( const lambda_reduce_body& other );
    public:
        lambda_reduce_body( const Value& identity, const RealBody& body, const Reduction& reduction )
            : identity_element(identity)
            , my_real_body(body)
            , my_reduction(reduction)
            , my_value(identity)
        { }
        lambda_reduce_body( const lambda_reduce_body& other )
            : identity_element(other.identity_element)
            , my_real_body(other.my_real_body)
            , my_reduction(other.my_reduction)
            , my_value(other.my_value)
        { }
        lambda_reduce_body( lambda_reduce_body& other, tbb::split )
            : identity_element(other.identity_element)
            , my_real_body(other.my_real_body)
            , my_reduction(other.my_reduction)
            , my_value(other.identity_element)
        { }
        void operator()(Range& range) {
            my_value = my_real_body(range, const_cast<const Value&>(my_value));
        }
        void join( lambda_reduce_body& rhs ) {
            my_value = my_reduction(const_cast<const Value&>(my_value), const_cast<const Value&>(rhs.my_value));
        }
        Value result() const {
            return my_value;
        }
    };

} // namespace internal
//! @endcond

// Requirements on Range concept are documented in blocked_range.h

/** \page parallel_reduce_body_req Requirements on parallel_reduce body
    Class \c Body implementing the concept of parallel_reduce body must define:
    - \code Body::Body( Body&, split ); \endcode        Splitting constructor.
                                                        Must be able to run concurrently with operator() and method \c join
    - \code Body::~Body(); \endcode                     Destructor
    - \code void Body::operator()( Range& r ); \endcode Function call operator applying body to range \c r
                                                        and accumulating the result
    - \code void Body::join( Body& b ); \endcode        Join results.
                                                        The result in \c b should be merged into the result of \c this
**/

/** \page parallel_reduce_lambda_req Requirements on parallel_reduce anonymous function objects (lambda functions)
    TO BE DOCUMENTED
**/

/** \name parallel_reduce
    See also requirements on \ref range_req "Range" and \ref parallel_reduce_body_req "parallel_reduce Body". **/
//@{

//! Parallel iteration with reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body ) {
    internal::start_reduce<Range,Body, const __TBB_DEFAULT_PARTITIONER>::run( range, body, __TBB_DEFAULT_PARTITIONER() );
}

//! Parallel iteration with reduction and simple_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const simple_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,const simple_partitioner>::run( range, body, partitioner );
}

//! Parallel iteration with reduction and auto_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const auto_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,const auto_partitioner>::run( range, body, partitioner );
}

//! Parallel iteration with reduction and affinity_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, affinity_partitioner& partitioner ) {
    internal::start_reduce<Range,Body,affinity_partitioner>::run( range, body, partitioner );
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const simple_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,const simple_partitioner>::run( range, body, partitioner, context );
}

//! Parallel iteration with reduction, auto_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, const auto_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,const auto_partitioner>::run( range, body, partitioner, context );
}

//! Parallel iteration with reduction, affinity_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_reduce( const Range& range, Body& body, affinity_partitioner& partitioner, task_group_context& context ) {
    internal::start_reduce<Range,Body,affinity_partitioner>::run( range, body, partitioner, context );
}
#endif /* __TBB_TASK_GROUP_CONTEXT */

/** parallel_reduce overloads that work with anonymous function objects
    (see also \ref parallel_reduce_lambda_req "requirements on parallel_reduce anonymous function objects"). **/

//! Parallel iteration with reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const __TBB_DEFAULT_PARTITIONER>
                          ::run(range, body, __TBB_DEFAULT_PARTITIONER() );
    return body.result();
}

//! Parallel iteration with reduction and simple_partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const simple_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const simple_partitioner>
                          ::run(range, body, partitioner );
    return body.result();
}

//! Parallel iteration with reduction and auto_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const auto_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const auto_partitioner>
                          ::run( range, body, partitioner );
    return body.result();
}

//! Parallel iteration with reduction and affinity_partitioner
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       affinity_partitioner& partitioner ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,affinity_partitioner>
                                        ::run( range, body, partitioner );
    return body.result();
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const simple_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const simple_partitioner>
                          ::run( range, body, partitioner, context );
    return body.result();
}

//! Parallel iteration with reduction, auto_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       const auto_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,const auto_partitioner>
                          ::run( range, body, partitioner, context );
    return body.result();
}

//! Parallel iteration with reduction, affinity_partitioner and user-supplied context
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       affinity_partitioner& partitioner, task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction>,affinity_partitioner>
                                        ::run( range, body, partitioner, context );
    return body.result();
}
#endif /* __TBB_TASK_GROUP_CONTEXT */

//! Parallel iteration with deterministic reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_deterministic_reduce( const Range& range, Body& body ) {
    internal::start_deterministic_reduce<Range,Body>::run( range, body );
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with deterministic reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Body>
void parallel_deterministic_reduce( const Range& range, Body& body, task_group_context& context ) {
    internal::start_deterministic_reduce<Range,Body>::run( range, body, context );
}
#endif /* __TBB_TASK_GROUP_CONTEXT */

/** parallel_reduce overloads that work with anonymous function objects
    (see also \ref parallel_reduce_lambda_req "requirements on parallel_reduce anonymous function objects"). **/

//! Parallel iteration with deterministic reduction and default partitioner.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_deterministic_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_deterministic_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction> >
                          ::run(range, body);
    return body.result();
}

#if __TBB_TASK_GROUP_CONTEXT
//! Parallel iteration with deterministic reduction, simple partitioner and user-supplied context.
/** @ingroup algorithms **/
template<typename Range, typename Value, typename RealBody, typename Reduction>
Value parallel_deterministic_reduce( const Range& range, const Value& identity, const RealBody& real_body, const Reduction& reduction,
                       task_group_context& context ) {
    internal::lambda_reduce_body<Range,Value,RealBody,Reduction> body(identity, real_body, reduction);
    internal::start_deterministic_reduce<Range,internal::lambda_reduce_body<Range,Value,RealBody,Reduction> >
                          ::run( range, body, context );
    return body.result();
}
#endif /* __TBB_TASK_GROUP_CONTEXT */
//@}

} // namespace tbb

#endif /* __TBB_parallel_reduce_H */

