#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <chrono>
#include <faiss/ProductQuantizer.h>
#include <faiss/index_io.h>
#include "hnswIndexPQ.h"
#include "hnswlib.h"

#include <map>
#include <set>
#include <unordered_set>
using namespace std;
using namespace hnswlib;

class StopW {
    std::chrono::steady_clock::time_point time_begin;
public:
    StopW() {
        time_begin = std::chrono::steady_clock::now();
    }
    float getElapsedTimeMicro() {
        std::chrono::steady_clock::time_point time_end = std::chrono::steady_clock::now();
        return (std::chrono::duration_cast<std::chrono::microseconds>(time_end - time_begin).count());
    }
    void reset() {
        time_begin = std::chrono::steady_clock::now();
    }

};


/*
* Author:  David Robert Nadeau
* Site:    http://NadeauSoftware.com/
* License: Creative Commons Attribution 3.0 Unported License
*          http://creativecommons.org/licenses/by/3.0/deed.en_US
*/


#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
#include <fcntl.h>
#include <procfs.h>

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
#include <stdio.h>

#endif

#else
#error "Cannot define getPeakRSS( ) or getCurrentRSS( ) for an unknown OS."
#endif



/**
* Returns the peak (maximum so far) resident set size (physical
* memory use) measured in bytes, or zero if the value cannot be
* determined on this OS.
*/
size_t getPeakRSS()
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
	PROCESS_MEMORY_COUNTERS info;
	GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
	return (size_t)info.PeakWorkingSetSize;

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
    /* AIX and Solaris ------------------------------------------ */
	struct psinfo psinfo;
	int fd = -1;
	if ((fd = open("/proc/self/psinfo", O_RDONLY)) == -1)
		return (size_t)0L;      /* Can't open? */
	if (read(fd, &psinfo, sizeof(psinfo)) != sizeof(psinfo))
	{
		close(fd);
		return (size_t)0L;      /* Can't read? */
	}
	close(fd);
	return (size_t)(psinfo.pr_rssize * 1024L);

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* BSD, Linux, and OSX -------------------------------------- */
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
#if defined(__APPLE__) && defined(__MACH__)
    return (size_t)rusage.ru_maxrss;
#else
    return (size_t)(rusage.ru_maxrss * 1024L);
#endif

#else
    /* Unknown OS ----------------------------------------------- */
	return (size_t)0L;          /* Unsupported. */
#endif
}


/**
* Returns the current resident set size (physical memory use) measured
* in bytes, or zero if the value cannot be determined on this OS.
*/
size_t getCurrentRSS()
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
	PROCESS_MEMORY_COUNTERS info;
	GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
	return (size_t)info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
    /* OSX ------------------------------------------------------ */
	struct mach_task_basic_info info;
	mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
		(task_info_t)&info, &infoCount) != KERN_SUCCESS)
		return (size_t)0L;      /* Can't access? */
	return (size_t)info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
    /* Linux ---------------------------------------------------- */
    long rss = 0L;
    FILE* fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) == NULL)
        return (size_t)0L;      /* Can't open? */
    if (fscanf(fp, "%*s%ld", &rss) != 1)
    {
        fclose(fp);
        return (size_t)0L;      /* Can't read? */
    }
    fclose(fp);
    return (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);

#else
    /* AIX, BSD, Solaris, and Unknown OS ------------------------ */
	return (size_t)0L;          /* Unsupported. */
#endif
}


template <typename dist_t>
static void get_gt(unsigned int *massQA, size_t qsize, vector<std::priority_queue< std::pair<dist_t, labeltype >>> &answers,
                   size_t gt_dim, size_t k = 1)
{
    (vector<std::priority_queue< std::pair<dist_t, labeltype >>>(qsize)).swap(answers);
    std::cout << qsize << "\n";
    for (int i = 0; i < qsize; i++) {
        for (int j = 0; j < k; j++) {
            answers[i].emplace(0.0f, massQA[gt_dim*i + j]);
        }
    }
}

//template <typename format>
//static void readXvec(std::ifstream &input, format *mass, const int d, const int n = 1)
//{
//    int in = 0;
//    for (int i = 0; i < n; i++) {
//        input.read((char *) &in, sizeof(int));
//        if (in != d) {
//            std::cout << "file error\n";
//            exit(1);
//        }
//        input.read((char *)(mass+i*d), in * sizeof(format));
//    }
//}

template <typename format>
static void loadXvecs(const char *path, format *mass, const int n, const int d)
{
    ifstream input(path, ios::binary);
    for (int i = 0; i < n; i++) {
        int in = 0;
        input.read((char *)&in, sizeof(int));
        if (in != d) {
            cout << "file error\n";
            exit(1);
        }
        input.read((char *)(mass + i*d), in*sizeof(format));
    }
    input.close();
}

static void check_precomputing(Index *index, const char *path_data, const char *path_precomputed_idxs,
                               size_t vecdim, size_t ncentroids, size_t vecsize,
                               std::set<idx_t> gt_mistakes, std::set<idx_t> gt_correct)
{
    size_t batch_size = 1000000;
    std::ifstream base_input(path_data, ios::binary);
    std::ifstream idx_input(path_precomputed_idxs, ios::binary);
    std::vector<float> batch(batch_size * vecdim);
    std::vector<idx_t> idx_batch(batch_size);

//    int counter = 0;
    std::vector<float> mistake_dst;
    std::vector<float> correct_dst;
    for (int b = 0; b < (vecsize / batch_size); b++) {
        readXvec<idx_t>(idx_input, idx_batch.data(), batch_size, 1);
        readXvec<float>(base_input, batch.data(), vecdim, batch_size);

        printf("%.1f %c \n", (100.*b)/(vecsize/batch_size), '%');

        for (int i = 0; i < batch_size; i++) {
            int elem = batch_size*b + i;
            //float min_dist = 1000000;
            //int min_centroid = 100000000;

            if (gt_mistakes.count(elem) == 0 &&
                gt_correct.count(elem) == 0)
                continue;

            float *data = batch.data() + i*vecdim;
            for (int j = 0; j < ncentroids; j++) {
                float *centroid = (float *) index->quantizer->getDataByInternalId(j);
                float dist = faiss::fvec_L2sqr(data, centroid, vecdim);
                //if (dist < min_dist){
                //    min_dist = dist;
                //    min_centroid = j;
                //}
                if (gt_mistakes.count(elem) != 0)
                    mistake_dst.push_back(dist);
                if (gt_correct.count(elem) != 0)
                    correct_dst.push_back(dist);
            }
//            if (min_centroid != idx_batch[i]){
//                std::cout << "Element: " << elem << " True centroid: " << min_centroid << " Precomputed centroid:" << idx_batch[i] << std::endl;
//                counter++;
//            }
        }
    }

    std::cout << "Correct distance distribution\n";
    for (int i = 0; i < correct_dst.size(); i++)
        std::cout << correct_dst[i] << std::endl;

    std::cout << std::endl << std::endl << std::endl;
    std::cout << "Mistake distance distribution\n";
    for (int i = 0; i < mistake_dst.size(); i++)
        std::cout << mistake_dst[i] << std::endl;

    idx_input.close();
    base_input.close();
}

void compute_vector(float *vector, const float *p1, const float *p2, const int d)
{
    for (int i = 0; i < d; i++)
        vector[i] = p1[i] - p2[i];
}

void normalize_vector(float *vector, const int d)
{
    float norm = faiss::fvec_norm_L2(vector, d);
    for (int i = 0; i < d; i++)
        vector[i] /= norm;
}



//void check_idea(Index *index, const char *path_centroids,
//                const char *path_precomputed_idxs, const char *path_data,
//                const int vecsize, const int vecdim)
//{
//    const int nc = 16;
//    const int centroid_num = 100;
//    /** Consider the 100th centroid **/
//    float *centroid = (float *) index->quantizer->getDataByInternalId(centroid_num);
//    auto nn_centroids_raw = index->quantizer->searchKnn((void *) centroid, nc + 1);
//
//    /** Remove the 100th centroid from answers **/
//    std::priority_queue<std::pair<float, idx_t>> nn_centroids_before_heuristic;
//    std::vector<std::pair<float, idx_t>> nn_centroids;
//    while (nn_centroids_queue.size() > 1){
//        nn_centroids_before_heuristic.emplace(nn_centroids_raw.top());
//        nn_centroids_raw.pop();
//    }
//
//    /** Pruning **/
//    index->quantizer->getNeighborsByHeuristic(nn_centroids_before_heuristic, nc);
//    std::vector<std::pair<float, idx_t>> nn_centroids;
//    while (nn_centroids_raw.size() > 0){
//        nn_centroids.push_front(nn_centroids_before_heuristic.top());
//        nn_centroids_before_heuristic.pop();
//    }
//
//    if (nn_centroids.size() > nc){
//        std::cout << "Wrong number of nn centroids\n";
//        exit(1);
//    }
//
//    /** Take 100th group element ids and codes **/
//    std::vector<idx_t> ids = index->ids[centroid_num];
//    std::vector<idx_t> codes = index->codes[centroid_num];
//    size_t groupsize = ids.size();
//
//    /** Read original vectors of the 100th group **/
//    std::vector<float> data(ids.size() * vecdim);
//    std::unordered_map<idx_t, int> ids_map;
//    for (int i = 0; i < ids.size(); i++)
//        ids_map.insert({ids[i], i});
//
//    size_t batch_size = 1000000;
//    std::ifstream base_input(path_data, ios::binary);
//    std::ifstream idx_input(path_precomputed_idxs, ios::binary);
//    std::vector<float> batch(batch_size * vecdim);
//    std::vector<idx_t> idx_batch(batch_size);
//
//    for (int b = 0; b < (vecsize / batch_size); b++) {
//        readXvec<idx_t>(idx_input, idx_batch.data(), batch_size, 1);
//        readXvec<float>(base_input, batch.data(), vecdim, batch_size);
//
//        for (size_t i = 0; i < batch_size; i++){
//            int element_num = batch_size*b + i;
//            if (ids_map.count(element_num) == 1){
//                for (int d = 0; d < vecdim; d++) {
//                    data[ids_map[element_num] * vecdim + d] = batch[i * vecdim + d];
//                    ids_map.erase(element_num);
//                }
//            }
//        }
//    }
//    idx_input.close();
//    base_input.close();
//
//    if (ids_map.size() > 0){
//        std::cout <<"Ids map must be empty\n";
//        exit(1);
//    }
//
//    /** Compute centroid-neighbor_centroid and centroid-group_point vectors **/
//    size_t ncentroids = nn_centroids.size();
//    std::vector<float> normalized_centroid_vectors (ncentroids * vecdim);
//    std::vector<float> point_vectors (ncentroids * vecdim);
//
//    for (int i = 0; i < ncentroids; i++){
//        float *neighbor_centroid = (float *) index->quantizer->getDataByInternalId(nn_centroids[i].second);
//        compute_vector(normalized_centroid_vectors.data() + i*vecdim, centroid, neighbor_centroid, vecdim);
//
//        /** Normalize them **/
//        normalize_vector(normalized_centroid_vectors.data() + i*vecdim, vecdim);
//    }
//    for (int i = 0; i < groupsize; i++) {
//        compute_vector(point_vectors.data() + i * vecdim, centroid, data.data() + i * d, vecdim);
//    }
//
//    /** Find alphas for vectors **/
//    std::vector<float> alphas(ncentroids);
//    for (int c = 0; c < ncentroids; c++) {
//        float *centroid_vector = normalized_centroid_vectors.data() + c*vecdim;
//        float alpha = 0.0;
//        int counter_positive = 0;
//
//        for (int i = 0; i < groupsize; i++) {
//            float *point_vector = point_vectors.data() + i*vecdim;
//            float inner_product = faiss::fvec_inner_product (centroid_vector, point_vector, vecdim);
//            if (inner_product < 0)
//                continue;
//            alpha += inner_product;
//            counter_positive++;
//        }
//        alpha /= counter_positive;
//        alphas[c] = alpha;
//    }
//
//    /** Compute final subcentroids **/
//    std::vector<float> sub_centroids(ncentroids * vecdim);
//    for (int c = 0; c < ncentroids; c++) {
//        float *centroid_vector = normalized_centroid_vectors.data() + c * vecdim;
//        float *sub_centroid = sub_centroids.data() + c * vecdim;
//        float alpha = alphas[c];
//        for (int i = 0; i < vecdim; i++)
//            sub_centroid[i] = centroid_vector[i] * alpha;
//    }
//
//    /** Compute sub idxs for group points **/
//    for (int i = 0; i < groupsize; i++){
//        std::priority_queue<std::pair<>>
//        for (int c = 0; c < ncentroids; c++){
//
//        }
//    }
//}

void hybrid_test(const char *path_centroids,
                 const char *path_index, const char *path_precomputed_idxs,
                 const char *path_pq, const char *path_norm_pq,
                 const char *path_learn, const char *path_data, const char *path_q,
                 const char *path_gt, const char *path_info, const char *path_edges,
                 const int k,
                 const int vecsize,
                 const int ncentroids,
                 const int qsize,
                 const int vecdim,
                 const int efConstruction,
                 const int efSearch,
                 const int M,
                 const int M_PQ,
                 const int nprobes,
                 const int max_codes)
{
    cout << "Loading GT:\n";
    const int gt_dim = 1;
    idx_t *massQA = new idx_t[qsize * gt_dim];
    loadXvecs<idx_t>(path_gt, massQA, qsize, gt_dim);

    cout << "Loading queries:\n";
    float massQ[qsize * vecdim];
    loadXvecs<float>(path_q, massQ, qsize, vecdim);

    SpaceInterface<float> *l2space = new L2Space(vecdim);

    /** Create Index **/
    Index *index = new Index(vecdim, ncentroids, M_PQ, 8);
    index->buildQuantizer(l2space, path_centroids, path_info, path_edges, 500);
    index->precompute_idx(vecsize, path_data, path_precomputed_idxs);

    /** Train PQ **/
    std::ifstream learn_input(path_learn, ios::binary);
    int nt = 65536;
    std::vector<float> trainvecs(nt * vecdim);

    readXvec<float>(learn_input, trainvecs.data(), vecdim, nt);
    learn_input.close();

    /** Train residual PQ **/
    if (exists_test(path_pq)) {
        std::cout << "Loading PQ codebook from " << path_pq << std::endl;
        read_pq(path_pq, index->pq);
    }
    else {
        index->train_residual_pq(nt, trainvecs.data());
        std::cout << "Saving PQ codebook to " << path_pq << std::endl;
        write_pq(path_pq, index->pq);
    }

    /** Train norm PQ **/
    if (exists_test(path_norm_pq)) {
        std::cout << "Loading norm PQ codebook from " << path_norm_pq << std::endl;
        read_pq(path_norm_pq, index->norm_pq);
    }
    else {
        index->train_norm_pq(nt, trainvecs.data());
        std::cout << "Saving norm PQ codebook to " << path_norm_pq << std::endl;
        write_pq(path_norm_pq, index->norm_pq);
    }


    if (exists_test(path_index)){
        /** Load Index **/
        std::cout << "Loading index from " << path_index << std::endl;
        index->read(path_index);
    } else {
        /** Add elements **/
        size_t batch_size = 1000000;
        std::ifstream base_input(path_data, ios::binary);
        std::ifstream idx_input(path_precomputed_idxs, ios::binary);
        std::vector<float> batch(batch_size * vecdim);
        std::vector<idx_t> idx_batch(batch_size);
        std::vector<idx_t> ids(vecsize);

        for (int b = 0; b < (vecsize / batch_size); b++) {
            readXvec<idx_t>(idx_input, idx_batch.data(), batch_size, 1);
            readXvec<float>(base_input, batch.data(), vecdim, batch_size);
            for (size_t i = 0; i < batch_size; i++)
                ids[batch_size*b + i] = batch_size*b + i;

            printf("%.1f %c \n", (100.*b)/(vecsize/batch_size), '%');
            index->add(batch_size, batch.data(), ids.data() + batch_size*b, idx_batch.data());
        }
        idx_input.close();
        base_input.close();

        /** Save index, pq and norm_pq **/
        std::cout << "Saving index to " << path_index << std::endl;
        std::cout << "       pq to " << path_pq << std::endl;
        std::cout << "       norm pq to " << path_norm_pq << std::endl;
        index->write(path_index);
    }
    /** Compute centroid norms **/
    std::cout << "Computing centroid norms"<< std::endl;
    index->compute_centroid_norm_table();

    /** Compute centroid sizes **/
    //std::cout << "Computing centroid sizes"<< std::endl;
    //index->compute_centroid_size_table(path_data, path_precomputed_idxs);

    /** Compute centroid vars **/
    //std::cout << "Computing centroid vars"<< std::endl;
    //index->compute_centroid_var_table(path_data, path_precomputed_idxs);

    //const char *path_index_new = "/home/dbaranchuk/hybrid8M_PQ16_new.index";
    //index->write(path_index_new);

    /** Update centroids **/
    //std::cout << "Update centroids" << std::endl;
    //index->update_centroids(path_data, path_precomputed_idxs,
    //                        "/home/dbaranchuk/data/updated_centroids4M.fvecs");

    /** Parse groundtruth **/
    vector<std::priority_queue< std::pair<float, labeltype >>> answers;
    std::cout << "Parsing gt:\n";
    get_gt<float>(massQA, qsize, answers, gt_dim);

    /** Set search parameters **/
    int correct = 0;
    idx_t results[k];

    index->max_codes = max_codes;
    index->nprobe = nprobes;
    index->quantizer->ef_ = efSearch;

    /** Search **/
//    std::set<idx_t> gt_mistakes;
//    std::set<idx_t> gt_correct;

    StopW stopw = StopW();
    for (int i = 0; i < qsize; i++) {
        index->search(massQ+i*vecdim, k, results);

        std::priority_queue<std::pair<float, labeltype >> gt(answers[i]);
        unordered_set<labeltype> g;

        while (gt.size()) {
            g.insert(gt.top().second);
            gt.pop();
        }

        for (int j = 0; j < k; j++){
            if (g.count(results[j]) != 0){
                correct++;
                break;
            }
        }

//        if (prev_correct == correct){
//            //std::cout << i << " " << answers[i].top().second << std::endl;
//            gt_mistakes.insert(answers[i].top().second);
//        } else {
//            gt_correct.insert(answers[i].top().second);
//        }
    }
    /**Represent results**/
    float time_us_per_query = stopw.getElapsedTimeMicro() / qsize;
    std::cout << "Recall@" << k << ": " << 1.0f*correct / qsize << std::endl;
    std::cout << "Time per query: " << time_us_per_query << " us" << std::endl;


    //std::cout << "Check precomputed idxs"<< std::endl;
    //check_precomputing(index, path_data, path_precomputed_idxs, vecdim, ncentroids, vecsize, gt_mistakes, gt_correct);

    delete index;
    delete massQA;
    delete l2space;
}