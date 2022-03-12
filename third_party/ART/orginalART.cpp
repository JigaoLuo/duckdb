/*
  Adaptive Radix Tree
  Viktor Leis, 2012
  leis@in.tum.de
 */

#include <cstdlib>    // malloc, free
#include <string.h>    // memset, memcpy
#include <stdint.h>    // integer types
#include <emmintrin.h> // x86 SSE intrinsics
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>  // gettime
#include <algorithm>   // std::random_shuffle
#include <vector>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <cstring>
#include <random>

#include <unistd.h>
#include <stdint.h>

#include "../perfevent/PerfEvent.hpp"
#include "../zipf/zipf_table_distribution.hpp"

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

inline Node* makeLeaf(uintptr_t tid) {
   // Create a pseudo-leaf
   return reinterpret_cast<Node*>((tid<<1)|1);
}

inline uintptr_t getLeafValue(Node* node) {
   // The the value stored in the pseudo-leaf
   return reinterpret_cast<uintptr_t>(node)>>1;
}

inline bool isLeaf(Node* node) {
   // Is the node a leaf?
   return reinterpret_cast<uintptr_t>(node)&1;
}

uint8_t flipSign(uint8_t keyByte) {
   // Flip the sign bit, enables signed SSE comparison of unsigned values, used by Node16
   return keyByte^128;
}

void loadKey(uintptr_t tid,uint8_t key[]) {
   // Store the key of the tuple into the key vector
   // Implementation is database specific
   reinterpret_cast<uint64_t*>(key)[0]=__builtin_bswap64(tid);
}

// This address is used to communicate that search failed
Node* nullNode=NULL;

static inline unsigned ctz(uint16_t x) {
   // Count trailing zeros, only defined for x>0
#ifdef __GNUC__
   return __builtin_ctz(x);
#else
   // Adapted from Hacker's Delight
   unsigned n=1;
   if ((x&0xFF)==0) {n+=8; x=x>>8;}
   if ((x&0x0F)==0) {n+=4; x=x>>4;}
   if ((x&0x03)==0) {n+=2; x=x>>2;}
   return n-(x&1);
#endif
}


void traversal(Node* n, std::vector<Node*>& res) {
    if (n == nullptr) return;
    if (isLeaf(n)) return;
    res.push_back(n);
    switch (n->type) {
        case NodeType4: {
            Node4* node=static_cast<Node4*>(n);
            for (unsigned i=0;i<node->count;i++) {
                traversal(node->child[i], res);
            }
            return;
        }
        case NodeType16: {
            Node16* node=static_cast<Node16*>(n);
            for (unsigned i=0;i<node->count;i++) {
                traversal(node->child[i], res);
            }
            return;
        }
        case NodeType48: {
            Node48* node=static_cast<Node48*>(n);
            for (unsigned i=0;i<48;i++) {
                if (node->childIndex[i]!=emptyMarker) {
                    traversal(node->child[node->childIndex[i]], res);
                }
            }
            return;
        }
        case NodeType256: {
            Node256* node=static_cast<Node256*>(n);
            for (unsigned i=0;i<256;i++) {
                if (node->child[i] != 0) {
                    traversal(node->child[i], res);
                }
            }
            return;
        }
    }
}


Node** findChild(Node* n,uint8_t keyByte) {
   // Find the next child for the keyByte
   switch (n->type) {
      case NodeType4: {
         Node4* node=static_cast<Node4*>(n);
         for (unsigned i=0;i<node->count;i++)
            if (node->key[i]==keyByte)
               return &node->child[i];
         return &nullNode;
      }
      case NodeType16: {
         Node16* node=static_cast<Node16*>(n);
         __m128i cmp=_mm_cmpeq_epi8(_mm_set1_epi8(flipSign(keyByte)),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
         unsigned bitfield=_mm_movemask_epi8(cmp)&((1<<node->count)-1);
         if (bitfield)
            return &node->child[ctz(bitfield)]; else
            return &nullNode;
      }
      case NodeType48: {
         Node48* node=static_cast<Node48*>(n);
         if (node->childIndex[keyByte]!=emptyMarker)
            return &node->child[node->childIndex[keyByte]]; else
            return &nullNode;
      }
      case NodeType256: {
         Node256* node=static_cast<Node256*>(n);
         return &(node->child[keyByte]);
      }
   }
   throw; // Unreachable
}

Node* minimum(Node* node) {
   // Find the leaf with smallest key
   if (!node)
      return NULL;

   if (isLeaf(node))
      return node;

   switch (node->type) {
      case NodeType4: {
         Node4* n=static_cast<Node4*>(node);
         return minimum(n->child[0]);
      }
      case NodeType16: {
         Node16* n=static_cast<Node16*>(node);
         return minimum(n->child[0]);
      }
      case NodeType48: {
         Node48* n=static_cast<Node48*>(node);
         unsigned pos=0;
         while (n->childIndex[pos]==emptyMarker)
            pos++;
         return minimum(n->child[n->childIndex[pos]]);
      }
      case NodeType256: {
         Node256* n=static_cast<Node256*>(node);
         unsigned pos=0;
         while (!n->child[pos])
            pos++;
         return minimum(n->child[pos]);
      }
   }
   throw; // Unreachable
}

Node* maximum(Node* node) {
   // Find the leaf with largest key
   if (!node)
      return NULL;

   if (isLeaf(node))
      return node;

   switch (node->type) {
      case NodeType4: {
         Node4* n=static_cast<Node4*>(node);
         return maximum(n->child[n->count-1]);
      }
      case NodeType16: {
         Node16* n=static_cast<Node16*>(node);
         return maximum(n->child[n->count-1]);
      }
      case NodeType48: {
         Node48* n=static_cast<Node48*>(node);
         unsigned pos=255;
         while (n->childIndex[pos]==emptyMarker)
            pos--;
         return maximum(n->child[n->childIndex[pos]]);
      }
      case NodeType256: {
         Node256* n=static_cast<Node256*>(node);
         unsigned pos=255;
         while (!n->child[pos])
            pos--;
         return maximum(n->child[pos]);
      }
   }
   throw; // Unreachable
}

bool leafMatches(Node* leaf,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
   // Check if the key of the leaf is equal to the searched key
   if (depth!=keyLength) {
      uint8_t leafKey[maxKeyLength];
      loadKey(getLeafValue(leaf),leafKey);
      for (unsigned i=depth;i<keyLength;i++)
         if (leafKey[i]!=key[i])
            return false;
   }
   return true;
}

unsigned prefixMismatch(Node* node,uint8_t key[],unsigned depth,unsigned maxKeyLength) {
   // Compare the key with the prefix of the node, return the number matching bytes
   unsigned pos;
   if (node->prefixLength>maxPrefixLength) {
      for (pos=0;pos<maxPrefixLength;pos++)
         if (key[depth+pos]!=node->prefix[pos])
            return pos;
      uint8_t minKey[maxKeyLength];
      loadKey(getLeafValue(minimum(node)),minKey);
      for (;pos<node->prefixLength;pos++)
         if (key[depth+pos]!=minKey[depth+pos])
            return pos;
   } else {
      for (pos=0;pos<node->prefixLength;pos++)
         if (key[depth+pos]!=node->prefix[pos])
            return pos;
   }
   return pos;
}

Node* lookup(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
   // Find the node with a matching key, optimistic version

   bool skippedPrefix=false; // Did we optimistically skip some prefix without checking it?

   while (node!=NULL) {
      if (isLeaf(node)) {
         if (!skippedPrefix&&depth==keyLength) // No check required
            return node;

         if (depth!=keyLength) {
            // Check leaf
            uint8_t leafKey[maxKeyLength];
            loadKey(getLeafValue(node),leafKey);
            for (unsigned i=(skippedPrefix?0:depth);i<keyLength;i++)
               if (leafKey[i]!=key[i])
                  return NULL;
         }
         return node;
      }

      if (node->prefixLength) {
         if (node->prefixLength<maxPrefixLength) {
            for (unsigned pos=0;pos<node->prefixLength;pos++)
               if (key[depth+pos]!=node->prefix[pos])
                  return NULL;
         } else
            skippedPrefix=true;
         depth+=node->prefixLength;
      }

      node=*findChild(node,key[depth]);
      depth++;
   }

   return NULL;
}

////Node* lookupPessimistic(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {  //TODO(jigao): try this!!!
//Node* lookupPessimistic(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength, std::unordered_set<uintptr_t>& add, std::vector<int>& tree_depth, std::unordered_map<Node*, int>& nodes) {  //TODO(jigao): try this!!! //TODO(jigao): delete this, if not counting # pointers or pages
//   // Find the node with a matching key, alternative pessimistic version
//
//int tree_depth_ = 0;
//   while (node!=NULL) {
//      if (isLeaf(node)) {
//         if (leafMatches(node,key,keyLength,depth,maxKeyLength)) {
// tree_depth.push_back(tree_depth_);
//             return node;
//         }
//         return NULL;
//      }
//
//tree_depth_++;
//nodes[node] = tree_depth_;
//if (!isLeaf(node) && node != NULL) {
//   add.emplace( ( reinterpret_cast<uintptr_t>( node ) / getpagesize() ) );
//}
//      if (prefixMismatch(node,key,depth,maxKeyLength)!=node->prefixLength)
//         return NULL; else
//         depth+=node->prefixLength;
//
//      node=*findChild(node,key[depth]);
//      depth++;
//   }
//
//   return NULL;
//}

// Forward references
void insertNode4(Node4* node,Node** nodeRef,uint8_t keyByte,Node* child);
void insertNode16(Node16* node,Node** nodeRef,uint8_t keyByte,Node* child);
void insertNode48(Node48* node,Node** nodeRef,uint8_t keyByte,Node* child);
void insertNode256(Node256* node,Node** nodeRef,uint8_t keyByte,Node* child);

unsigned min(unsigned a,unsigned b) {
   // Helper function
   return (a<b)?a:b;
}

void copyPrefix(Node* src,Node* dst) {
   // Helper function that copies the prefix from the source to the destination node
   dst->prefixLength=src->prefixLength;
   memcpy(dst->prefix,src->prefix,min(src->prefixLength,maxPrefixLength));
}

void insert(Node* node,Node** nodeRef,uint8_t key[],unsigned depth,uintptr_t value,unsigned maxKeyLength) {
   // Insert the leaf value into the tree

   if (node==NULL) {
      *nodeRef=makeLeaf(value);
      return;
   }

   if (isLeaf(node)) {
      // Replace leaf with Node4 and store both leaves in it
      uint8_t existingKey[maxKeyLength];
      loadKey(getLeafValue(node),existingKey);
      unsigned newPrefixLength=0;
      while (existingKey[depth+newPrefixLength]==key[depth+newPrefixLength])
         newPrefixLength++;

      Node4* newNode=new Node4();
      newNode->prefixLength=newPrefixLength;
      memcpy(newNode->prefix,key+depth,min(newPrefixLength,maxPrefixLength));
      *nodeRef=newNode;

      insertNode4(newNode,nodeRef,existingKey[depth+newPrefixLength],node);
      insertNode4(newNode,nodeRef,key[depth+newPrefixLength],makeLeaf(value));
      return;
   }

   // Handle prefix of inner node
   if (node->prefixLength) {
      unsigned mismatchPos=prefixMismatch(node,key,depth,maxKeyLength);
      if (mismatchPos!=node->prefixLength) {
         // Prefix differs, create new node
         Node4* newNode=new Node4();
         *nodeRef=newNode;
         newNode->prefixLength=mismatchPos;
         memcpy(newNode->prefix,node->prefix,min(mismatchPos,maxPrefixLength));
         // Break up prefix
         if (node->prefixLength<maxPrefixLength) {
            insertNode4(newNode,nodeRef,node->prefix[mismatchPos],node);
            node->prefixLength-=(mismatchPos+1);
            memmove(node->prefix,node->prefix+mismatchPos+1,min(node->prefixLength,maxPrefixLength));
         } else {
            node->prefixLength-=(mismatchPos+1);
            uint8_t minKey[maxKeyLength];
            loadKey(getLeafValue(minimum(node)),minKey);
            insertNode4(newNode,nodeRef,minKey[depth+mismatchPos],node);
            memmove(node->prefix,minKey+depth+mismatchPos+1,min(node->prefixLength,maxPrefixLength));
         }
         insertNode4(newNode,nodeRef,key[depth+mismatchPos],makeLeaf(value));
         return;
      }
      depth+=node->prefixLength;
   }

   // Recurse
   Node** child=findChild(node,key[depth]);
   if (*child) {
      insert(*child,child,key,depth+1,value,maxKeyLength);
      return;
   }

   // Insert leaf into inner node
   Node* newNode=makeLeaf(value);
   switch (node->type) {
      case NodeType4: insertNode4(static_cast<Node4*>(node),nodeRef,key[depth],newNode); break;
      case NodeType16: insertNode16(static_cast<Node16*>(node),nodeRef,key[depth],newNode); break;
      case NodeType48: insertNode48(static_cast<Node48*>(node),nodeRef,key[depth],newNode); break;
      case NodeType256: insertNode256(static_cast<Node256*>(node),nodeRef,key[depth],newNode); break;
   }
}

void insertNode4(Node4* node,Node** nodeRef,uint8_t keyByte,Node* child) {
   // Insert leaf into inner node
   if (node->count<4) {
      // Insert element
      unsigned pos;
      for (pos=0;(pos<node->count)&&(node->key[pos]<keyByte);pos++);
      memmove(node->key+pos+1,node->key+pos,node->count-pos);
      memmove(node->child+pos+1,node->child+pos,(node->count-pos)*sizeof(uintptr_t));
      node->key[pos]=keyByte;
      node->child[pos]=child;
      node->count++;
   } else {
      // Grow to Node16
      Node16* newNode=new Node16();
      *nodeRef=newNode;
      newNode->count=4;
      copyPrefix(node,newNode);
      for (unsigned i=0;i<4;i++)
         newNode->key[i]=flipSign(node->key[i]);
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
      delete node;
      return insertNode16(newNode,nodeRef,keyByte,child);
   }
}

void insertNode16(Node16* node,Node** nodeRef,uint8_t keyByte,Node* child) {
   // Insert leaf into inner node
   if (node->count<16) {
      // Insert element
      uint8_t keyByteFlipped=flipSign(keyByte);
      __m128i cmp=_mm_cmplt_epi8(_mm_set1_epi8(keyByteFlipped),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
      uint16_t bitfield=_mm_movemask_epi8(cmp)&(0xFFFF>>(16-node->count));
      unsigned pos=bitfield?ctz(bitfield):node->count;
      memmove(node->key+pos+1,node->key+pos,node->count-pos);
      memmove(node->child+pos+1,node->child+pos,(node->count-pos)*sizeof(uintptr_t));
      node->key[pos]=keyByteFlipped;
      node->child[pos]=child;
      node->count++;
   } else {
      // Grow to Node48
      Node48* newNode=new Node48();
      *nodeRef=newNode;
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
      for (unsigned i=0;i<node->count;i++)
         newNode->childIndex[flipSign(node->key[i])]=i;
      copyPrefix(node,newNode);
      newNode->count=node->count;
      delete node;
      return insertNode48(newNode,nodeRef,keyByte,child);
   }
}

void insertNode48(Node48* node,Node** nodeRef,uint8_t keyByte,Node* child) {
   // Insert leaf into inner node
   if (node->count<48) {
      // Insert element
      unsigned pos=node->count;
      if (node->child[pos])
         for (pos=0;node->child[pos]!=NULL;pos++);
      node->child[pos]=child;
      node->childIndex[keyByte]=pos;
      node->count++;
   } else {
      // Grow to Node256
      Node256* newNode=new Node256();
      for (unsigned i=0;i<256;i++)
         if (node->childIndex[i]!=48)
            newNode->child[i]=node->child[node->childIndex[i]];
      newNode->count=node->count;
      copyPrefix(node,newNode);
      *nodeRef=newNode;
      delete node;
      return insertNode256(newNode,nodeRef,keyByte,child);
   }
}

void insertNode256(Node256* node,Node** nodeRef,uint8_t keyByte,Node* child) {
   // Insert leaf into inner node
   node->count++;
   node->child[keyByte]=child;
}

static double gettime(void) {
  struct timeval now_tv;
  gettimeofday (&now_tv,NULL);
  return ((double)now_tv.tv_sec) + ((double)now_tv.tv_usec)/1000000.0;
}

int main(int argc,char** argv) {
   if (argc!=5) {
      printf("usage: %s n 0|1|2 u|z alpha\nn: number of keys\n0: sorted keys\n1: dense keys\n2: sparse keys\n"
                                           "u: uniform distributed lookup\nz: zipfian distributed lookup\n"
                                           "alpha: the factor of the zipfian distribution", argv[0]);
      return 1;
   }

   const uint64_t n=atoi(argv[1]);
   uint64_t* keys=new uint64_t[n];

   // Generate keys
   for (uint64_t i=0;i<n;i++)
      // dense, sorted
      keys[i]=i+1;
   if (atoi(argv[2])==1)
      // dense, random
      std::random_shuffle(keys,keys+n);
   if (atoi(argv[2])==2)
      // "pseudo-sparse" (the most-significant leaf bit gets lost)
      for (uint64_t i=0;i<n;i++)
         keys[i]=(static_cast<uint64_t>(rand())<<32) | static_cast<uint64_t>(rand());

   const double alpha = atof(argv[4]);

   // Build tree
   double start = gettime();
   Node* tree=NULL;
   for (uint64_t i=0;i<n;i++) {
      uint8_t key[8];loadKey(keys[i],key);
      insert(tree,&tree,key,0,keys[i],8);
   }
   printf("insert,%ld,%f\n",n,(n/1000000.0)/(gettime()-start));

   /// Prepare to-be-looked-up keys w.r.t. the distribution program argument
    uint64_t* lookup_keys=new uint64_t[n];
    if (argv[3][0]=='u') {
        /// uniform distributed lookup == the original ART lookup procedure
        /// just copy the key array :D
        std::memcpy(lookup_keys, keys, n * sizeof(keys));
    } else if (argv[3][0]=='z') {
        /// zipfian distributed lookup
        std::random_device rd;
        std::mt19937 gen(rd());
        zipf_table_distribution<> zipf(n, alpha);  /// zipf distribution \in [1, n]
        std::vector<unsigned long> vec;
        std::set<unsigned long> set;
        for (int i = 0; i < n; ++i) {
            const unsigned long index = zipf(gen) - 1;
            vec.emplace_back(index);
            set.emplace(index);
            lookup_keys[i] = keys[index]; /// Fix zipfian distribution's value range to [0, n)
        }
         std::cout << "lookup indexes as set: #=" << set.size() << std::endl;
    }
    /// Before shuffle
//    for (uint64_t i=0;i<n;i++) {
//        std::cout << (keys[i]) << " | " << lookup_keys[i] << std::endl;
//    }
    std::sort(lookup_keys, lookup_keys + n); ///
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> uni_distrib(1, n);
    for (uint64_t i=0;i<n;) {
        const uint64_t before = lookup_keys[i];
        const uint64_t after = static_cast<uint64_t>(uni_distrib(gen));
        while (lookup_keys[i] == before) {
            lookup_keys[i] = after;
            ++i;
        }
    }
    std::random_shuffle(lookup_keys, lookup_keys + n);
//    std::vector<uint8_t*> real_lookup_keys;
    for (uint64_t i=0;i<n;i++) {
        uint8_t* key=new uint8_t[8];
        loadKey(lookup_keys[i],key);
//        real_lookup_keys.push_back(key);  /// Not used.
    }
    /// After shuffle
//    for (uint64_t i=0;i<n;i++) {
//        std::cout << (keys[i]) << " | " << lookup_keys[i] << std::endl;  // std::cout << (keys[i]) << " | " << lookup_keys[i] << " | " << __builtin_bswap64(*(reinterpret_cast<uint64_t*>(real_lookup_keys[i]))) << std::endl;
//    }


    int iteration = 3;
    for (int i = 0; i < iteration; ++i) {
//   uint64_t repeat=10000000/n;
        uint64_t repeat = 10;
        if (repeat < 1) repeat = 1;
        uint64_t leafoutput = 0;
        PerfEvent e_lookup;
        start = gettime();
        e_lookup.startCounters();

        for (uint64_t r = 0; r < repeat; r++) {
            for (uint64_t i = 0; i < n; i++) {
                uint8_t key[8];
                loadKey(lookup_keys[i],key);
               Node *leaf = lookup(tree, key, 8, 0, 8); /// leaf is just a madeup pointer
                leafoutput += (getLeafValue(leaf)==lookup_keys[i]);
//                assert(isLeaf(leaf) && getLeafValue(leaf)==lookup_keys[i]);
            }
        }
        double end = gettime();
        printf("lookup,%ld,%f\n", n, (n * repeat / 1000000.0) / (end - start));
        e_lookup.stopCounters();
        e_lookup.printReport(std::cout, n * repeat); // use n as scale factor
        std::cout << std::endl;
        std::cout << "leafoutput " << leafoutput << std::endl;

        std::string output = "|";
        output += std::to_string(alpha) + ",";
        const double throughput = (n * repeat / 1000000.0) / (end - start);
        output += std::to_string(throughput) + ",";
        double tlb_miss = 0;
        for (unsigned i = 0; i < e_lookup.events.size(); i++) {
            if (e_lookup.names[i] == "cycles" || e_lookup.names[i] == "L1-misses" ||
                e_lookup.names[i] == "LLC-misses" || e_lookup.names[i] == "dTLB-load-misses") {
                output += std::to_string(e_lookup.events[i].readCounter() / (n * repeat)) + ",";
            }
            if (e_lookup.names[i] == "dTLB-load-misses") {
                tlb_miss = e_lookup.events[i].readCounter();
            }
        }
        output += std::to_string(100.0 * tlb_miss / ((end - start) * 1000000000.0)) + ",";
        output.pop_back();
        std::cout << output << std::endl;
    }


    std::vector<Node*> res;
    traversal(tree, res);
    std::cout << "size: " << res.size() << std::endl;

    size_t node4_num = 0;
    size_t node16_num = 0;
    size_t node48_num = 0;
    size_t node256_num = 0;
    for (const auto& n : res) {
        switch (n->type) {
            case NodeType4: {
                ++node4_num;
                break;
            }
            case NodeType16: {
                ++node16_num;
                break;
            }
            case NodeType48: {
                ++node48_num;
                break;
            }
            case NodeType256: {
                ++node256_num;
                break;
            }
        }
    }
    std::cout << "node4_num:" << node4_num << std::endl;
    std::cout << "node16_num:" << node16_num << std::endl;
    std::cout << "node48_num:" << node48_num << std::endl;
    std::cout << "node256_num:" << node256_num << std::endl;

//    for (uint64_t i=0;i<n;i++) delete(real_lookup_keys[i]);

   return 0;
}
