#pragma once


// Constants for the node types
static const int8_t NodeType4=0;
static const int8_t NodeType16=1;
static const int8_t NodeType48=2;
static const int8_t NodeType256=3;

// The maximum prefix length for compressed paths stored in the
// header, if the path is longer it is loaded from the database on
// demand
static const unsigned maxPrefixLength=9;

// Shared header of all inner nodes
struct Node {
    // length of the compressed path (prefix)
    uint32_t prefixLength;
    // number of non-null children
    uint16_t count;
    // node type
    int8_t type;
    // compressed path (prefix)
    uint8_t prefix[maxPrefixLength];
    // reference counter
    uint32_t rc = 0;

    Node(int8_t type) : prefixLength(0),count(0),type(type) {}
};

// Node with up to 4 children
struct Node4 : Node {
    uint8_t key[4];
    Node* child[4];

    Node4() : Node(NodeType4) {
        memset(key,0,sizeof(key));
        memset(child,0,sizeof(child));
    }
};

// Node with up to 16 children
struct Node16 : Node {
    uint8_t key[16];
    Node* child[16];

    Node16() : Node(NodeType16) {
        memset(key,0,sizeof(key));
        memset(child,0,sizeof(child));
    }
};

static const uint8_t emptyMarker=48;

// Node with up to 48 children
struct Node48 : Node {
    uint8_t childIndex[256];
    Node* child[48];

    Node48() : Node(NodeType48) {
        memset(childIndex,emptyMarker,sizeof(childIndex));
        memset(child,0,sizeof(child));
    }
};

// Node with up to 256 children
struct Node256 : Node {
    Node* child[256];

    Node256() : Node(NodeType256) {
        memset(child,0,sizeof(child));
    }
};