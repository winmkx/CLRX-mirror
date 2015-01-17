/*
 *  CLRadeonExtender - Unofficial OpenCL Radeon Extensions Library
 *  Copyright (C) 2014-2015 Mateusz Szpakowski
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*! \file Containers.h
 * \brief containers and other utils for other libraries and programs
 */

#ifndef __CLRX_CONTAINERS_H__
#define __CLRX_CONTAINERS_H__

#include <CLRX/Config.h>
#include <iterator>
#include <algorithm>
#include <initializer_list>

/// main namespace
namespace CLRX
{

/// an array class
template<typename T>
class Array
{
public:
    typedef T* iterator;
    typedef const T* const_iterator;
    typedef T element_type;
private:
    T* ptr, *ptrEnd;
public:
    Array(): ptr(nullptr), ptrEnd(nullptr)
    { }
    explicit Array(size_t N)
    {
        ptr = nullptr;
        if (N != 0)
            ptr = new T[N];
        ptrEnd = ptr+N;
    }
    
    template<typename It>
    Array(It b, It e)
    try
    {
        ptr = nullptr;
        const size_t N = e-b;
        if (N != 0)
            ptr = new T[N];
        ptrEnd = ptr+N;
        std::copy(b, e, ptr);
    }
    catch(...)
    { 
        delete[] ptr;
        throw;
    }
    
    Array(const Array& cp)
    try
    {
        ptr = ptrEnd = nullptr;
        const size_t N = cp.size();
        if (N != 0)
            ptr = new T[N];
        ptrEnd = ptr+N;
        std::copy(cp.ptr, cp.ptrEnd, ptr);
    }
    catch(...)
    {
        delete[] ptr;
        throw;
    }
    
    Array(Array&& cp) noexcept
    {
        ptr = cp.ptr;
        ptrEnd = cp.ptrEnd;
        cp.ptr = cp.ptrEnd = nullptr;
    }
    
    Array(std::initializer_list<T> list)
    try
    {
        ptr = nullptr;
        const size_t N = list.size();
        if (N != 0)
            ptr = new T[N];
        ptrEnd = ptr+N;
        std::copy(list.begin(), list.end(), ptr);
    }
    catch(...)
    {
        delete[] ptr;
        throw;
    }
    
    ~Array()
    { delete[] ptr; }
    
    Array& operator=(const Array& cp)
    {
        if (this == &cp)
            return *this;
        assign(cp.begin(), cp.end());
        return *this;
    }
    
    Array& operator=(Array&& cp) noexcept
    {
        if (this == &cp)
            return *this;
        delete[] ptr;
        ptr = cp.ptr;
        ptrEnd = cp.ptrEnd;
        cp.ptr = cp.ptrEnd = nullptr;
        return *this;
    }
    
    Array& operator=(std::initializer_list<T> list)
    {
        assign(list.begin(), list.end());
        return *this;
    }
    
    const T& operator[] (size_t i) const
    { return ptr[i]; }
    T& operator[] (size_t i)
    { return ptr[i]; }
    
    bool empty() const
    { return ptrEnd==ptr; }
    
    size_t size() const
    { return ptrEnd-ptr; }
    
    /// only allocating space without keeping previous content
    void allocate(size_t N)
    {
        if (N == size())
            return;
        delete[] ptr;
        ptr = nullptr;
        if (N != 0)
            ptr = new T[N];
        ptrEnd = ptr + N;
    }
    
    /// resize space with keeping old content
    void resize(size_t N)
    {
        if (N == size())
            return;
        T* newPtr = nullptr;
        if (N != 0)
            newPtr = new T[N];
        try
        { std::copy(ptr, ptr + std::min(N, size()), newPtr); }
        catch(...)
        {
            delete[] newPtr;
            throw;
        }
        delete[] ptr;
        ptr = newPtr;
        ptrEnd = ptr + N;
    }
    
    void clear()
    {
        delete[] ptr;
        ptr = ptrEnd = nullptr;
    }
    
    template<typename It>
    void assign(It b, It e)
    {
        const size_t N = e-b;
        if (N != size())
        {
            T* newPtr = nullptr;
            if (N != 0)
                newPtr = new T[N];
            try
            { std::copy(b, e, newPtr); }
            catch(...)
            {
                delete[] newPtr;
                throw;
            }
            delete[] ptr;
            ptr = newPtr;
            ptrEnd = ptr+N;
        }
        else // no size changed only copy
            std::copy(b, e, ptr);
    }
    
    const T* data() const
    { return ptr; }
    T* data()
    { return ptr; }
    
    const T* begin() const
    { return ptr; }
    T* begin()
    { return ptr; }
    
    const T* end() const
    { return ptrEnd; }
    T* end()
    { return ptrEnd; }
};

/// binary find helper
template<typename Iter>
Iter binaryFind(Iter begin, Iter end, const
            typename std::iterator_traits<Iter>::value_type& v);

/// binary find helper
template<typename Iter, typename Comp =
        std::less<typename std::iterator_traits<Iter>::value_type> >
Iter binaryFind(Iter begin, Iter end, const
        typename std::iterator_traits<Iter>::value_type& v, Comp comp);

/// binary find helper for array-map
template<typename Iter>
Iter binaryMapFind(Iter begin, Iter end, const 
        typename std::iterator_traits<Iter>::value_type::first_type& k);

/// binary find helper for array-map
template<typename Iter, typename Comp =
    std::less<typename std::iterator_traits<Iter>::value_type::first_type> >
Iter binaryMapFind(Iter begin, Iter end, const
        typename std::iterator_traits<Iter>::value_type::first_type& k, Comp comp);

template<typename Iter>
void mapSort(Iter begin, Iter end);

template<typename Iter, typename Comp =
    std::less<typename std::iterator_traits<Iter>::value_type::first_type> >
void mapSort(Iter begin, Iter end, Comp comp);

template<typename Iter>
Iter binaryFind(Iter begin, Iter end,
        const typename std::iterator_traits<Iter>::value_type& v)
{
    auto it = std::lower_bound(begin, end, v);
    if (it == end || v < *it)
        return end;
    return it;
}

template<typename Iter, typename Comp =
        std::less<typename std::iterator_traits<Iter>::value_type> >
Iter binaryFind(Iter begin, Iter end,
        const typename std::iterator_traits<Iter>::value_type& v, Comp comp)
{
    auto it = std::lower_bound(begin, end, v, comp);
    if (it == end || comp(v, *it))
        return end;
    return it;
}

template<typename Iter>
Iter binaryMapFind(Iter begin, Iter end, const
            typename std::iterator_traits<Iter>::value_type::first_type& k)
{
    typedef typename std::iterator_traits<Iter>::value_type::first_type K;
    typedef typename std::iterator_traits<Iter>::value_type::second_type V;
    auto it = std::lower_bound(begin, end, std::make_pair(k, V()),
               [](const std::pair<K,V>& e1, const std::pair<K,V>& e2) {
        return e1.first < e2.first; });
    if (it == end || k < it->first)
        return end;
    return it;
}

template<typename Iter, typename Comp =
     std::less<typename std::iterator_traits<Iter>::value_type::first_type> >
Iter binaryMapFind(Iter begin, Iter end, const
        typename std::iterator_traits<Iter>::value_type::first_type& k, Comp comp)
{
    typedef typename std::iterator_traits<Iter>::value_type::first_type K;
    typedef typename std::iterator_traits<Iter>::value_type::second_type V;
    auto it = std::lower_bound(begin, end, std::make_pair(k, V()),
               [&comp](const std::pair<K,V>& e1, const std::pair<K,V>& e2) {
        return comp(e1.first, e2.first); });
    if (it == end || comp(k, it->first))
        return end;
    return it;
}

template<typename Iter>
void mapSort(Iter begin, Iter end)
{
    typedef typename std::iterator_traits<Iter>::value_type::first_type K;
    typedef typename std::iterator_traits<Iter>::value_type::second_type V;
    std::sort(begin, end, [](const std::pair<K,V>& e1, const std::pair<K,V>& e2) {
        return e1.first < e2.first; });
}

template<typename Iter, typename Comp = 
    std::less<typename std::iterator_traits<Iter>::value_type::first_type> >
void mapSort(Iter begin, Iter end, Comp comp)
{
    typedef typename std::iterator_traits<Iter>::value_type::first_type K;
    typedef typename std::iterator_traits<Iter>::value_type::second_type V;
    std::sort(begin, end, [&comp](const std::pair<K,V>& e1, const std::pair<K,V>& e2) {
       return comp(e1.first, e2.first); });
}

};

#endif