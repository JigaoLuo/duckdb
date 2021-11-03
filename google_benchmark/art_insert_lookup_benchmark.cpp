#include "duckdb/common/radix.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/execution/index/art/art_key.hpp"
#include "duckdb/execution/index/art/art.hpp"

#include <benchmark/benchmark.h>
#include <string>
#include <set>

using namespace duckdb;

class INT32_ART_Fixture : public benchmark::Fixture {
public:

	// Set up when new state comes.
    void SetUp(const ::benchmark::State& state) {
        // Check: src/include/duckdb/planner/operator/logical_create_index.hpp
        vector<column_t> column_ids{0}; // Build index on a table with only one column.
        vector<unique_ptr<Expression>> unbound_expressions;

        // Check: src/execution/operator/schema/physical_create_index.cpp
        index = make_unique<ART>(column_ids, unbound_expressions, false);

        const int32_t num_keys = state.range(0);
        in_art_input_data.reserve(num_keys);
        for (int32_t i = 1; i <= num_keys; ++i) in_art_input_data.emplace_back(i);

        // Check: src/execution/index/art/art.cpp static void TemplatedGenerateKeys(Vector &input, idx_t count, vector<unique_ptr<Key>> &insert_keys, bool is_little_endian)
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            in_art_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
        }
    }

    void TearDown(const ::benchmark::State& state) {}

    void GenerateSortedDenseKeys() {
        insert_keys.clear();
        // Check: src/execution/index/art/art.cpp static void TemplatedGenerateKeys(Vector &input, idx_t count, vector<unique_ptr<Key>> &insert_keys, bool is_little_endian)
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            insert_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
        }
    }

    void GenerateRandomDenseKeys() {
		/// Random Dense Keys == Shuffle Dense Key
		if (!if_shuffled) {
            std::random_shuffle(in_art_input_data.begin(), in_art_input_data.end());
        }

        insert_keys.clear();
        // Check: src/execution/index/art/art.cpp static void TemplatedGenerateKeys(Vector &input, idx_t count, vector<unique_ptr<Key>> &insert_keys, bool is_little_endian)
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            insert_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
        }
    }

	void Insert() {
        vector<column_t> column_ids;
        vector<unique_ptr<Expression>> unbound_expressions;
        index = make_unique<ART>(column_ids, unbound_expressions, false);

        // Check: src/execution/index/art/art.cpp bool ART::Insert(IndexLock &lock, DataChunk &input, Vector &row_ids)
        // now insert the elements into the index
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            row_t row_id = idx;
            bool insert_result = index->Insert(index->tree, move(insert_keys[idx]), 0, row_id);
        }
	}

	void Lookup() {
        // Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            auto leaf = static_cast<Leaf *>(index->Lookup(index->tree, *in_art_keys[idx], 0));
        }
	}

    unique_ptr<ART> index;

    // Check: src/execution/index/art/art.cpp void ART::GenerateKeys(DataChunk &input, vector<unique_ptr<Key>> &insert_keys)
    // Here is a table with one column. ART is built upon this column.
    vector<int32_t> in_art_input_data;
    vector<unique_ptr<Key>> insert_keys;
    vector<unique_ptr<Key>> in_art_keys;

	bool if_shuffled = false;
};

BENCHMARK_DEFINE_F(INT32_ART_Fixture, SortedDenseKeys_Insert_Test)(benchmark::State& state) {
	for (auto _ : state) {
		{
			state.PauseTiming();
			GenerateSortedDenseKeys();
			state.ResumeTiming();
		}
        Insert();
    }
}

BENCHMARK_REGISTER_F(INT32_ART_Fixture, SortedDenseKeys_Insert_Test)->Arg(1000)->Arg(1000000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(INT32_ART_Fixture, SortedDenseKeys_Lookup_Test)(benchmark::State& state) {
    for (auto _ : state) {
        {
            state.PauseTiming();
            GenerateSortedDenseKeys();
            Insert();
            state.ResumeTiming();
        }
        Lookup();
    }
}
BENCHMARK_REGISTER_F(INT32_ART_Fixture, SortedDenseKeys_Lookup_Test)->Arg(1000)->Arg(1000000)->Unit(benchmark::kMillisecond);


BENCHMARK_DEFINE_F(INT32_ART_Fixture, RandomDenseKeys_Insert_Test)(benchmark::State& state) {
    for (auto _ : state) {
        {
            state.PauseTiming();
            GenerateRandomDenseKeys();
            state.ResumeTiming();
        }
        Insert();
    }
}

BENCHMARK_REGISTER_F(INT32_ART_Fixture, RandomDenseKeys_Insert_Test)->Arg(1000)->Arg(1000000)->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(INT32_ART_Fixture, RandomDenseKeys_Lookup_Test)(benchmark::State& state) {
    for (auto _ : state) {
        {
            state.PauseTiming();
            GenerateRandomDenseKeys();
            Insert();
            state.ResumeTiming();
        }
        Lookup();
    }
}
BENCHMARK_REGISTER_F(INT32_ART_Fixture, RandomDenseKeys_Lookup_Test)->Arg(1000)->Arg(1000000)->Unit(benchmark::kMillisecond);



BENCHMARK_MAIN();

// Easy to run in CLion with a Run-Bottom
//int main(int argc, char** argv) {
//    ::benchmark::Initialize(&argc, argv);
//    ::benchmark::RunSpecifiedBenchmarks();
//    return 0;
//}