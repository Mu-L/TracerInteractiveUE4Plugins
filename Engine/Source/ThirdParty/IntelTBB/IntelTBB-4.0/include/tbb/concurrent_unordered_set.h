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

/* Container implementations in this header are based on PPL implementations
   provided by Microsoft. */

#ifndef __TBB_concurrent_unordered_set_H
#define __TBB_concurrent_unordered_set_H

#include "internal/_concurrent_unordered_impl.h"

namespace tbb
{

namespace interface5 {

// Template class for hash set traits
template<typename Key, typename Hash_compare, typename Allocator, bool Allow_multimapping>
class concurrent_unordered_set_traits
{
protected:
    typedef Key value_type;
    typedef Key key_type;
    typedef Hash_compare hash_compare;
    typedef typename Allocator::template rebind<value_type>::other allocator_type;
    enum { allow_multimapping = Allow_multimapping };

    concurrent_unordered_set_traits() : my_hash_compare() {}
    concurrent_unordered_set_traits(const hash_compare& hc) : my_hash_compare(hc) {}

    typedef hash_compare value_compare;

    static const Key& get_key(const value_type& value) {
        return value;
    }

    hash_compare my_hash_compare; // the comparator predicate for keys
};

template <typename Key, typename Hasher = tbb::tbb_hash<Key>, typename Key_equality = std::equal_to<Key>, typename Allocator = tbb::tbb_allocator<Key> >
class concurrent_unordered_set : public internal::concurrent_unordered_base< concurrent_unordered_set_traits<Key, internal::hash_compare<Key, Hasher, Key_equality>, Allocator, false> >
{
    // Base type definitions
    typedef internal::hash_compare<Key, Hasher, Key_equality> hash_compare;
    typedef internal::concurrent_unordered_base< concurrent_unordered_set_traits<Key, hash_compare, Allocator, false> > base_type;
    typedef concurrent_unordered_set_traits<Key, internal::hash_compare<Key, Hasher, Key_equality>, Allocator, false> traits_type;
    using traits_type::my_hash_compare;
#if __TBB_EXTRA_DEBUG
public:
#endif
    using traits_type::allow_multimapping;
public:
    using base_type::end;
    using base_type::find;
    using base_type::insert;

    // Type definitions
    typedef Key key_type;
    typedef typename base_type::value_type value_type;
    typedef Key mapped_type;
    typedef Hasher hasher;
    typedef Key_equality key_equal;
    typedef hash_compare key_compare;

    typedef typename base_type::allocator_type allocator_type;
    typedef typename base_type::pointer pointer;
    typedef typename base_type::const_pointer const_pointer;
    typedef typename base_type::reference reference;
    typedef typename base_type::const_reference const_reference;

    typedef typename base_type::size_type size_type;
    typedef typename base_type::difference_type difference_type;

    typedef typename base_type::iterator iterator;
    typedef typename base_type::const_iterator const_iterator;
    typedef typename base_type::iterator local_iterator;
    typedef typename base_type::const_iterator const_local_iterator;

    // Construction/destruction/copying
    explicit concurrent_unordered_set(size_type n_of_buckets = 8, const hasher& a_hasher = hasher(),
        const key_equal& a_keyeq = key_equal(), const allocator_type& a = allocator_type())
        : base_type(n_of_buckets, key_compare(a_hasher, a_keyeq), a)
    {
    }

    concurrent_unordered_set(const Allocator& a) : base_type(8, key_compare(), a)
    {
    }

    template <typename Iterator>
    concurrent_unordered_set(Iterator first, Iterator last, size_type n_of_buckets = 8, const hasher& a_hasher = hasher(),
        const key_equal& a_keyeq = key_equal(), const allocator_type& a = allocator_type())
        : base_type(n_of_buckets, key_compare(a_hasher, a_keyeq), a)
    {
        for (; first != last; ++first)
            base_type::insert(*first);
    }

    concurrent_unordered_set(const concurrent_unordered_set& table) : base_type(table)
    {
    }

    concurrent_unordered_set(const concurrent_unordered_set& table, const Allocator& a)
        : base_type(table, a)
    {
    }

    concurrent_unordered_set& operator=(const concurrent_unordered_set& table)
    {
        base_type::operator=(table);
        return (*this);
    }

    iterator unsafe_erase(const_iterator where)
    {
        return base_type::unsafe_erase(where);
    }

    size_type unsafe_erase(const key_type& key)
    {
        return base_type::unsafe_erase(key);
    }

    iterator unsafe_erase(const_iterator first, const_iterator last)
    {
        return base_type::unsafe_erase(first, last);
    }

    void swap(concurrent_unordered_set& table)
    {
        base_type::swap(table);
    }

    // Observers
    hasher hash_function() const
    {
        return my_hash_compare.my_hash_object;
    }

    key_equal key_eq() const
    {
        return my_hash_compare.my_key_compare_object;
    }
};

} // namespace interface5

using interface5::concurrent_unordered_set;

} // namespace tbb

#endif// __TBB_concurrent_unordered_set_H
