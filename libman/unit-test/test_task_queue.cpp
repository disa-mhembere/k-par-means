/**
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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <pthread.h>
#include <atomic>

#include "task_queue.hpp"
#include "io.hpp"
#include "util.hpp"

void test_queue_get() {
    constexpr unsigned NTHREADS = 1;
    printf("\n\nRunning test_queue_get with"
            " constexpr NTHREADS = %u...\n", NTHREADS);
    constexpr unsigned nrow = 50;
    constexpr unsigned ncol = 5;
    const std::string fn = "/mnt/nfs/disa/data/tiny/matrix_r50_c5_rrw.bin";

    kpmbase::bin_reader<double> br(fn, nrow, ncol);
    double* data = new double [nrow*ncol];
    printf("Bin read data\n");
    br.read(data);
#if 0
    printf("Raw data print out ...\n");
    print_mat<double>(data, nrow, ncol);
    printf("END Raw data ...\n\n");
#endif
    kpmeans::task_queue q(data, 0, nrow, ncol);
    printf("Task queue ==> nrow: %u, ncol: %u\n",
            q.get_nrow(), nrow);

    printf("run:");
    for (unsigned i = 0; i < 4; i++) {
        printf(" %u", i);
        // Test reset
        q.reset();
        while(q.has_task()) {
            kpmeans::task* t = q.get_task();
#if 0
            print_mat<double>(t->get_data_ptr(), t->get_nrow(), ncol);
#endif
            BOOST_VERIFY(kpmbase::eq_all<double>(
                        t->get_data_ptr(), &(data[t->get_start_rid()*ncol]),
                        MIN_TASK_ROWS*ncol));
            delete t;
        }
    }

    printf("\n\nTask queue test SUCCESSful! ...\n");
    delete [] data;
}

int main(int argc, char* argv[]) {
    test_queue_get();
    return (EXIT_SUCCESS);
}
