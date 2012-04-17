/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include "config.h"
#include "ep_engine.h"

static ThreadLocal<EventuallyPersistentEngine*> *th;

/**
 * Object registry link hook for getting the registry thread local
 * installed.
 */
class installer {
public:
   installer() {
      if (th == NULL) {
         th = new ThreadLocal<EventuallyPersistentEngine*>();
      }
   }
} install;

static bool verifyEngine(EventuallyPersistentEngine *engine)
{
   if (engine == NULL) {
       if (getenv("ALLOW_NO_STATS_UPDATE") != NULL) {
           return false;
       } else {
           assert(engine);
       }
   }
   return true;
}


void ObjectRegistry::onCreateBlob(Blob *blob)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.currentSize.incr(blob->getSize());
       stats.totalValueSize.incr(blob->getSize());
       assert(stats.currentSize.get() < GIGANTOR);
   }
}

void ObjectRegistry::onDeleteBlob(Blob *blob)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.currentSize.decr(blob->getSize());
       stats.totalValueSize.decr(blob->getSize());
       assert(stats.currentSize.get() < GIGANTOR);
   }
}

void ObjectRegistry::onCreateQueuedItem(QueuedItem *qi)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.memOverhead.incr(qi->size());
       assert(stats.memOverhead.get() < GIGANTOR);
   }
}

void ObjectRegistry::onDeleteQueuedItem(QueuedItem *qi)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.memOverhead.decr(qi->size());
       assert(stats.memOverhead.get() < GIGANTOR);
   }
}

void ObjectRegistry::onCreateItem(Item *pItem)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.memOverhead.incr(pItem->size() - pItem->getValMemSize());
       assert(stats.memOverhead.get() < GIGANTOR);
   }
}

void ObjectRegistry::onDeleteItem(Item *pItem)
{
   EventuallyPersistentEngine *engine = th->get();
   if (verifyEngine(engine)) {
       EPStats &stats = engine->getEpStats();
       stats.memOverhead.decr(pItem->size() - pItem->getValMemSize());
       assert(stats.memOverhead.get() < GIGANTOR);
   }
}

EventuallyPersistentEngine *ObjectRegistry::onSwitchThread(EventuallyPersistentEngine *engine,
                                                           bool want_old_thread_local)
{
    EventuallyPersistentEngine *old_engine = NULL;
    if (want_old_thread_local) {
        old_engine = th->get();
    }
    th->set(engine);
    return old_engine;
}

bool ObjectRegistry::memoryAllocated(size_t mem) {
   EventuallyPersistentEngine *engine = th->get();
   if (!engine) {
        return false;
   }
   EPStats &stats = engine->getEpStats();
   stats.totalMemory.incr(mem);
   return true;
}

bool ObjectRegistry::memoryDeallocated(size_t mem) {
   EventuallyPersistentEngine *engine = th->get();
   if (!engine) {
        return false;
   }
   EPStats &stats = engine->getEpStats();
   stats.totalMemory.decr(mem);
   return true;
}
