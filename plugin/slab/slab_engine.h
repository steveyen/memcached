/*
 * Copyright (c) <2008>, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the  nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SUN MICROSYSTEMS, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SUN MICROSYSTEMS, INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Summary: Specification of the storage engine interface.
 *
 * Copy: See Copyright for the status of this software.
 *
 * Author: Trond Norbye <trond.norbye@sun.com>
 */

#ifndef MEMCACHED_SLAB_ENGINE_H
#define MEMCACHED_SLAB_ENGINE_H

#include <pthread.h>
#include <stdbool.h>

#include <memcached/engine.h>

/* Forward decl */
struct slabber_engine;

#include "items.h"
#include "assoc.h"
#include "slabs.h"
#include "hash.h"

#ifdef __cplusplus
extern "C" {
#endif

struct config {
   bool use_cas;
   size_t verbose;
   rel_time_t oldest_live;
   bool evict_to_free;
   size_t maxbytes;
   bool preallocate;
   float factor;
   size_t chunk_size;
};

ENGINE_ERROR_CODE create_instance(uint64_t interface, ENGINE_HANDLE **handle);

/* FIXME! This symbol shouldn't be used directly from the backend! */
rel_time_t realtime(const time_t exptime);
extern rel_time_t current_time;

/**
 * Statistic information collected by the slab engine
 */
struct stats {
   pthread_mutex_t lock;
   uint64_t evictions;
   uint64_t curr_bytes;
   uint64_t curr_items;
   uint64_t total_items;
};


/**
 * Definition of the private instance data used by the slabber engine.
 *
 * This is currently "work in progress" so it is not as clean as it should be.
 */
struct slabber_engine {
   ENGINE_HANDLE_V1 engine;

   /**
    * Is the engine initalized or not
    */
   bool initialized;

   /**
    * The cache layer (item_* and assoc_*) is currently protected by
    * this single mutex
    */
   pthread_mutex_t cache_lock;

   /**
    * Are we in the middle of expanding the assoc table now?
    */
   volatile bool assoc_expanding;

   struct config config;
   struct stats stats;
};

#endif

