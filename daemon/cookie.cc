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

#include "cookie.h"
#include "connection_mcbp.h"

#include <cJSON_utils.h>

const std::string& Cookie::getErrorJson() {
    json_message.clear();
    if (error_context.empty() && event_id.empty()) {
        return json_message;
    }

    unique_cJSON_ptr root(cJSON_CreateObject());
    unique_cJSON_ptr error(cJSON_CreateObject());
    if (!error_context.empty()) {
        cJSON_AddStringToObject(error.get(), "context", error_context.c_str());
    }
    if (!event_id.empty()) {
        cJSON_AddStringToObject(error.get(), "ref", event_id.c_str());
    }
    cJSON_AddItemToObject(root.get(), "error", error.release());
    json_message = to_string(root, false);
    return json_message;
}

cb::const_byte_buffer Cookie::getPacket() const {
    auto buffer = connection.read->rdata();

    if (buffer.size() < sizeof(cb::mcbp::Request)) {
        // we don't have the header, so we can't even look at the body
        // length
        throw std::logic_error("Cookie::getPacket(): header not available");
    }

    const auto* req = reinterpret_cast<const cb::mcbp::Request*>(buffer.data());
    const size_t packetsize = sizeof(cb::mcbp::Request) + req->getBodylen();

    if (buffer.size() < packetsize) {
        throw std::logic_error("Cookie::getPacket(): Body not available");
    }

    return cb::const_byte_buffer{buffer.data(), packetsize};
}
