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

#ifndef __TBB__flow_graph_or_impl_H
#define __TBB__flow_graph_or_impl_H

#ifndef __TBB_flow_graph_H
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

#if TBB_PREVIEW_GRAPH_NODES
#include "tbb/internal/_flow_graph_types_impl.h"

namespace internal {

    // Output of the or_node is a struct containing a std::tuple, and will be of
    // the form
    //
    //  struct {
    //     size_t indx;
    //     tuple_types result;
    //  };
    //
    //  where the value of indx will indicate which result was put to the
    //  successor. So if oval is the output to the successor, indx == 0 
    //  means std::get<0>(oval.result) is the output, and so on.
    //
    //  tuple_types is the tuple that specified the possible outputs (and
    //  the corresponding inputs to the or_node.)
    //
    //  the types of each element are represented by tuple_types, a typedef
    //  in the or_node.  So the 2nd type in the union that is the
    //  output type for an or_node OrType is
    //
    //      std::tuple_element<1,OrType::tuple_types>::type

    // the struct has an OutputTuple default constructed, with element index assigned
    // the actual output value.
    template<typename OutputTuple>
    struct or_output_type {
        typedef OutputTuple tuple_types;
        typedef struct {
            size_t indx;
            OutputTuple result;
        } type;
    };

    template<typename TupleTypes,int N>
    struct or_item_helper {
        template<typename OutputType>
        static inline void create_output_value(OutputType &o, void *v) {
            o.indx = N;
            std::get<N>(o.result) = *(reinterpret_cast<typename std::tuple_element<N, TupleTypes>::type *>(v));
        }
    };

    template<typename TupleTypes,int N>
    struct or_helper {
        template<typename OutputType>
        static inline void create_output(OutputType &o, size_t i, void* v) {
            if(i == N-1) {
                or_item_helper<TupleTypes,N-1>::create_output_value(o,v);
            }
            else
                or_helper<TupleTypes,N-1>::create_output(o,i,v);
        }
        template<typename PortTuple, typename PutBase>
        static inline void set_or_node_pointer(PortTuple &my_input, PutBase *p) {
            std::get<N-1>(my_input).set_up(p, N-1);
            or_helper<TupleTypes,N-1>::set_or_node_pointer(my_input, p);
        }
    };

    template<typename TupleTypes>
    struct or_helper<TupleTypes,1> {
        template<typename OutputType>
        static inline void create_output(OutputType &o, size_t i, void* v) {
            if(i == 0) {
                or_item_helper<TupleTypes,0>::create_output_value(o,v);
            }
        }
        template<typename PortTuple, typename PutBase>
        static inline void set_or_node_pointer(PortTuple &my_input, PutBase *p) {
            std::get<0>(my_input).set_up(p, 0);
        }
    };

    struct put_base {
        virtual bool try_put_with_index(size_t index, void *v) = 0;
        virtual ~put_base() { }
    };

    template<typename T>
    class or_input_port : public receiver<T> {
    private:
        size_t my_index;
        put_base *my_or_node;
    public:
        void set_up(put_base *p, size_t i) { my_index = i; my_or_node = p; }
        bool try_put(const T &v) {
            return my_or_node->try_put_with_index(my_index, reinterpret_cast<void *>(const_cast<T*>(&v)));
        }
    protected:
        /*override*/void reset_receiver() {}
    };

    template<typename InputTuple, typename OutputType, typename StructTypes>
    class or_node_FE : public put_base {
    public:
        static const int N = std::tuple_size<InputTuple>::value;
        typedef OutputType output_type;
        typedef InputTuple input_type;

        or_node_FE( ) {
            or_helper<StructTypes,N>::set_or_node_pointer(my_inputs, this);
        }

        input_type &input_ports() { return my_inputs; }
    protected:
        input_type my_inputs;
    };

    //! or_node_base
    template<typename InputTuple, typename OutputType, typename StructTypes>
    class or_node_base : public graph_node, public or_node_FE<InputTuple, OutputType,StructTypes>,
                           public sender<OutputType> {
       using graph_node::my_graph;
    public:
        static const size_t N = std::tuple_size<InputTuple>::value;
        typedef OutputType output_type;
        typedef StructTypes tuple_types;
        typedef receiver<output_type> successor_type;
        typedef or_node_FE<InputTuple, output_type,StructTypes> input_ports_type;

    private:
        // ----------- Aggregator ------------
        enum op_type { reg_succ, rem_succ, try__put };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef or_node_base<InputTuple,output_type,StructTypes> my_class;

        class or_node_base_operation : public aggregated_operation<or_node_base_operation> {
        public:
            char type;
            size_t indx;
            union {
                void *my_arg;
                successor_type *my_succ;
            };
            or_node_base_operation(size_t i, const void* e, op_type t) :
                type(char(t)), indx(i), my_arg(const_cast<void *>(e)) {}
            or_node_base_operation(const successor_type &s, op_type t) : type(char(t)), 
                my_succ(const_cast<successor_type *>(&s)) {}
            or_node_base_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, or_node_base_operation> my_handler;
        friend class internal::aggregating_functor<my_class, or_node_base_operation>;
        aggregator<my_handler, or_node_base_operation> my_aggregator;

        void handle_operations(or_node_base_operation* op_list) {
            or_node_base_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {

                case reg_succ:
                    my_successors.register_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;

                case rem_succ:
                    my_successors.remove_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;

                case try__put:
                    output_type oval;
                    or_helper<tuple_types,N>::create_output(oval,current->indx,current->my_arg);
                    bool res = my_successors.try_put(oval);
                    __TBB_store_with_release(current->status, res ? SUCCEEDED : FAILED);
                    break;
                }
            }
        }
        // ---------- end aggregator -----------
    public:
        or_node_base(graph& g) : graph_node(g), input_ports_type() {
            my_successors.set_owner(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        or_node_base(const or_node_base& other) : graph_node(other.my_graph), input_ports_type(), sender<output_type>() {
            my_successors.set_owner(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        bool register_successor(successor_type &r) {
            or_node_base_operation op_data(r, reg_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool remove_successor( successor_type &r) {
            or_node_base_operation op_data(r, rem_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool try_put_with_index(size_t indx, void *v) {
            or_node_base_operation op_data(indx, v, try__put);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

    protected:
        /*override*/void reset() {}

    private:
        broadcast_cache<output_type, null_rw_mutex> my_successors;
    };

    // type generators
    template<typename OutputTuple>
    struct or_types {
        static const int N = std::tuple_size<OutputTuple>::value;
        typedef typename wrap_tuple_elements<N,or_input_port,OutputTuple>::type input_ports_type;
        typedef typename or_output_type<OutputTuple>::type output_type;
        typedef internal::or_node_FE<input_ports_type,output_type,OutputTuple> or_FE_type;
        typedef internal::or_node_base<input_ports_type, output_type, OutputTuple> or_base_type;
    };

    template<class OutputTuple>
    class unfolded_or_node : public or_types<OutputTuple>::or_base_type {
    public:
        typedef typename or_types<OutputTuple>::input_ports_type input_ports_type;
        typedef OutputTuple tuple_types;
        typedef typename or_types<OutputTuple>::output_type output_type;
    private:
        typedef typename or_types<OutputTuple>::or_base_type base_type;
    public:
        unfolded_or_node(graph& g) : base_type(g) {}
        unfolded_or_node(const unfolded_or_node &other) : base_type(other) {}
    };


} /* namespace internal */
#endif  // TBB_PREVIEW_GRAPH_NODES

#endif  /* __TBB__flow_graph_or_impl_H */
