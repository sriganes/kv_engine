/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
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

/*
 * Test how logger handles running out of file descriptors (EMFILE)
 */

#include "logger_test_common.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <platform/cbassert.h>
#include <platform/dirutils.h>
#include <sys/resource.h>

#include <string>
#include <thread>

void wait_for_log_to_contain(FILE* log, const char* log_message) {
    // read() gives no guarantees of how the data will be chunked, so accumulate
    // one lines' worth (up to \n) in a string.

    fprintf(stderr, "Waiting for log file to contain: [%s]\n", log_message);

    std::string line;
    while (true) {
        char buffer[1024];

        auto* ptr = fgets(buffer, sizeof(buffer), log);
        if (ptr == nullptr) {
            if (feof(log)) {
                clearerr(log);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            } else if (ferror(log)) {
                perror("fgets failed");
                abort();
            }
        } else {
            line.append(ptr);
            if (line.back() == '\n') {
                // We've got a complete line
                if (line.find(log_message) != std::string::npos) {
                    fprintf(stderr, "log file to contains: [%s]\n", log_message);
                    return;
                }
                line.clear();
            }
        }
    }
}

int main() {
#ifndef WIN32
    unsetenv("CB_MAXIMIZE_LOGGER_CYCLE_SIZE");
    unsetenv("CB_MAXIMIZE_LOGGER_BUFFER_SIZE");
    unsetenv("CB_MINIMIZE_LOGGER_SLEEPTIME");
#endif

    // Timeout (and dump core) if takes longer than 30s.
    std::thread watchdog{[]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        fprintf(stderr, "Watchdog timeout!\n");
        std::abort();
    }};
    watchdog.detach();

    // Clean out any old files.
    std::vector<std::string> files;
    files = cb::io::findFilesWithPrefix("log_test_emfile");
    if (!files.empty()) {
        remove_files(files);
    }

    // Bring down out open file limit to a more conservative level (to
    // save using up a huge number of user / system FDs).
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "Failed to get getrlimit number of files\n");
        exit(1);
    }

    rlim.rlim_cur = 100;
    if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "Failed to setrlimit number of files\n");
        exit(2);
    }

    // Open the logger
    EXTENSION_ERROR_CODE ret = memcached_extensions_initialize(
            "unit_test=true;cyclesize=50;"
            "buffersize=150;sleeptime=1;filename=log_test_emfile",
            get_server_api);
    cb_assert(ret == EXTENSION_SUCCESS);

    // Wait for first log file to be created, and open it
    FILE* log_file;
    while ((log_file = fopen("log_test_emfile.000000.txt", "rb")) == nullptr) {
        usleep(10);
    }

    wait_for_log_to_contain(log_file, "Restarting file logging");

    // Consume all available FD so we cannot open any more files
    // (i.e. rotation will fail).
    std::vector<FILE*> FDs;
    FILE* file;
    while ((file = std::fopen(".", "r")) != NULL) {
        FDs.emplace_back(file);
    }

    // add log entries, enough to trigger a rotation attempt.
    logger->log(EXTENSION_LOG_DETAIL, NULL,
                "test_emfile: Log line which should be in log_test_emfile.0.log");
    logger->log(EXTENSION_LOG_DETAIL, NULL,
                "test_emfile: Log line which should be in log_test_emfile.0.log");

    // read() gives no guarantees of how the data will be chunked, so accumulate
    // one lines' worth (up to \n) in a string.
    wait_for_log_to_contain(log_file, "Failed to open next logfile");

    // Close extra FDs so we can now print.
    for (auto f : FDs) {
        std::fclose(f);
    }

    // Wait for second log file to be created, and open it
    fclose(log_file);
    while ((log_file = fopen("log_test_emfile.000001.txt", "r")) == nullptr) {
        usleep(10);
    }

    // add log entries, enough to trigger a rotation attempt.
    logger->log(EXTENSION_LOG_DETAIL, NULL,
               "test_emfile: Log line which should be in log_test_emfile.1.log");

    wait_for_log_to_contain(log_file, "Restarting file logging");

    fclose(log_file);
    logger->shutdown(false);
}
