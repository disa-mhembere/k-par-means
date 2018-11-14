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

#ifndef __KNOR_HCLUST_HPP__
#define __KNOR_HCLUST_HPP__

#include <unordered_map>
#include "thread.hpp"

namespace knor {
namespace base {
    class clusters;
}

typedef std::unordered_map<unsigned, std::shared_ptr<base::clusters>>
    hclust_map;
typedef std::unordered_map<unsigned, unsigned> change_map;

class hclust : public thread {
    protected:
         // Pointer to global cluster data
        hclust_map* g_hcltrs;
        hclust_map local_hcltrs;
        change_map nchanged;

        std::vector<bool>* cltr_active_vec; // Which clusters are still active

        unsigned k;
        unsigned nprocrows; // The number of rows in this threads partition

        hclust(const int node_id, const unsigned thd_id,
                const unsigned start_rid, const unsigned nprocrows,
                const unsigned ncol,
                hclust_map* g_hcltrs,
                unsigned* cluster_assignments,
                const std::string fn, base::dist_t dist_metric);
    public:
        static thread::ptr create(
                const int node_id, const unsigned thd_id,
                const unsigned start_rid, const unsigned nprocrows,
                const unsigned ncol,
                hclust_map* g_hcltrs,
                unsigned* cluster_assignments, const std::string fn,
                base::dist_t dist_metric) {
            return thread::ptr(
                        new hclust(node_id, thd_id, start_rid,
                        nprocrows, ncol, g_hcltrs,
                        cluster_assignments, fn, dist_metric));
        }

        void set_cltr_active_vec(std::vector<bool>* av) {
            cltr_active_vec = av;
        }

        void start(const thread_state_t state) override;
        // Allocate and move data using this thread
        void H_EM_step();
        void kmspp_dist();
        virtual void run() override;
};
}
#endif