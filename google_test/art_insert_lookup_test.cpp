#include "duckdb/common/radix.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/execution/index/art/art_key.hpp"
#include "duckdb/execution/index/art/art.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {
using namespace duckdb;

class ARTTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check: src/include/duckdb/planner/operator/logical_create_index.hpp
        vector<column_t> column_ids{0}; // Build index on a table with only one column.
        vector<unique_ptr<Expression>> unbound_expressions;

        // Check: src/execution/operator/schema/physical_create_index.cpp
        index = make_unique<ART>(column_ids, unbound_expressions, false);
    }

    void TearDown() override {}

	void InsertInputData() {
		// Check: src/execution/index/art/art.cpp static void TemplatedGenerateKeys(Vector &input, idx_t count, vector<unique_ptr<Key>> &insert_keys, bool is_little_endian)
		for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
			insert_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
			in_art_keys.push_back(Key::CreateKey<int32_t>(in_art_input_data.data()[idx], index->is_little_endian));
		}

		// Check: src/execution/index/art/art.cpp bool ART::Insert(IndexLock &lock, DataChunk &input, Vector &row_ids)
		// now insert the elements into the index
		for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
			row_t row_id = idx;
			bool insert_result = index->Insert(index->tree, move(insert_keys[idx]), 0, row_id);
			ASSERT_TRUE(insert_result);
		}
	}

    void LookupInputData() {
        // Check: src/execution/index/art/art.cpp bool ART::SearchEqual(ARTIndexScanState *state, idx_t max_count, vector<row_t> &result_ids) {
        for (idx_t idx = 0; idx < in_art_input_data.size(); ++idx) {
            auto leaf = static_cast<Leaf *>(index->Lookup(index->tree, *in_art_keys[idx], 0));
            ASSERT_TRUE(leaf);
        }

        for (idx_t idx = 0; idx < not_in_art_input_data.size(); ++idx) {
            not_in_art_keys.push_back(Key::CreateKey<int32_t>(not_in_art_input_data.data()[idx], index->is_little_endian));
        }
        for (idx_t idx = 0; idx < not_in_art_input_data.size(); ++idx) {
            auto leaf = static_cast<Leaf *>(index->Lookup(index->tree, *not_in_art_keys[idx], 0));
            ASSERT_FALSE(leaf);
        }
	}

    unique_ptr<ART> index;

    // Check: src/execution/index/art/art.cpp void ART::GenerateKeys(DataChunk &input, vector<unique_ptr<Key>> &insert_keys)
    // Here is a table with one column. ART is built upon this column.
    vector<int32_t> in_art_input_data;
    vector<unique_ptr<Key>> insert_keys;
    vector<unique_ptr<Key>> in_art_keys;

    vector<int32_t> not_in_art_input_data;
    vector<unique_ptr<Key>> not_in_art_keys;
};

TEST_F(ARTTest, EasyARTTest) {
	// Prepare the input data.
    in_art_input_data = {0,1,2,3,4,5,6,7,8,9};
    not_in_art_input_data = {10,11,12,13,14};

	// Check the input data.
    InsertInputData();
	LookupInputData();
}

TEST_F(ARTTest, AnotherEasyARTTest) {
    // Prepare the input data.
    in_art_input_data = {10,11,12,13,14};
    not_in_art_input_data = {0,1,2,3,4,5,6,7,8,9};

    // Check the input data.
    InsertInputData();
    LookupInputData();
}

}  // namespace