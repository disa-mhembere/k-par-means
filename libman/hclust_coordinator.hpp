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
#ifndef __KNOR_HCLUST_COORDINATOR_HPP__
#define __KNOR_HCLUST_COORDINATOR_HPP__

#include <mutex>
#include <unordered_map>

#include "coordinator.hpp"
#include "util.hpp"

namespace knor {

namespace base {
    class clusters;
    class h_clusters;
    class thd_safe_bool_vector;
}

class hclust_id_generator;
static std::shared_ptr<hclust_id_generator> ider = nullptr;

class hclust_coordinator : public coordinator {
    protected:
        std::unordered_map<unsigned, std::shared_ptr<base::clusters>> hcltrs;
        std::unordered_map<unsigned, unsigned> nchanged;
        // Whether a particular cluster is cluster is still actively splitting
        std::vector<bool>* cltr_active_vec;
        std::default_random_engine ui_generator;
        std::uniform_int_distribution<unsigned> ui_distribution;
        std::mutex _mutex;
        // Keep track of the parent cluster id (partition id)
        std::vector<unsigned> part_id;
        unsigned curr_nclust;

        // Override the vector in coordinator.hpp
        std::unordered_map<unsigned, size_t> cluster_assignment_counts;

        hclust_coordinator(const std::string fn, const size_t nrow,
                const size_t ncol, const unsigned k, const unsigned max_iters,
                const unsigned nnodes, const unsigned nthreads,
                const double* centers, const base::init_t it,
                const double tolerance, const base::dist_t dt);

    public:
        static coordinator::ptr create(const std::string fn,
                const size_t nrow,
                const size_t ncol, const unsigned k, const unsigned max_iters,
                const unsigned nnodes, const unsigned nthreads,
                const double* centers=NULL, const std::string init="kmeanspp",
                const double tolerance=-1, const std::string dist_type="eucl") {

            base::init_t _init_t = base::get_init_type(init);
            base::dist_t _dist_t = base::get_dist_type(dist_type);
#if KM_TEST
#ifndef BIND
            printf("hclust coordinator => NUMA nodes: %u, nthreads: %u, "
                    "nrow: %lu, ncol: %lu, init: '%s', dist_t: '%s', fn: '%s'"
                    "\n\n", nnodes, nthreads, nrow, ncol, init.c_str(),
                    dist_type.c_str(), fn.c_str());
#endif
#endif
            return coordinator::ptr(
                    new hclust_coordinator(fn, nrow, ncol, k, max_iters,
                    nnodes, nthreads, centers, _init_t, tolerance, _dist_t));
        }

        void run_hinit();
        unsigned forgy_select(const unsigned cid);
        void print_active_clusters();

        // Pass file handle to threads to read & numa alloc
        virtual base::cluster_t run(double* allocd_data=NULL,
            const bool numa_opt=false) override;
        //void update_clusters();
        void kmeanspp_init() override;
        void random_partition_init() override;
        void forgy_init() override;
        virtual void preprocess_data() {
            throw knor::base::abstract_exception();
        }
        std::shared_ptr<hclust_id_generator> get_ider();
        virtual void build_thread_state() override;
        virtual void init_splits();
        void accumulate_cluster_counts();
        ~hclust_coordinator();
};
}
#endif
