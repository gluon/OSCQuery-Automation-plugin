/*
    This file is part of the OSCQuery Automation Plugin —
    https://github.com/pob31/OSCQuery-Automation-plugin

    Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle

    The OSCQuery Automation Plugin is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the License,
    or (at your option) any later version. It is distributed in the hope that it
    will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/*
    THE COUNTING ALLOCATOR, FOR TEST EXECUTABLES ONLY (core/rt/RtCheck.h says
    why a plugin never has this). Every global operator new tells
    rt::noteAllocation() and then allocates; every delete frees.

    EVERY OVERLOAD OR NONE (go.dot's lesson, RtAllocatorHook.cpp): a
    replaced new paired with the library's delete corrupts the debug heap on
    Windows, so the whole family is here: plain, array, nothrow, aligned, sized.
    Compiled into a test executable as a source file, never into a library,
    because a replacement in a static library the linker may leave out is a
    counter that is sometimes deaf.
*/

#include <core/rt/RtCheck.h>

#include <cstdlib>
#include <new>

#if defined (_WIN32)
 #include <malloc.h>
#endif

namespace
{
    void* allocate (std::size_t size)
    {
        oqa::core::rt::noteAllocation();
        return std::malloc (size == 0 ? 1 : size);
    }

    void* allocateAligned (std::size_t size, std::size_t alignment)
    {
        oqa::core::rt::noteAllocation();

      #if defined (_WIN32)
        return _aligned_malloc (size == 0 ? 1 : size, alignment);
      #else
        void* p = nullptr;
        return posix_memalign (&p, alignment < sizeof (void*) ? sizeof (void*) : alignment, size == 0 ? 1 : size) == 0 ? p : nullptr;
      #endif
    }

    void release (void* p) noexcept
    {
        std::free (p);
    }

    void releaseAligned (void* p) noexcept
    {
      #if defined (_WIN32)
        _aligned_free (p);
      #else
        std::free (p);
      #endif
    }

    /*  Announces the replacement is in, so a test can tell "zero" from "not
        measuring". A static initialiser of this translation unit; the counters
        it talks to are constinit and need none. */
    const bool announced = []
    {
        oqa::core::rt::countingEnabled.store (true, std::memory_order_relaxed);
        return true;
    }();
}

//==============================================================================
void* operator new (std::size_t size)
{
    if (auto* p = allocate (size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (auto* p = allocate (size))
        return p;

    throw std::bad_alloc();
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept    { return allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept  { return allocate (size); }

void* operator new (std::size_t size, std::align_val_t alignment)
{
    if (auto* p = allocateAligned (size, static_cast<std::size_t> (alignment)))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    if (auto* p = allocateAligned (size, static_cast<std::size_t> (alignment)))
        return p;

    throw std::bad_alloc();
}

void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned (size, static_cast<std::size_t> (alignment));
}

//==============================================================================
void operator delete (void* p) noexcept                                           { release (p); }
void operator delete[] (void* p) noexcept                                         { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept                    { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept                  { release (p); }
void operator delete (void* p, std::size_t) noexcept                              { release (p); }
void operator delete[] (void* p, std::size_t) noexcept                            { release (p); }

void operator delete (void* p, std::align_val_t) noexcept                         { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                       { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept            { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept          { releaseAligned (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept  { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
