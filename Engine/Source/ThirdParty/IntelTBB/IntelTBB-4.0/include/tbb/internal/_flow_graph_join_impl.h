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

#ifndef __TBB__flow_graph_join_impl_H
#define __TBB__flow_graph_join_impl_H

#ifndef __TBB_flow_graph_H
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

#include "tbb/internal/_flow_graph_types_impl.h"

namespace internal {

    typedef size_t tag_value;
    static const tag_value NO_TAG = tag_value(-1);

    struct forwarding_base {
        forwarding_base(task *rt) : my_root_task(rt), current_tag(NO_TAG) {}
        virtual ~forwarding_base() {}
        virtual void decrement_port_count() = 0;
        virtual void increment_port_count() = 0;
        virtual void increment_tag_count(tag_value /*t*/) {}
        // moved here so input ports can queue tasks
        task* my_root_task;
        tag_value current_tag; // so ports can refer to FE's desired items
    };

    template< int N >
    struct join_helper {

        template< typename TupleType, typename PortType >
        static inline void set_join_node_pointer(TupleType &my_input, PortType *port) {
            std::get<N-1>( my_input ).set_join_node_pointer(port);
            join_helper<N-1>::set_join_node_pointer( my_input, port );
        }
        template< typename TupleType >
        static inline void consume_reservations( TupleType &my_input ) {
            std::get<N-1>( my_input ).consume();
            join_helper<N-1>::consume_reservations( my_input );
        }

        template< typename TupleType >
        static inline void release_my_reservation( TupleType &my_input ) {
            std::get<N-1>( my_input ).release();
        }

        template <typename TupleType>
        static inline void release_reservations( TupleType &my_input) {
            join_helper<N-1>::release_reservations(my_input);
            release_my_reservation(my_input);
        }

        template< typename InputTuple, typename OutputTuple >
        static inline bool reserve( InputTuple &my_input, OutputTuple &out) {
            if ( !std::get<N-1>( my_input ).reserve( std::get<N-1>( out ) ) ) return false;
            if ( !join_helper<N-1>::reserve( my_input, out ) ) {
                release_my_reservation( my_input );
                return false;
            }
            return true;
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_my_item( InputTuple &my_input, OutputTuple &out) {
            bool res = std::get<N-1>(my_input).get_item(std::get<N-1>(out) ); // may fail
            return join_helper<N-1>::get_my_item(my_input, out) && res;       // do get on other inputs before returning
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_items(InputTuple &my_input, OutputTuple &out) {
            return get_my_item(my_input, out);
        }

        template<typename InputTuple>
        static inline void reset_my_port(InputTuple &my_input) {
            join_helper<N-1>::reset_my_port(my_input);
            std::get<N-1>(my_input).reset_port();
        }

        template<typename InputTuple>
        static inline void reset_ports(InputTuple& my_input) {
            reset_my_port(my_input);
        }

        template<typename InputTuple, typename TagFuncTuple>
        static inline void set_tag_func(InputTuple &my_input, TagFuncTuple &my_tag_funcs) {
            std::get<N-1>(my_input).set_my_original_tag_func(std::get<N-1>(my_tag_funcs));
            std::get<N-1>(my_input).set_my_tag_func(std::get<N-1>(my_input).my_original_func()->clone());
            std::get<N-1>(my_tag_funcs) = NULL;
            join_helper<N-1>::set_tag_func(my_input, my_tag_funcs);
        }

        template< typename TagFuncTuple1, typename TagFuncTuple2>
        static inline void copy_tag_functors(TagFuncTuple1 &my_inputs, TagFuncTuple2 &other_inputs) {
            if(std::get<N-1>(other_inputs).my_original_func()) {
                std::get<N-1>(my_inputs).set_my_tag_func(std::get<N-1>(other_inputs).my_original_func()->clone());
                std::get<N-1>(my_inputs).set_my_original_tag_func(std::get<N-1>(other_inputs).my_original_func()->clone());
            }
            join_helper<N-1>::copy_tag_functors(my_inputs, other_inputs);
        }

        template<typename InputTuple>
        static inline void reset_inputs(InputTuple &my_input) {
            join_helper<N-1>::reset_inputs(my_input);
            std::get<N-1>(my_input).reinitialize_port();
        }
    };

    template< >
    struct join_helper<1> {

        template< typename TupleType, typename PortType >
        static inline void set_join_node_pointer(TupleType &my_input, PortType *port) {
            std::get<0>( my_input ).set_join_node_pointer(port);
        }

        template< typename TupleType >
        static inline void consume_reservations( TupleType &my_input ) {
            std::get<0>( my_input ).consume();
        }

        template< typename TupleType >
        static inline void release_my_reservation( TupleType &my_input ) {
            std::get<0>( my_input ).release();
        }

        template<typename TupleType>
        static inline void release_reservations( TupleType &my_input) {
            release_my_reservation(my_input);
        }

        template< typename InputTuple, typename OutputTuple >
        static inline bool reserve( InputTuple &my_input, OutputTuple &out) {
            return std::get<0>( my_input ).reserve( std::get<0>( out ) );
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_my_item( InputTuple &my_input, OutputTuple &out) {
            return std::get<0>(my_input).get_item(std::get<0>(out));
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_items(InputTuple &my_input, OutputTuple &out) {
            return get_my_item(my_input, out);
        }

        template<typename InputTuple>
        static inline void reset_my_port(InputTuple &my_input) {
            std::get<0>(my_input).reset_port();
        }

        template<typename InputTuple>
        static inline void reset_ports(InputTuple& my_input) {
            reset_my_port(my_input);
        }

        template<typename InputTuple, typename TagFuncTuple>
        static inline void set_tag_func(InputTuple &my_input, TagFuncTuple &my_tag_funcs) {
            std::get<0>(my_input).set_my_original_tag_func(std::get<0>(my_tag_funcs));
            std::get<0>(my_input).set_my_tag_func(std::get<0>(my_input).my_original_func()->clone());
            std::get<0>(my_tag_funcs) = NULL;
        }

        template< typename TagFuncTuple1, typename TagFuncTuple2>
        static inline void copy_tag_functors(TagFuncTuple1 &my_inputs, TagFuncTuple2 &other_inputs) {
            if(std::get<0>(other_inputs).my_original_func()) {
                std::get<0>(my_inputs).set_my_tag_func(std::get<0>(other_inputs).my_original_func()->clone());
                std::get<0>(my_inputs).set_my_original_tag_func(std::get<0>(other_inputs).my_original_func()->clone());
            }
        }
        template<typename InputTuple>
        static inline void reset_inputs(InputTuple &my_input) {
            std::get<0>(my_input).reinitialize_port();
        }
    };

    //! The two-phase join port
    template< typename T >
    class reserving_port : public receiver<T> {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
    private:
        // ----------- Aggregator ------------
        enum op_type { reg_pred, rem_pred, res_item, rel_res, con_res };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef reserving_port<T> my_class;

        class reserving_port_operation : public aggregated_operation<reserving_port_operation> {
        public:
            char type;
            union {
                T *my_arg;
                predecessor_type *my_pred;
            };
            reserving_port_operation(const T& e, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(&e)) {}
            reserving_port_operation(const predecessor_type &s, op_type t) : type(char(t)), 
                my_pred(const_cast<predecessor_type *>(&s)) {}
            reserving_port_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, reserving_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, reserving_port_operation>;
        aggregator<my_handler, reserving_port_operation> my_aggregator;

        void handle_operations(reserving_port_operation* op_list) {
            reserving_port_operation *current;
            bool no_predecessors;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case reg_pred:
                    no_predecessors = my_predecessors.empty();
                    my_predecessors.add(*(current->my_pred));
                    if ( no_predecessors ) {
                        my_join->decrement_port_count( ); // may try to forward
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case rem_pred:
                    my_predecessors.remove(*(current->my_pred));
                    if(my_predecessors.empty()) my_join->increment_port_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case res_item:
                    if ( reserved ) {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    else if ( my_predecessors.try_reserve( *(current->my_arg) ) ) {
                        reserved = true;
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    } else {
                        if ( my_predecessors.empty() ) {
                            my_join->increment_port_count();
                        }
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    break;
                case rel_res:
                    reserved = false;
                    my_predecessors.try_release( );
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case con_res:
                    reserved = false;
                    my_predecessors.try_consume( );
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                }
            }
        }

    public:

        //! Constructor
        reserving_port() : reserved(false) {
            my_join = NULL;
            my_predecessors.set_owner( this );
            my_aggregator.initialize_handler(my_handler(this));
        }

        // copy constructor
        reserving_port(const reserving_port& /* other */) : receiver<T>() {
            reserved = false;
            my_join = NULL;
            my_predecessors.set_owner( this );
            my_aggregator.initialize_handler(my_handler(this));
        }

        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        // always rejects, so arc is reversed (and reserves can be done.)
        bool try_put( const T & ) {
            return false;
        }

        //! Add a predecessor
        bool register_predecessor( sender<T> &src ) {
            reserving_port_operation op_data(src, reg_pred);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Remove a predecessor
        bool remove_predecessor( sender<T> &src ) {
            reserving_port_operation op_data(src, rem_pred);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Reserve an item from the port
        bool reserve( T &v ) {
            reserving_port_operation op_data(v, res_item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Release the port
        void release( ) {
            reserving_port_operation op_data(rel_res);
            my_aggregator.execute(&op_data);
        }

        //! Complete use of the port
        void consume( ) {
            reserving_port_operation op_data(con_res);
            my_aggregator.execute(&op_data);
        }

        void reinitialize_port() {
            my_predecessors.reset();
            reserved = false;
        }

    protected:

        /*override*/void reset_receiver() {
            my_predecessors.reset();
        }

    private:
        forwarding_base *my_join;
        reservable_predecessor_cache< T, null_mutex > my_predecessors;
        bool reserved;
    };

    //! queueing join_port
    template<typename T>
    class queueing_port : public receiver<T>, public item_buffer<T> {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
        typedef queueing_port<T> my_node_type;

    // ----------- Aggregator ------------
    private:
        enum op_type { try__put, get__item, res_port };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef queueing_port<T> my_class;

        class queueing_port_operation : public aggregated_operation<queueing_port_operation> {
        public:
            char type;
            T my_val;
            T *my_arg;
            // constructor for value parameter
            queueing_port_operation(const T& e, op_type t) :
                // type(char(t)), my_val(const_cast<T>(e)) {}
                type(char(t)), my_val(e) {}
            // constructor for pointer parameter
            queueing_port_operation(const T* p, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(p)) {}
            // constructor with no parameter
            queueing_port_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, queueing_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, queueing_port_operation>;
        aggregator<my_handler, queueing_port_operation> my_aggregator;

        void handle_operations(queueing_port_operation* op_list) {
            queueing_port_operation *current;
            bool was_empty;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case try__put:
                    was_empty = this->buffer_empty();
                    this->push_back(current->my_val);
                    if (was_empty) my_join->decrement_port_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case get__item:
                    if(!this->buffer_empty()) {
                        this->fetch_front(*(current->my_arg));
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    else {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    break;
                case res_port:
                    __TBB_ASSERT(this->item_valid(this->my_head), "No item to reset");
                    this->invalidate_front(); ++(this->my_head);
                    if(this->item_valid(this->my_head)) {
                        my_join->decrement_port_count();
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                }
            }
        }
    // ------------ End Aggregator ---------------
    public:

        //! Constructor
        queueing_port() : item_buffer<T>() {
            my_join = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        //! copy constructor
        queueing_port(const queueing_port& /* other */) : receiver<T>(), item_buffer<T>() {
            my_join = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        //! record parent for tallying available items
        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        /*override*/bool try_put(const T &v) {
            queueing_port_operation op_data(v, try__put);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }


        bool get_item( T &v ) {
            queueing_port_operation op_data(&v, get__item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        // reset_port is called when item is accepted by successor, but
        // is initiated by join_node.
        void reset_port() {
            queueing_port_operation op_data(res_port);
            my_aggregator.execute(&op_data);
            return;
        }

        void reinitialize_port() {
            item_buffer<T>::reset();
        }

    protected:

        /*override*/void reset_receiver() {
            // nothing to do.  We queue, so no predecessor cache
        }

    private:
        forwarding_base *my_join;
    };

#include "_flow_graph_tagged_buffer_impl.h"

    template< typename T >
    class tag_matching_port : public receiver<T>, public tagged_buffer< tag_value, T, NO_TAG > {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
        typedef tag_matching_port<T> my_node_type;  // for forwarding, if needed
        typedef function_body<input_type, tag_value> my_tag_func_type;
        typedef tagged_buffer<tag_value,T,NO_TAG> my_buffer_type;
    private:
// ----------- Aggregator ------------
    private:
        enum op_type { try__put, get__item, res_port };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef tag_matching_port<T> my_class;

        class tag_matching_port_operation : public aggregated_operation<tag_matching_port_operation> {
        public:
            char type;
            T my_val;
            T *my_arg;
            tag_value my_tag_value;
            // constructor for value parameter
            tag_matching_port_operation(const T& e, op_type t) :
                // type(char(t)), my_val(const_cast<T>(e)) {}
                type(char(t)), my_val(e) {}
            // constructor for pointer parameter
            tag_matching_port_operation(const T* p, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(p)) {}
            // constructor with no parameter
            tag_matching_port_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, tag_matching_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, tag_matching_port_operation>;
        aggregator<my_handler, tag_matching_port_operation> my_aggregator;

        void handle_operations(tag_matching_port_operation* op_list) {
            tag_matching_port_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case try__put: {
                        bool was_inserted = this->tagged_insert(current->my_tag_value, current->my_val);
                        // return failure if a duplicate insertion occurs
                        __TBB_store_with_release(current->status, was_inserted ? SUCCEEDED : FAILED);
                    }
                    break;
                case get__item:
                    // use current_tag from FE for item
                    if(!this->tagged_find(my_join->current_tag, *(current->my_arg))) {
                        __TBB_ASSERT(false, "Failed to find item corresponding to current_tag.");
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case res_port:
                    // use current_tag from FE for item
                    this->tagged_delete(my_join->current_tag);
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                }
            }
        }
// ------------ End Aggregator ---------------
    public:

        tag_matching_port() : receiver<T>(), tagged_buffer<tag_value, T, NO_TAG>() {
            my_join = NULL;
            my_tag_func = NULL;
            my_original_tag_func = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        // copy constructor
        tag_matching_port(const tag_matching_port& /*other*/) : receiver<T>(), tagged_buffer<tag_value,T, NO_TAG>() {
            my_join = NULL;
            // setting the tag methods is done in the copy-constructor for the front-end.
            my_tag_func = NULL;
            my_original_tag_func = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        ~tag_matching_port() {
            if (my_tag_func) delete my_tag_func;
            if (my_original_tag_func) delete my_original_tag_func;
        }

        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        void set_my_original_tag_func(my_tag_func_type *f) {
            my_original_tag_func = f;
        }

        void set_my_tag_func(my_tag_func_type *f) {
            my_tag_func = f;
        }

        /*override*/bool try_put(const T& v) {
            tag_matching_port_operation op_data(v, try__put);
            op_data.my_tag_value = (*my_tag_func)(v);
            my_aggregator.execute(&op_data);
            if(op_data.status == SUCCEEDED) {
                // the assertion in the aggregator above will ensure we do not call with the same
                // tag twice.  We have to exit the aggregator to keep lock-ups from happening;
                // the increment of the tag hash table in the FE is under a separate aggregator,
                // so that is serialized.
                // is a race possible?  I do not believe so; the increment may cause a build of
                // an output tuple, but its component is already in the hash table for the port.
                my_join->increment_tag_count(op_data.my_tag_value);  // may spawn

            }
            return op_data.status == SUCCEEDED;
        }


        bool get_item( T &v ) {
            tag_matching_port_operation op_data(&v, get__item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        // reset_port is called when item is accepted by successor, but
        // is initiated by join_node.
        void reset_port() {
            tag_matching_port_operation op_data(res_port);
            my_aggregator.execute(&op_data);
            return;
        }

        my_tag_func_type *my_func() { return my_tag_func; }
        my_tag_func_type *my_original_func() { return my_original_tag_func; }

        void reinitialize_port() {
            my_buffer_type::reset();
        }

    protected:

        /*override*/void reset_receiver() {
            // nothing to do.  We queue, so no predecessor cache
        }

    private:
        // need map of tags to values
        forwarding_base *my_join;
        my_tag_func_type *my_tag_func;
        my_tag_func_type *my_original_tag_func;
    };  // tag_matching_port

    using namespace graph_policy_namespace;

    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_base;

    //! join_node_FE : implements input port policy
    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_FE;

    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<reserving, InputTuple, OutputTuple> : public forwarding_base {
    public:
        static const int N = std::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef join_node_base<reserving, InputTuple, OutputTuple> my_node_type; // for forwarding

        join_node_FE(graph &g) : forwarding_base(g.root_task()), my_node(NULL) {
            ports_with_no_inputs = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(other.my_root_task), my_node(NULL) {
            ports_with_no_inputs = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

       void increment_port_count() {
            ++ports_with_no_inputs;
        }

        // if all input_ports have predecessors, spawn forward to try and consume tuples
        void decrement_port_count() {
            if(ports_with_no_inputs.fetch_and_decrement() == 1) {
                task::enqueue( * new ( task::allocate_additional_child_of( *(this->my_root_task) ) )
                    forward_task<my_node_type>(*my_node) );
            }
        }

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset() {
            // called outside of parallel contexts
            ports_with_no_inputs = N;
            join_helper<N>::reset_inputs(my_inputs);
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {
            return !ports_with_no_inputs;
        }

        bool try_to_make_tuple(output_type &out) {
            if(ports_with_no_inputs) return false;
            return join_helper<N>::reserve(my_inputs, out);
        }

        void tuple_accepted() {
            join_helper<N>::consume_reservations(my_inputs);
        }
        void tuple_rejected() {
            join_helper<N>::release_reservations(my_inputs);
        }

        input_type my_inputs;
        my_node_type *my_node;
        atomic<size_t> ports_with_no_inputs;
    };

    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<queueing, InputTuple, OutputTuple> : public forwarding_base {
    public:
        static const int N = std::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef join_node_base<queueing, InputTuple, OutputTuple> my_node_type; // for forwarding

        join_node_FE(graph &g) : forwarding_base(g.root_task()), my_node(NULL) {
            ports_with_no_items = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(other.my_root_task), my_node(NULL) {
            ports_with_no_items = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        // needed for forwarding
        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

        void reset_port_count() {
            ports_with_no_items = N;
        }

        // if all input_ports have items, spawn forward to try and consume tuples
        void decrement_port_count() {
            if(ports_with_no_items.fetch_and_decrement() == 1) {
                task::enqueue( * new ( task::allocate_additional_child_of( *(this->my_root_task) ) )
                    forward_task<my_node_type>(*my_node) );
            }
        }

        void increment_port_count() { __TBB_ASSERT(false, NULL); }  // should never be called

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset() {
            reset_port_count();
            join_helper<N>::reset_inputs(my_inputs);
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {
            return !ports_with_no_items;
        }

        bool try_to_make_tuple(output_type &out) {
            if(ports_with_no_items) return false;
            return join_helper<N>::get_items(my_inputs, out);
        }

        void tuple_accepted() {
            reset_port_count();
            join_helper<N>::reset_ports(my_inputs);
        }
        void tuple_rejected() {
            // nothing to do.
        }

        input_type my_inputs;
        my_node_type *my_node;
        atomic<size_t> ports_with_no_items;
    };

    // tag_matching join input port.
    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<tag_matching, InputTuple, OutputTuple> : public forwarding_base,
             public tagged_buffer<tag_value, size_t, NO_TAG>, public item_buffer<OutputTuple> {
    public:
        static const int N = std::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef tagged_buffer<tag_value, size_t, NO_TAG> my_tag_buffer;
        typedef item_buffer<output_type> output_buffer_type;
        typedef join_node_base<tag_matching, InputTuple, OutputTuple> my_node_type; // for forwarding

// ----------- Aggregator ------------
        // the aggregator is only needed to serialize the access to the hash table.
        // and the output_buffer_type base class
    private:
        enum op_type { res_count, inc_count, may_succeed, try_make };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef join_node_FE<tag_matching, InputTuple, OutputTuple> my_class;

        class tag_matching_FE_operation : public aggregated_operation<tag_matching_FE_operation> {
        public:
            char type;
            union {
                tag_value my_val;
                output_type* my_output;
            };
            // constructor for value parameter
            tag_matching_FE_operation(const tag_value& e, op_type t) :
                // type(char(t)), my_val(const_cast<T>(e)) {}
                type(char(t)), my_val(e) {}
            tag_matching_FE_operation(output_type *p, op_type t) :
                type(char(t)), my_output(p) {}
            // constructor with no parameter
            tag_matching_FE_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, tag_matching_FE_operation> my_handler;
        friend class internal::aggregating_functor<my_class, tag_matching_FE_operation>;
        aggregator<my_handler, tag_matching_FE_operation> my_aggregator;

        // called from aggregator, so serialized
        void fill_output_buffer(bool should_enqueue) {
            output_type l_out;
            bool do_fwd = should_enqueue && this->buffer_empty();
            while(find_value_tag(this->current_tag,N)) {
                this->tagged_delete(this->current_tag);
                if(join_helper<N>::get_items(my_inputs, l_out)) {  //  <== call back
                    this->push_back(l_out);
                    if(do_fwd) {
                        task::enqueue( * new ( task::allocate_additional_child_of( *(this->my_root_task) ) )
                        forward_task<my_node_type>(*my_node) );
                        do_fwd = false;
                    }
                    // retire the input values
                    join_helper<N>::reset_ports(my_inputs);  //  <== call back
                    this->current_tag = NO_TAG;    
                }
                else {
                    __TBB_ASSERT(false, "should have had something to push");
                }
            }
        }

        void handle_operations(tag_matching_FE_operation* op_list) {
            tag_matching_FE_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case res_count:  // called from BE
                    {
                        output_type l_out;
                        this->pop_front(l_out);  // don't care about returned value.
                        // buffer as many tuples as we can make
                        fill_output_buffer(true);
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                case inc_count: {  // called from input ports
                        size_t *p = 0;
                        tag_value t = current->my_val;
                        if(!(this->tagged_find_ref(t,p))) {
                            this->tagged_insert(t, 0);
                            if(!(this->tagged_find_ref(t,p))) {
                                __TBB_ASSERT(false, NULL);
                            }
                        }
                        if(++(*p) == size_t(N)) {
                            task::enqueue( * new ( task::allocate_additional_child_of( *(this->my_root_task) ) )
                            forward_task<my_node_type>(*my_node) );
                        }
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case may_succeed:  // called from BE
                    fill_output_buffer(false);
                    __TBB_store_with_release(current->status, this->buffer_empty() ? FAILED : SUCCEEDED);
                    break;
                case try_make:  // called from BE
                    if(this->buffer_empty()) {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    else {
                        this->fetch_front(*(current->my_output));
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                }
            }
        }
// ------------ End Aggregator ---------------

    public:
        template<typename FunctionTuple>
        join_node_FE(graph &g, FunctionTuple tag_funcs) : forwarding_base(g.root_task()), my_node(NULL) {
            join_helper<N>::set_join_node_pointer(my_inputs, this);
            join_helper<N>::set_tag_func(my_inputs, tag_funcs);
            my_aggregator.initialize_handler(my_handler(this));
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(other.my_root_task), my_tag_buffer(),
        output_buffer_type() {
            my_node = NULL;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
            join_helper<N>::copy_tag_functors(my_inputs, const_cast<input_type &>(other.my_inputs));
            my_aggregator.initialize_handler(my_handler(this));
        }

        // needed for forwarding
        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

        void reset_port_count() {  // called from BE
            tag_matching_FE_operation op_data(res_count);
            my_aggregator.execute(&op_data);
            return;
        }

        // if all input_ports have items, spawn forward to try and consume tuples
        void increment_tag_count(tag_value t) {  // called from input_ports
            tag_matching_FE_operation op_data(t, inc_count);
            my_aggregator.execute(&op_data);
            return;
        }

        void decrement_port_count() { __TBB_ASSERT(false, NULL); }

        void increment_port_count() { __TBB_ASSERT(false, NULL); }  // should never be called

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset() {
            // called outside of parallel contexts
            join_helper<N>::reset_inputs(my_inputs);

            my_tag_buffer::reset();  // have to reset the tag counts
            output_buffer_type::reset();  // also the queue of outputs
            my_node->current_tag = NO_TAG;
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {  // called from back-end
            tag_matching_FE_operation op_data(may_succeed);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        // cannot lock while calling back to input_ports.  current_tag will only be set
        // and reset under the aggregator, so it will remain consistent.
        bool try_to_make_tuple(output_type &out) {
            tag_matching_FE_operation op_data(&out,try_make);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        void tuple_accepted() {
            reset_port_count();  // reset current_tag after ports reset.
        }

        void tuple_rejected() {
            // nothing to do.
        }

        input_type my_inputs;  // input ports
        my_node_type *my_node;
    }; // join_node_FE<tag_matching, InputTuple, OutputTuple>

    //! join_node_base
    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_base : public graph_node, public join_node_FE<JP, InputTuple, OutputTuple>,
                           public sender<OutputTuple> {
        using graph_node::my_graph;
    public:
        typedef OutputTuple output_type;

        typedef receiver<output_type> successor_type;
        typedef join_node_FE<JP, InputTuple, OutputTuple> input_ports_type;
        using input_ports_type::tuple_build_may_succeed;
        using input_ports_type::try_to_make_tuple;
        using input_ports_type::tuple_accepted;
        using input_ports_type::tuple_rejected;

    private:
        // ----------- Aggregator ------------
        enum op_type { reg_succ, rem_succ, try__get, do_fwrd };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef join_node_base<JP,InputTuple,OutputTuple> my_class;

        class join_node_base_operation : public aggregated_operation<join_node_base_operation> {
        public:
            char type;
            union {
                output_type *my_arg;
                successor_type *my_succ;
            };
            join_node_base_operation(const output_type& e, op_type t) :
                type(char(t)), my_arg(const_cast<output_type*>(&e)) {}
            join_node_base_operation(const successor_type &s, op_type t) : type(char(t)), 
                my_succ(const_cast<successor_type *>(&s)) {}
            join_node_base_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, join_node_base_operation> my_handler;
        friend class internal::aggregating_functor<my_class, join_node_base_operation>;
        bool forwarder_busy;
        aggregator<my_handler, join_node_base_operation> my_aggregator;

        void handle_operations(join_node_base_operation* op_list) {
            join_node_base_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case reg_succ:
                    my_successors.register_successor(*(current->my_succ));
                    if(tuple_build_may_succeed() && !forwarder_busy) {
                        task::enqueue( * new ( task::allocate_additional_child_of(*(this->my_root_task)) )
                                forward_task<join_node_base<JP,InputTuple,OutputTuple> >(*this));
                        forwarder_busy = true;
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case rem_succ:
                    my_successors.remove_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case try__get:
                    if(tuple_build_may_succeed()) {
                        if(try_to_make_tuple(*(current->my_arg))) {
                            tuple_accepted();
                            __TBB_store_with_release(current->status, SUCCEEDED);
                        }
                        else __TBB_store_with_release(current->status, FAILED);
                    }
                    else __TBB_store_with_release(current->status, FAILED);
                    break;
                case do_fwrd: {
                        bool build_succeeded;
                        output_type out;
                        if(tuple_build_may_succeed()) {
                            do {
                                build_succeeded = try_to_make_tuple(out);
                                if(build_succeeded) {
                                    if(my_successors.try_put(out)) {
                                        tuple_accepted();
                                    }
                                    else {
                                        tuple_rejected();
                                        build_succeeded = false;
                                    }
                                }
                            } while(build_succeeded);
                        }
                        __TBB_store_with_release(current->status, SUCCEEDED);
                        forwarder_busy = false;
                    }
                    break;
                }
            }
        }
        // ---------- end aggregator -----------
    public:
        join_node_base(graph &g) : graph_node(g), input_ports_type(g), forwarder_busy(false) {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        join_node_base(const join_node_base& other) :
            graph_node(other.my_graph), input_ports_type(other),
            sender<OutputTuple>(), forwarder_busy(false), my_successors() {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        template<typename FunctionTuple>
        join_node_base(graph &g, FunctionTuple f) : graph_node(g), input_ports_type(g, f), forwarder_busy(false) {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        bool register_successor(successor_type &r) {
            join_node_base_operation op_data(r, reg_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool remove_successor( successor_type &r) {
            join_node_base_operation op_data(r, rem_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool try_get( output_type &v) {
            join_node_base_operation op_data(v, try__get);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

    protected:

        /*override*/void reset() {
            input_ports_type::reset();
        }

    private:
        broadcast_cache<output_type, null_rw_mutex> my_successors;

        friend class forward_task< join_node_base<JP, InputTuple, OutputTuple> >;

        void forward() {
            join_node_base_operation op_data(do_fwrd);
            my_aggregator.execute(&op_data);
        }
    };

    // join base class type generator
    template<int N, template<class> class PT, typename OutputTuple, graph_buffer_policy JP>
    struct join_base {
        typedef typename internal::join_node_base<JP, typename wrap_tuple_elements<N,PT,OutputTuple>::type, OutputTuple> type;
    };

    //! unfolded_join_node : passes input_ports_type to join_node_base.  We build the input port type
    //  using tuple_element.  The class PT is the port type (reserving_port, queueing_port, tag_matching_port)
    //  and should match the graph_buffer_policy.

    template<int N, template<class> class PT, typename OutputTuple, graph_buffer_policy JP>
    class unfolded_join_node : public join_base<N,PT,OutputTuple,JP>::type {
    public:
        typedef typename wrap_tuple_elements<N, PT, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<JP, input_ports_type, output_type > base_type;
    public:
        unfolded_join_node(graph &g) : base_type(g) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    // tag_matching unfolded_join_node.  This must be a separate specialization because the constructors
    // differ.

    template<typename OutputTuple>
    class unfolded_join_node<2,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<2,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
    public:
        typedef typename wrap_tuple_elements<2,tag_matching_port,OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename std::tuple< f0_p, f1_p > func_initializer_type;
    public:
        template<typename B0, typename B1>
        unfolded_join_node(graph &g, B0 b0, B1 b1) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<3,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<3,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
    public:
        typedef typename wrap_tuple_elements<3, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<4,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<4,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
    public:
        typedef typename wrap_tuple_elements<4, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<5,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<5,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
    public:
        typedef typename wrap_tuple_elements<5, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

#if __TBB_VARIADIC_MAX >= 6
    template<typename OutputTuple>
    class unfolded_join_node<6,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<6,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
        typedef typename std::tuple_element<5, OutputTuple>::type T5;
    public:
        typedef typename wrap_tuple_elements<6, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 7
    template<typename OutputTuple>
    class unfolded_join_node<7,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<7,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
        typedef typename std::tuple_element<5, OutputTuple>::type T5;
        typedef typename std::tuple_element<6, OutputTuple>::type T6;
    public:
        typedef typename wrap_tuple_elements<7, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 8
    template<typename OutputTuple>
    class unfolded_join_node<8,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<8,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
        typedef typename std::tuple_element<5, OutputTuple>::type T5;
        typedef typename std::tuple_element<6, OutputTuple>::type T6;
        typedef typename std::tuple_element<7, OutputTuple>::type T7;
    public:
        typedef typename wrap_tuple_elements<8, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 9
    template<typename OutputTuple>
    class unfolded_join_node<9,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<9,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
        typedef typename std::tuple_element<5, OutputTuple>::type T5;
        typedef typename std::tuple_element<6, OutputTuple>::type T6;
        typedef typename std::tuple_element<7, OutputTuple>::type T7;
        typedef typename std::tuple_element<8, OutputTuple>::type T8;
    public:
        typedef typename wrap_tuple_elements<9, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename internal::function_body<T8, tag_value> *f8_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p, f8_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7, typename B8>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7, B8 b8) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7),
                    new internal::function_body_leaf<T8, tag_value, B8>(b8)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 10
    template<typename OutputTuple>
    class unfolded_join_node<10,tag_matching_port,OutputTuple,tag_matching> : public 
            join_base<10,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename std::tuple_element<0, OutputTuple>::type T0;
        typedef typename std::tuple_element<1, OutputTuple>::type T1;
        typedef typename std::tuple_element<2, OutputTuple>::type T2;
        typedef typename std::tuple_element<3, OutputTuple>::type T3;
        typedef typename std::tuple_element<4, OutputTuple>::type T4;
        typedef typename std::tuple_element<5, OutputTuple>::type T5;
        typedef typename std::tuple_element<6, OutputTuple>::type T6;
        typedef typename std::tuple_element<7, OutputTuple>::type T7;
        typedef typename std::tuple_element<8, OutputTuple>::type T8;
        typedef typename std::tuple_element<9, OutputTuple>::type T9;
    public:
        typedef typename wrap_tuple_elements<10, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename internal::function_body<T8, tag_value> *f8_p;
        typedef typename internal::function_body<T9, tag_value> *f9_p;
        typedef typename std::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p, f8_p, f9_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7, typename B8, typename B9>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7, B8 b8, B9 b9) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7),
                    new internal::function_body_leaf<T8, tag_value, B8>(b8),
                    new internal::function_body_leaf<T9, tag_value, B9>(b9)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

    //! templated function to refer to input ports of the join node
    template<size_t N, typename JNT>
    typename std::tuple_element<N, typename JNT::input_ports_type>::type &input_port(JNT &jn) {
        return std::get<N>(jn.input_ports());
    }

} 
#endif // __TBB__flow_graph_join_impl_H

