//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/index/art/node16.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once
#include "duckdb/execution/index/art/node.hpp"

namespace duckdb {

class Node16 : public Node {
public:
	Node16(ART &art, size_t compression_lengthh);

	uint8_t key[16];
	unique_ptr<Node, void(*)(void*)> child[16];

public:
	//! Get position of a byte, returns -1 if not exists
	idx_t GetChildPos(uint8_t k) override;
	//! Get the position of the first child that is greater or equal to the specific byte, or INVALID_INDEX if there are
	//! no children matching the criteria
	idx_t GetChildGreaterEqual(uint8_t k, bool &equal) override;
	//! Get the next position in the node, or INVALID_INDEX if there is no next position
	idx_t GetNextPos(idx_t pos) override;
	//! Get Node16 Child
	unique_ptr<Node, void(*)(void*)> *GetChild(idx_t pos) override;

	idx_t GetMin() override;

	//! Insert node into Node16
	static void Insert(ART &art, unique_ptr<Node, void(*)(void*)> &node, uint8_t key_byte, unique_ptr<Node, void(*)(void*)> &child);
	//! Shrink to node 4
	static void Erase(ART &art, unique_ptr<Node> &node, int pos);
};
} // namespace duckdb

//namespace std {
//template<>
//class default_delete<duckdb::Node16> {
//public:
//    void operator()(duckdb::Node16 *ptr) {
//        std::cout << "Node16 Delete" << std::endl; //TODO(jigao): only a checker
//        delete ptr;
//    }
//};
//} // namespace std
