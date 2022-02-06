#include "duckdb/common/radix.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/execution/index/art/art_key.hpp"
#include "duckdb/execution/index/art/art.hpp"

#include "PerfEvent.hpp"
#include "zipf_table_distribution.hpp"

#include <sys/time.h>

#include <string>
#include <set>

using namespace duckdb;

static inline double gettime(void) {
    struct timeval now_tv;
    gettimeofday(&now_tv,NULL);
    return ((double)now_tv.tv_sec) + ((double)now_tv.tv_usec)/1000000.0;  /// Unit: Second
}

int main(int argc,char** argv) {
    if (argc!=5) {
        printf("usage: %s n 0|1|2 u|z alpha\nn: number of keys\n0: sorted keys\n1: dense keys\n2: sparse keys\n"
               "u: uniform distributed lookup\nz: zipfian distributed lookup\n"
               "alpha: the factor of the zipfian distribution\n", argv[0]);
        return 1;
    }

    const int32_t num_keys = atoi(argv[1]);

    unique_ptr<ART> index;

    // Check: src/execution/index/art/art.cpp void ART::GenerateKeys(DataChunk &input, vector<unique_ptr<Key>> &insert_keys)
    // Here is a table with one column. ART is built upon this column.
    vector<int32_t> in_art_input_data;    /// Keys in data type: int32_t. The original keys.
    vector<unique_ptr<Key>> insert_keys;  /// Moved into the ART, after the insertion.
    vector<unique_ptr<Key>> in_art_keys;  /// Keys in data type: Key. The same order as insertion order.
    vector<unique_ptr<Key>> look_up_art_keys;  /// Keys to be looked up.

    {
        /// Set Up
        // Check: src/include/duckdb/planner/operator/logical_create_index.hpp
        vector<column_t> column_ids{0}; // Build index on a table with only one column.
        vector<unique_ptr<Expression>> unbound_expressions;

        // Check: src/execution/operator/schema/physical_create_index.cpp
        index = make_unique<ART>(column_ids, unbound_expressions, false);
    }

    {
        /// Parse argv[2]
        /// Input Key \in [1, num_keys]
        // sorted dense keys
        in_art_input_data.reserve(num_keys);
        for (int32_t i = 1; i <= num_keys; ++i) in_art_input_data.emplace_back(i);
        if (atoi(argv[2]) == 1) {
            // shuffle dense keys
            std::random_shuffle(in_art_input_data.begin(), in_art_input_data.end());
        }
        if (atoi(argv[2]) == 2) {
            // create sparse, unique keys
            int32_t count = 0;
            std::set<int32_t> keySet;
            std::random_device dev;
            std::mt19937 rng(dev());
            std::uniform_int_distribution<std::mt19937::result_type> dist(1, std::numeric_limits<int32_t>::max());
            while (count < num_keys) {
                in_art_input_data[count] = dist(rng);
                if (!keySet.count(in_art_input_data[count])) keySet.insert(in_art_input_data[count++]);
            }
        }
    }

    {
        /// InsertInputData
        insert_keys.clear();
        in_art_keys.clear();
        // Check: src/execution/index/art/art.cpp static void TemplatedGenerateKeys(Vector &input, idx_t count, vector<unique_ptr<Key>> &insert_keys, bool is_little_endian)
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            insert_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
            in_art_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
        }

        double start = gettime();
        // Check: src/execution/index/art/art.cpp bool ART::Insert(IndexLock &lock, DataChunk &input, Vector &row_ids)
        // now insert the elements into the index
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            row_t row_id = idx;
            bool __attribute__((unused)) insert_result = index->Insert(index->tree, move(insert_keys[idx]), 0, row_id);
        }
        printf("%lu,insert(M operation/s),%f\n", in_art_input_data.size(), in_art_input_data.size() / ((gettime() - start)) / 1000000.0);
	}


    const int iteration = 10;

    /// Parse argv[4]
    const double alpha = atof(argv[4]);
    std::cout << "alpha := " << alpha << std::endl;

    /// Parse argv[3]
    if (argv[3][0] == 'u') {
        look_up_art_keys.clear();
        /// uniform distributed lookup == the original ART lookup procedure
        /// just copy the key array :D
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            look_up_art_keys.push_back(
                    Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
        }
/// Lookup 1 2 3 4 5 6: might hit the cache
/// To shuffle the input to be unsorted
//        std::random_device rd;
//        std::mt19937 g(rd());
//        std::shuffle(lookup_keys, lookup_keys + n, g);
    } else if (argv[3][0] == 'z') {
        look_up_art_keys.clear();
        /// zipfian distributed lookup
        const int n = in_art_input_data.size();
        std::random_device rd;
        std::mt19937 gen(rd());
        zipf_table_distribution<> zipf(n, alpha);  /// zipf distribution \in [1, n]
        std::vector<unsigned long> vec;
        std::set<unsigned long> set;
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
//            for (int i = 0; i < n; ++i) {
            const unsigned long zipf_gen_index = zipf(gen) - 1;
            vec.emplace_back(zipf_gen_index);
            set.emplace(zipf_gen_index);
            look_up_art_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[zipf_gen_index],
                                                               index->is_little_endian));/// Fix zipfian distribution's value range to [0, n)
        }
         std::cout << "lookup indexes as vector: " << std::endl; for (const auto& ele : vec)  std::cout << ele << std::endl;
         std::cout << "lookup indexes as set: #=" << set.size() << std::endl; for (const auto& ele : set)  std::cout << ele << std::endl;
    }

    for (int i = 0; i < iteration; ++i) {
//        {
//            /// LookupInputData: Warm Up
//            unsigned repeat = 100000000 / in_art_input_data.size();
//            if (repeat < 1) repeat = 1;
//            for (unsigned r = 0; r < repeat; ++r) {
//                // Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
//                for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
//                    auto __attribute__((unused)) leaf = static_cast<Leaf *>(index->Lookup(index->tree,
//                                                                                          *in_art_keys[idx], 0));
//                }
//            }
//        }
//
//        {
//            /// LookupInputData: Warm Up
//            unsigned repeat = 100000000 / in_art_input_data.size();
//            if (repeat < 1) repeat = 1;
//            for (unsigned r = 0; r < repeat; ++r) {
//                // Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
//                for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
//                    auto __attribute__((unused)) leaf = static_cast<Leaf *>(index->Lookup(index->tree,
//                                                                                          *look_up_art_keys[idx], 0));
//                }
//            }
//        }

        {
            /// LookupInputData
//        unsigned repeat = 100000000 / in_art_input_data.size();
//        if (repeat < 1) repeat = 1;
            unsigned repeat = 1;
            const int n = in_art_input_data.size();
//unsigned repeat = 10; // TODO: :D
            double start = gettime();
            PerfEvent e;
            e.startCounters();
            int cap = 0;
            for (unsigned r = 0; r < repeat; ++r) {
                // Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
                for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
                    auto leaf = static_cast<Leaf *>(index->Lookup(index->tree,*look_up_art_keys[idx], 0));
                    // TODO: return value is in EAX
                    cap += leaf->capacity; // Make sure the compiler doesn't compile away leaf
                    cap += leaf->num_elements; // Make sure the compiler doesn't compile away leaf
                }
            }
            std::cout << cap << std::endl; // Make sure the compiler doesn't compile away leaf
            printf("%lu,search(M operation/s),%f\n", in_art_input_data.size(),
                   in_art_input_data.size() * repeat / ((gettime() - start)) / 1000000.0);
            e.stopCounters();

            std::string output = "|";
            output += std::to_string(alpha) + ",";
            const double throughput = (n * repeat / 1000000.0) / (gettime() - start);
            output += std::to_string(throughput) + ",";
            for (unsigned i = 0; i < e.events.size(); i++) {
                if (e.names[i] == "cycles" || e.names[i] == "L1-misses" || e.names[i] == "LLC-misses" ||
                    e.names[i] == "dTLB-load-misses") {
                    output += std::to_string(e.events[i].readCounter() / n) + ",";
                }
            }
            output.pop_back();
            std::cout << output << std::endl;
            e.printReport(std::cout, in_art_input_data.size()); // use n as scale factor
            std::cout << std::endl;
        }

    }
    return 0;
}
