#include "duckdb/common/radix.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/execution/index/art/art_key.hpp"
#include "duckdb/execution/index/art/art.hpp"

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
    if (argc!=3) {
        printf("usage: %s n 0|1|2\nn: number of keys\n0: sorted keys\n1: dense keys\n2: sparse keys\n", argv[0]);
        exit(1);
    }

    const int32_t num_keys = atoi(argv[1]);

    unique_ptr<ART> index;

    // Check: src/execution/index/art/art.cpp void ART::GenerateKeys(DataChunk &input, vector<unique_ptr<Key>> &insert_keys)
    // Here is a table with one column. ART is built upon this column.
    vector<int32_t> in_art_input_data;
    vector<unique_ptr<Key>> insert_keys;
    vector<unique_ptr<Key>> in_art_keys;

    vector<int32_t> not_in_art_input_data;
    vector<unique_ptr<Key>> not_in_art_keys;

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
        printf("%ul,insert(M operation/s),%f\n", in_art_input_data.size(), in_art_input_data.size() / ((gettime() - start)) / 1000000.0);
	}

    {
        /// LookupInputData
        unsigned repeat = 100000000 / in_art_input_data.size();
        if (repeat < 1) repeat = 1;
        double start = gettime();
        for (unsigned r = 0;r < repeat; ++r) {
			// Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
			for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
				auto __attribute__((unused)) leaf = static_cast<Leaf *>(index->Lookup(index->tree, *in_art_keys[idx], 0));
			}
		}
        printf("%ul,search(M operation/s),%f\n", in_art_input_data.size(), in_art_input_data.size() * repeat / ((gettime()-start)) / 1000000.0);
    }

    return 0;
}
