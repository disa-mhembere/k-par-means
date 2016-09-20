/*
 * Copyright 2016 neurodata (http://neurodata.io/)
 * Written by Disa Mhembere (disa@jhu.edu)
 *
 * This file is part of k-par-means
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

#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#include "kmeans_task_coordinator.hpp"
#include "kmeans_task_thread.hpp"
#include "common.hpp"
#include "task_queue.hpp"

namespace kpmeans { namespace prune {
kmeans_task_coordinator::kmeans_task_coordinator(const std::string fn, const size_t nrow,
        const size_t ncol, const unsigned k, const unsigned max_iters,
        const unsigned nnodes, const unsigned nthreads,
        const double* centers, const kpmbase::init_type_t it,
        const double tolerance, const kpmbase::dist_type_t dt) :
    base_kmeans_coordinator(fn, nrow, ncol, k, max_iters,
            nnodes, nthreads, centers, it, tolerance, dt) {

        cltrs = kpmbase::prune_clusters::create(k, ncol);
        if (centers) {
            cltrs->set_mean(centers);
            if (_init_t != kpmbase::init_type_t::NONE) {
                BOOST_LOG_TRIVIAL(warning) << "[WARNING]: Init method " <<
                    "ignored because centers provided!";
            } else {
                BOOST_LOG_TRIVIAL(info) << "Init-ed centers ...";
            }
        }

        // For pruning
        recalculated_v = kpmbase::thd_safe_bool_vector::create(nrow, false);
        dist_v = new double[nrow];
        std::fill(&dist_v[0], &dist_v[nrow], std::numeric_limits<double>::max());
        dm = prune::dist_matrix::create(k);

        // NUMA node affinity binding policy is round-robin
        unsigned thds_row = nrow / nthreads;
        for (unsigned thd_id = 0; thd_id < nthreads; thd_id++) {
            std::pair<unsigned, unsigned> tup = get_rid_len_tup(thd_id);
            thd_max_row_idx.push_back((thd_id*thds_row) + tup.second);
            threads.push_back(prune::kmeans_task_thread::create((thd_id % nnodes),
                        thd_id, tup.first, tup.second,
                        ncol, cltrs, cluster_assignments, fn));
            threads[thd_id]->set_parent_cond(&cond);
            threads[thd_id]->set_parent_pending_threads(&pending_threads);
            threads[thd_id]->start(WAIT); // Thread puts itself to sleep
            threads[thd_id]->set_driver(this); // For computation stealing
        }
    }


std::pair<unsigned, unsigned> kmeans_task_coordinator::get_rid_len_tup(
        const unsigned thd_id) {
    unsigned rows_per_thread = nrow / nthreads;
    unsigned start_rid = (thd_id*rows_per_thread);

    if (thd_id == nthreads - 1)
        rows_per_thread += nrow % nthreads;
    return std::pair<unsigned, unsigned>(start_rid, rows_per_thread);
}


kmeans_task_coordinator::~kmeans_task_coordinator() {
    thread_iter it = threads.begin();
    for (; it != threads.end(); ++it)
        (*it)->destroy_numa_mem();

    delete [] cluster_assignments;
    delete [] cluster_assignment_counts;
    delete [] dist_v;

    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    pthread_mutexattr_destroy(&mutex_attr);
#if 0
    delete [] g_data;
#endif
    destroy_threads();
}

void kmeans_task_coordinator::set_prune_init(const bool prune_init) {
    for (thread_iter it = threads.begin(); it != threads.end(); ++it)
        (*it)->set_prune_init(prune_init);
}

std::vector<std::shared_ptr<prune::kmeans_task_thread> >&
    kmeans_task_coordinator::get_threads() {
  return threads;
}

void kmeans_task_coordinator::set_global_ptrs() {
    for (thread_iter it = threads.begin(); it != threads.end(); ++it) {
        pthread_mutex_lock(&mutex);
        (*it)->set_dist_v_ptr(dist_v);
        (*it)->set_recalc_v_ptr(recalculated_v);
        (*it)->set_dist_mat_ptr(dm);
        pthread_mutex_unlock(&mutex);
    }
}

void kmeans_task_coordinator::destroy_threads() {
    wake4run(EXIT);
}

// <Thread, within-thread-row-id>
const double* kmeans_task_coordinator::get_thd_data(const unsigned row_id) const {
    unsigned parent_thd = std::upper_bound(thd_max_row_idx.begin(),
            thd_max_row_idx.end(), row_id) - thd_max_row_idx.begin();
    unsigned rows_per_thread = nrow/nthreads; // All but the last thread

#if 0
    printf("Global row %u, row in parent thd: %u --> %u\n", row_id,
            parent_thd, (row_id-(parent_thd*rows_per_thread)));
#endif

    return &((threads[parent_thd]->get_local_data())
            [(row_id-(parent_thd*rows_per_thread))*ncol]);
}

void kmeans_task_coordinator::update_clusters(const bool prune_init) {
    if (prune_init) {
        printf("Clearing because of init ..\n");
        cltrs->clear();
    } else {
        cltrs->set_prev_means();
        for (unsigned idx = 0; idx < k; idx++)
            cltrs->unfinalize(idx);
    }

    for (thread_iter it = threads.begin(); it != threads.end(); ++it) {
        // Updated the changed cluster count
        num_changed += (*it)->get_num_changed();
        cltrs->peq((*it)->get_local_clusters());
    }

    unsigned chk_nmemb = 0;
    for (unsigned clust_idx = 0; clust_idx < k; clust_idx++) {
        cltrs->finalize(clust_idx);
        cltrs->set_prev_dist(
                kpmbase::eucl_dist(&(cltrs->get_means()[clust_idx*ncol]),
                &(cltrs->get_prev_means()[clust_idx*ncol]), ncol), clust_idx);
#if VERBOSE
        BOOST_LOG_TRIVIAL(info) << "Dist to prev mean for c:" << clust_idx
            << " is " << cltrs->get_prev_dist(clust_idx);
#endif

        cluster_assignment_counts[clust_idx] = cltrs->get_num_members(clust_idx);
        chk_nmemb += cluster_assignment_counts[clust_idx];
    }
    printf("chk_nmemb: %u\n", chk_nmemb);
    BOOST_VERIFY(chk_nmemb == nrow);

#if KM_TEST
    BOOST_LOG_TRIVIAL(info) << "Global number of changes: " << num_changed;
#endif
}

double kmeans_task_coordinator::reduction_on_cuml_sum() {
    double tot = 0;
    for (thread_iter it = threads.begin(); it != threads.end(); ++it)
        tot += (*it)->get_culm_dist();
    return tot;
}

void kmeans_task_coordinator::wake4run(const thread_state_t state) {
    pending_threads = nthreads;
    for (unsigned thd_id = 0; thd_id < threads.size(); thd_id++)
        threads[thd_id]->wake(state);
}

void kmeans_task_coordinator::set_thread_clust_idx(const unsigned clust_idx) {
    for (thread_iter it = threads.begin(); it != threads.end(); ++it)
        (*it)->set_clust_idx(clust_idx);
}

void kmeans_task_coordinator::set_thd_dist_v_ptr(double* v) {
    for (unsigned thd_id = 0; thd_id < threads.size(); thd_id++) {
        pthread_mutex_lock(&threads[thd_id]->get_lock());
        threads[thd_id]->set_dist_v_ptr(v);
        pthread_mutex_unlock(&threads[thd_id]->get_lock());
    }
}

void kmeans_task_coordinator::kmeanspp_init() {
    struct timeval start, end;
    gettimeofday(&start , NULL);
    set_thd_dist_v_ptr(dist_v);

    // Choose c1 uniformly at random
    unsigned selected_idx = random() % nrow; // 0...(nrow-1)

    cltrs->set_mean(get_thd_data(selected_idx), 0);
    dist_v[selected_idx] = 0.0;
#if KM_TEST
    BOOST_LOG_TRIVIAL(info) << "\nChoosing "
        << selected_idx << " as center K = 0";
#endif
    unsigned clust_idx = 0; // The number of clusters assigned

    // Choose next center c_i with weighted prob
    while ((clust_idx + 1) < k) {
        set_thread_clust_idx(clust_idx); // Set the current cluster index
        wake4run(KMSPP_INIT); // Run || distance comp to clust_idx
        wait4complete();
        double cuml_dist = reduction_on_cuml_sum(); // Sum the per thread cumulative dists

        cuml_dist = (cuml_dist * ((double)random())) / (RAND_MAX - 1.0);
        clust_idx++;

        for (size_t row = 0; row < nrow; row++) {
            cuml_dist -= dist_v[row];
            if (cuml_dist <= 0) {
#if KM_TEST
                BOOST_LOG_TRIVIAL(info) << "Choosing "
                    << row << " as center k = " << clust_idx;
#endif
                cltrs->set_mean(get_thd_data(row), clust_idx);
                break;
            }
        }
        BOOST_VERIFY(cuml_dist <= 0);
    }

#if VERBOSE
    BOOST_LOG_TRIVIAL(info) << "\nCluster centers after kmeans++";
    clusters->print_means();
#endif
    gettimeofday(&end, NULL);
    BOOST_LOG_TRIVIAL(info) << "\n\nInitialization time: " <<
        kpmbase::time_diff(start, end) << " sec\n";
}

void kmeans_task_coordinator::random_partition_init() {
    for (unsigned row = 0; row < nrow; row++) {
        unsigned asgnd_clust = random() % k; // 0...k
        const double* dp = get_thd_data(row);

        cltrs->add_member(dp, asgnd_clust);
        cluster_assignments[row] = asgnd_clust;
    }

    for (unsigned cl = 0; cl < k; cl++)
        cltrs->finalize(cl);

#if VERBOSE
    printf("After rand paritions cluster_asgns: ");
    print_arr<unsigned>(cluster_assignments, nrow);
#endif
}

void kmeans_task_coordinator::forgy_init() {
    BOOST_LOG_TRIVIAL(info) << "Forgy init start";
    for (unsigned clust_idx = 0; clust_idx < k; clust_idx++) { // 0...k
        unsigned rand_idx = random() % nrow; // 0...(nrow-1)
        cltrs->set_mean(get_thd_data(rand_idx), clust_idx);
    }
    BOOST_LOG_TRIVIAL(info) << "Forgy init end";
}

void kmeans_task_coordinator::run_init() {
    switch(_init_t) {
        case kpmbase::init_type_t::RANDOM:
            random_partition_init();
            break;
        case kpmbase::init_type_t::FORGY:
            forgy_init();
            break;
        case kpmbase::init_type_t::PLUSPLUS:
            kmeanspp_init();
            break;
        case kpmbase::init_type_t::NONE:
            break;
        default:
            fprintf(stderr, "[FATAL]: Unknow initialization type\n");
            exit(EXIT_FAILURE);
    }
}

/**
 * Main driver for kmeans
 */
void kmeans_task_coordinator::run_kmeans() {
#ifdef PROFILER
    ProfilerStart("matrix/kmeans_task_coordinator.perf");
#endif
    set_global_ptrs();
    wake4run(ALLOC_DATA);
    wait4complete();

    struct timeval start, end;
    gettimeofday(&start , NULL);
    run_init(); // Initialize clusters

#if 0
    printf("printing clusters:\n");
    cltrs->print_means();
#endif

    // Init Engine
    printf("Running init engine:\n");
    wake4run(EM);
    wait4complete();
    update_clusters(true);
    set_prune_init(false);

    num_changed = 0;
    // Run kmeans loop
    size_t iter = 1;
    bool converged = false;
    while (iter <= max_iters) {
        BOOST_LOG_TRIVIAL(info) << "E-step Iteration: " << iter;

        BOOST_LOG_TRIVIAL(info) << "Main: Computing cluster distance matrix ...";
        dm->compute_dist(cltrs, ncol);

        wake4run(EM);
        wait4complete();
        update_clusters(false);

        printf("Cluster assignment counts: ");
        kpmbase::print_arr(cluster_assignment_counts, k);

        if (num_changed == 0 ||
                ((num_changed/(double)nrow)) <= tolerance) {
            converged = true;
            break;
        } else {
            num_changed = 0;
        }
        iter++;
    }
#ifdef PROFILER
    ProfilerStop();
#endif
    gettimeofday(&end, NULL);
    BOOST_LOG_TRIVIAL(info) << "\n\nAlgorithmic time taken = " <<
        kpmbase::time_diff(start, end) << " sec\n";

    BOOST_LOG_TRIVIAL(info) << "\n******************************************\n";
    if (converged) {
        BOOST_LOG_TRIVIAL(info) <<
            "K-means converged in " << iter << " iterations";
    } else {
        BOOST_LOG_TRIVIAL(warning) << "[Warning]: K-means failed to converge in "
            << iter << " iterations";
    }
    BOOST_LOG_TRIVIAL(info) << "\n******************************************\n";
}
} } // End namespace kpmeans, prune
