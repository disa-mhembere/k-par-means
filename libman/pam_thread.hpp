/*
 * Copyright 2016 neurodata (http://neurodata.io/)
 * Written by Disa Mhembere (disa@jhu.edu)
 *
 * This file is part of knor
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY CURRENT_KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __KNOR_PAM_THREAD_HPP__
#define __KNOR_PAM_THREAD_HPP__

#include "thread.hpp"

namespace knor { namespace base {
    class clusters;
} }

namespace kbase = knor::base;

namespace knor {
class pam_thread : public thread {
    private:
         // Pointer to global cluster data
        std::shared_ptr<kbase::clusters> g_clusters;
        unsigned nprocrows; // How many rows to process

        pam_thread(const int node_id, const unsigned thd_id,
                const unsigned start_rid, const unsigned nprocrows,
                const unsigned ncol,
                std::shared_ptr<kbase::clusters> g_clusters,
                unsigned* cluster_assignments,
                const std::string fn);
    public:
        static thread::ptr create(
                const int node_id, const unsigned thd_id,
                const unsigned start_rid, const unsigned nprocrows,
                const unsigned ncol,
                std::shared_ptr<kbase::clusters> g_clusters,
                unsigned* cluster_assignments, const std::string fn) {
            return thread::ptr(
                    new pam_thread(node_id, thd_id, start_rid,
                        nprocrows, ncol, g_clusters,
                        cluster_assignments, fn));
        }

        void start(const thread_state_t state);
        // Allocate and move data using this thread
        void EM_step();
        const unsigned get_global_data_id(const unsigned row_id) const;
        void run();
        void wait();
        void sleep();
        void wake(thread_state_t state);
        const void print_local_data() const;
};
}
#endif