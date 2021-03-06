/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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

#include <daemon/buckets.h>
#include <daemon/cccp_notification_task.h>
#include <daemon/mcbp.h>
#include <daemon/memcached.h>
#include <daemon/session_cas.h>

#include "executors.h"

void get_cluster_config_executor(McbpConnection* c, void*) {
    auto& bucket = c->getBucket();
    if (bucket.type == BucketType::NoBucket) {
        if (c->isXerrorSupport()) {
            c->getCookieObject().setErrorContext("No bucket selected");
            mcbp_write_packet(c, PROTOCOL_BINARY_RESPONSE_NO_BUCKET);
        } else {
            LOG_NOTICE(c,
                       "%u: Can't get cluster configuration without "
                       "selecting a bucket. Disconnecting %s",
                       c->getId(),
                       c->getDescription().c_str());
            c->setState(McbpStateMachine::State::closing);
        }
        return;
    }

    auto pair = c->getBucket().clusterConfiguration.getConfiguration();
    if (pair.first == -1) {
        mcbp_write_packet(c, PROTOCOL_BINARY_RESPONSE_KEY_ENOENT);
    } else {
        mcbp_response_handler(nullptr,
                              0,
                              nullptr,
                              0,
                              pair.second->data(),
                              uint32_t(pair.second->size()),
                              PROTOCOL_BINARY_RAW_BYTES,
                              PROTOCOL_BINARY_RESPONSE_SUCCESS,
                              0,
                              c->getCookie());
        mcbp_write_and_free(c, &c->getDynamicBuffer());
        c->setClustermapRevno(pair.first);
    }
}

void set_cluster_config_executor(McbpConnection* c, void* packet) {
    auto& bucket = c->getBucket();
    if (bucket.type == BucketType::NoBucket) {
        if (c->isXerrorSupport()) {
            c->getCookieObject().setErrorContext("No bucket selected");
            mcbp_write_packet(c, PROTOCOL_BINARY_RESPONSE_NO_BUCKET);
        } else {
            LOG_NOTICE(c,
                       "%u: Can't set cluster configuration without "
                       "selecting a bucket. Disconnecting %s",
                       c->getId(),
                       c->getDescription().c_str());
            c->setState(McbpStateMachine::State::closing);
        }
        return;
    }

    // First validate that the provided configuration is a valid payload
    const auto* req = reinterpret_cast<const cb::mcbp::Request*>(packet);
    auto cas = req->getCas();

    // verify that this is a legal session cas:
    if (!session_cas.increment_session_counter(cas)) {
        mcbp_write_packet(c, PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS);
        return;
    }

    try {
        auto payload = req->getValue();
        cb::const_char_buffer conf{
                reinterpret_cast<const char*>(payload.data()), payload.size()};
        bucket.clusterConfiguration.setConfiguration(conf);
        c->setCAS(cas);
        mcbp_write_packet(c, cb::mcbp::Status::Success);

        const long revision =
                bucket.clusterConfiguration.getConfiguration().first;

        LOG_NOTICE(c,
                   "%u: %s Updated cluster configuration for bucket [%s]. New "
                   "revision: %u",
                   c->getId(),
                   c->getDescription().c_str(),
                   bucket.name,
                   revision);

        // Start an executor job to walk through the connections and tell
        // them to push new clustermaps
        std::shared_ptr<Task> task;
        task = std::make_shared<CccpNotificationTask>(c->getBucketIndex(),
                                                      revision);
        std::lock_guard<std::mutex> guard(task->getMutex());
        executorPool->schedule(task, true);
    } catch (const std::invalid_argument& e) {
        LOG_WARNING(c,
                    "%u: %s Failed to update cluster configuration for bucket "
                    "[%s] - %s",
                    c->getId(),
                    c->getDescription().c_str(),
                    bucket.name,
                    e.what());
        c->getCookieObject().setErrorContext(e.what());
        mcbp_write_packet(c, cb::mcbp::Status::Einval);
    } catch (const std::exception& e) {
        LOG_WARNING(c,
                    "%u: %s Failed to update cluster configuration for bucket "
                    "[%s] - %s",
                    c->getId(),
                    c->getDescription().c_str(),
                    bucket.name,
                    e.what());
        c->getCookieObject().setErrorContext(e.what());
        mcbp_write_packet(c, cb::mcbp::Status::Einternal);
    }

    session_cas.decrement_session_counter();
}
