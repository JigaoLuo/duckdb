/*
  Adaptive Radix Tree
  Viktor Leis, 2012
  leis@in.tum.de
 */

#include <stdlib.h>    // malloc, free
#include <string.h>    // memset, memcpy
#include <stdint.h>    // integer types
#include <emmintrin.h> // x86 SSE intrinsics
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>  // gettime
#include <algorithm>   // std::random_shuffle
#include <iostream>
#include <vector>
#include <set>
#include <unordered_map>

#include "ART_nodes.hpp"
#include "../allocator/art_mmap_allocator.hpp"
#include "../allocator/MallocAllocator.hpp"
#include "../allocator/PooledAllocator.hpp"
#include "../allocator/MemoryPool/C-11/MemoryPool.h"
#include "../perfevent/PerfEvent.hpp"
#include "../zipf/zipf_table_distribution.hpp"

/// Allocator
art_mmap_allocator<page_type::huge_2mb, 0> art_allocator;
art_mmap_allocator<page_type::huge_2mb, 0> art_allocator_reorginize;

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
    /// Preorder
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
            for (unsigned i=0;i<256;i++) {
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

void traversal(Node* n, std::vector<std::pair<Node*, uint8_t>>& res, uint8_t depth) {
    if (n == nullptr) return;
    if (isLeaf(n)) return;
    /// Preorder
    res.emplace_back(std::make_pair(n, depth));
    switch (n->type) {
        case NodeType4: {
            Node4* node=static_cast<Node4*>(n);
            for (unsigned i=0;i<node->count;i++) {
                traversal(node->child[i], res, depth + 1);
            }
            return;
        }
        case NodeType16: {
            Node16* node=static_cast<Node16*>(n);
            for (unsigned i=0;i<node->count;i++) {
                traversal(node->child[i], res, depth + 1);
            }
            return;
        }
        case NodeType48: {
            Node48* node=static_cast<Node48*>(n);
            for (unsigned i=0;i<256;i++) {
                if (node->childIndex[i]!=emptyMarker) {
                    traversal(node->child[node->childIndex[i]], res, depth + 1);
                }
            }
            return;
        }
        case NodeType256: {
            Node256* node=static_cast<Node256*>(n);
            for (unsigned i=0;i<256;i++) {
                if (node->child[i] != 0) {
                    traversal(node->child[i], res, depth + 1);
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

Node* lookupPessimistic(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {  //TODO(jigao): try this!!!
   // Find the node with a matching key, alternative pessimistic version

   while (node!=NULL) {
      if (isLeaf(node)) {
         if (leafMatches(node,key,keyLength,depth,maxKeyLength))
            return node;
         return NULL;
      }

      if (prefixMismatch(node,key,depth,maxKeyLength)!=node->prefixLength)
         return NULL; else
         depth+=node->prefixLength;

      node=*findChild(node,key[depth]);
      depth++;
   }

   return NULL;
}

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

      auto memory = art_allocator.allocate_node4();  ///
      /// auto memory = allocator.allocate(sizeof(Node4));  ///
      /// auto memory = allocator4.allocate();  ///
      Node4* newNode=new (memory) Node4();  /// Node4* newNode=new Node4();
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
		 auto memory = art_allocator.allocate_node4();  ///
         /// auto memory = allocator.allocate(sizeof(Node4));  ///
         /// auto memory = allocator4.allocate();  ///
		 Node4* newNode=new (memory) Node4();  /// Node4* newNode=new Node4();
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
      auto memory = art_allocator.allocate_node16();  ///
      /// auto memory = allocator.allocate(sizeof(Node16)); ///
      /// auto memory = allocator16.allocate();  ///
      Node16* newNode=new (memory) Node16();  /// Node16* newNode=new Node16();
      *nodeRef=newNode;
      newNode->count=4;
      copyPrefix(node,newNode);
      for (unsigned i=0;i<4;i++)
         newNode->key[i]=flipSign(node->key[i]);
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
	  /// allocator.deallocate(reinterpret_cast<unsigned char*>(node), sizeof(Node4));  /// delete node;
      /// allocator4.deallocate(node);  ///
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
	  auto memory = art_allocator.allocate_node48();  ///
      /// auto memory = allocator.allocate(sizeof(Node48));  ///
	  /// auto memory = allocator48.allocate();  ///
	  Node48* newNode=new (memory) Node48();  /// Node48* newNode=new Node48();
      *nodeRef=newNode;
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
      for (unsigned i=0;i<node->count;i++)
         newNode->childIndex[flipSign(node->key[i])]=i;
      copyPrefix(node,newNode);
      newNode->count=node->count;
      /// allocator.deallocate(reinterpret_cast<unsigned char*>(node), sizeof(Node16));  /// delete node;
      /// allocator16.deallocate(node);  ///
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
	  auto memory = art_allocator.allocate_node256();  ///
      /// auto memory = allocator.allocate(sizeof(Node256));  ///
	  /// auto memory = allocator256.allocate();  ///
      Node256* newNode=new (memory) Node256();  /// Node256* newNode=new Node256();
      for (unsigned i=0;i<256;i++)
         if (node->childIndex[i]!=48)
            newNode->child[i]=node->child[node->childIndex[i]];
      newNode->count=node->count;
      copyPrefix(node,newNode);
      *nodeRef=newNode;
      /// allocator.deallocate(reinterpret_cast<unsigned char*>(node), sizeof(Node48));  /// delete node;
      /// allocator48.deallocate(node);  ///
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

   uint64_t n=atoi(argv[1]);
   uint64_t* keys=new uint64_t[n];

   std::cout << "Node4 Size: " << sizeof(Node4) << std::endl;
   std::cout << "Node16 Size: " << sizeof(Node16) << std::endl;
   std::cout << "Node48 Size: " << sizeof(Node48) << std::endl;
   std::cout << "Node256 Size: " << sizeof(Node256) << std::endl;

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
        std::random_shuffle(keys, keys + n);
        std::random_device rd;
        std::mt19937 gen(rd());
        zipf_table_distribution<> zipf(n, alpha);
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
//    std::sort(lookup_keys, lookup_keys + n); ///
//    std::set<uint64_t> key_set(lookup_keys, lookup_keys + n);
//    std::random_device rd;
//    std::mt19937 gen(rd());
//    std::uniform_int_distribution<> uni_distrib(1, n);
//    for (uint64_t i=0;i<n;) {
//        const uint64_t before = lookup_keys[i];
//        uint64_t pick_element = static_cast<uint64_t>(uni_distrib(gen));
//        while (key_set.find(pick_element) == key_set.end()) pick_element = static_cast<uint64_t>(uni_distrib(gen));
//        const uint64_t after = pick_element;  /// Ensure after in the key set and index
//        while (lookup_keys[i] == before) {
//            lookup_keys[i] = after;
//            ++i;
//        }
//    }
//    std::random_shuffle(lookup_keys, lookup_keys + n);
//    std::vector<uint8_t*> real_lookup_keys;
//    for (uint64_t i=0;i<n;i++) {
//        uint8_t* key=new uint8_t[8];
//        loadKey(lookup_keys[i],key);
//        real_lookup_keys.push_back(key);  /// Not used.
//    }
    /// After shuffle
//    for (uint64_t i=0;i<n;i++) {
//        std::cout << (keys[i]) << " | " << lookup_keys[i] << std::endl;  // std::cout << (keys[i]) << " | " << lookup_keys[i] << " | " << __builtin_bswap64(*(reinterpret_cast<uint64_t*>(real_lookup_keys[i]))) << std::endl;
//    }


    int iteration = 1;
    for (int i = 0; i < iteration; ++i) {
        // Repeat lookup for small trees to get reproducable results
        uint64_t repeat = 10000000 / n;
        if (repeat < 1)
            repeat = 1;
        start = gettime();
        PerfEvent e_lookup;
        e_lookup.startCounters();
        for (uint64_t r = 0; r < repeat; r++) {
            for (uint64_t i = 0; i < n; i++) {
                uint8_t key[8];
                loadKey(lookup_keys[i], key);
                Node *leaf = lookup(tree, key, 8, 0, 8);
                assert(isLeaf(leaf) && getLeafValue(leaf) == lookup_keys[i]);
            }
        }
        double end = gettime();
        printf("lookup,%ld,%f\n", n, (n * repeat / 1000000.0) / (end - start));
        e_lookup.stopCounters();
        e_lookup.printReport(std::cout, n*repeat); // use n as scale factor
        std::cout << std::endl;

        std::string output = "|";
        output += std::to_string(alpha) + ",";
        const double throughput = (n * repeat / 1000000.0) / (end - start);
        output += std::to_string(throughput) + ",";
        double tlb_miss = 0;
        for (unsigned i = 0; i < e_lookup.events.size(); i++) {
            if (e_lookup.names[i] == "cycles" || e_lookup.names[i] == "L1-misses" ||
                e_lookup.names[i] == "LLC-misses" || e_lookup.names[i] == "dTLB-load-misses") {
                output += std::to_string(e_lookup.events[i].readCounter() / n*repeat) + ",";
            }
            if (e_lookup.names[i] == "dTLB-load-misses") {
                tlb_miss = e_lookup.events[i].readCounter();
            }
        }
        output += std::to_string(100.0 * tlb_miss / ((end - start) * 1000000000.0)) + ",";
        output.pop_back();
        std::cout << output << std::endl;
    }


    /// Collect all nodes: Preorder Traversal
    std::vector<Node*> res;
    traversal(tree, res);
    std::cout << "size: " << res.size() << std::endl;

    /// Statistics of nodes
    std::cout << "Number of huge pages: " << art_allocator.num_pages() << std::endl;

    {
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
    }

    /// Collect all nodes: Preorder Traversal
    std::vector<std::pair<Node*, uint8_t>> ht;
    {
        traversal(tree, ht, 0);
        std::cout << "size: " << ht.size() << std::endl;

        size_t node4_num = 0; std::map<uint8_t, int64_t> node4_levels;
        size_t node16_num = 0; std::map<uint8_t, int64_t> node16_levels;
        size_t node48_num = 0; std::map<uint8_t, int64_t> node48_levels;
        size_t node256_num = 0; std::map<uint8_t, int64_t> node256_levels;
        for (const auto& n : ht) {
            switch (n.first->type) {
                case NodeType4: {
                    ++node4_num;
                    ++node4_levels[n.second];
                    break;
                }
                case NodeType16: {
                    ++node16_num;
                    ++node16_levels[n.second];
                    break;
                }
                case NodeType48: {
                    ++node48_num;
                    ++node48_levels[n.second];
                    break;
                }
                case NodeType256: {
                    ++node256_num;
                    ++node256_levels[n.second];
                    break;
                }
            }
        }
        std::cout << "node4_num:" << node4_num;
        for (const auto& l : node4_levels) std::cout << " | [Level " << int(l.first) << "]: " << l.second << " | "; std::cout << std::endl;
        std::cout << "node16_num:" << node16_num;
        for (const auto& l : node16_levels) std::cout << "| [Level " << int(l.first) << "]: " << l.second << " | "; std::cout << std::endl;
        std::cout << "node48_num:" << node48_num;
        for (const auto& l : node48_levels) std::cout << "| [Level " << int(l.first) << "]: " << l.second << " | "; std::cout << std::endl;
        std::cout << "node256_num:" << node256_num;
        for (const auto& l : node256_levels) std::cout << " | [Level " << int(l.first) << "]: " << l.second << " | "; std::cout << std::endl;
    }


    /// Sort nodes with SOMTHING
    /// TODO:

//    for (const auto& n : res) {
//        std::cout << int(n->type) <<  std::endl;
//    }


    /// Mark old & new nodes
    std::unordered_map<Node*, Node*> old_to_new;
    std::vector<Node*> new_nodes;
    for (const auto& n : res) {
        switch (n->type) {
            case NodeType4: {
                Node4* node=static_cast<Node4*>(n);
                auto memory = art_allocator_reorginize.allocate_node4();  ///
                Node4* newNode=new (memory) Node4(*node);  /// Node4* newNode=new Node4();
                old_to_new[n] = static_cast<Node*>(newNode);
                new_nodes.push_back(static_cast<Node*>(newNode));
                break;
            }
            case NodeType16: {
                Node16* node=static_cast<Node16*>(n);
                auto memory = art_allocator_reorginize.allocate_node16();  ///
                Node16* newNode=new (memory) Node16(*node);  /// Node16* newNode=new Node16();
                old_to_new[n] = static_cast<Node*>(newNode);
                new_nodes.push_back(static_cast<Node*>(newNode));
                break;
            }
            case NodeType48: {
                Node48* node=static_cast<Node48*>(n);
                auto memory = art_allocator_reorginize.allocate_node48();  ///
                Node48* newNode=new (memory) Node48(*node);  /// Node48* newNode=new Node48();
                old_to_new[n] = static_cast<Node*>(newNode);
                new_nodes.push_back(static_cast<Node*>(newNode));
                break;
            }
            case NodeType256: {
                Node256* node=static_cast<Node256*>(n);
                auto memory = art_allocator_reorginize.allocate_node256();  ///
                Node256* newNode=new (memory) Node256(*node);  /// Node256* newNode=new Node256();
                old_to_new[n] = static_cast<Node*>(newNode);
                new_nodes.push_back(static_cast<Node*>(newNode));
                break;
            }
        }
    }

    assert(res.size() == new_nodes.size());
    assert(res.size() == old_to_new.size());

    /// Replace all old pointers
    for (const auto& n : new_nodes) {
        switch (n->type) {
            case NodeType4: {
                Node4* node=static_cast<Node4*>(n);
                for (unsigned i=0;i<node->count;i++) {
                    if (node->child[i] != nullptr && !isLeaf(node->child[i])) {
                        auto found = old_to_new.find(node->child[i]);
                        assert(found != old_to_new.end());
                        node->child[i] = found->second;
                    }
                }
                break;
            }
            case NodeType16: {
                Node16* node=static_cast<Node16*>(n);
                for (unsigned i=0;i<node->count;i++) {
                    if (node->child[i] != nullptr && !isLeaf(node->child[i])) {
                        auto found = old_to_new.find(node->child[i]);
                        assert(found != old_to_new.end());
                        node->child[i] = found->second;
                    }
                }
                break;
            }
            case NodeType48: {
                Node48* node=static_cast<Node48*>(n);
                for (unsigned i=0;i<256;i++) {
                    if (node->childIndex[i]!=emptyMarker && !isLeaf(node->child[node->childIndex[i]])) {
                        auto found = old_to_new.find(node->child[node->childIndex[i]]);
                        assert(found != old_to_new.end());
                        node->child[node->childIndex[i]] = found->second;
                    }
                }
                break;
            }
            case NodeType256: {
                Node256* node=static_cast<Node256*>(n);
                for (unsigned i=0;i<256;i++) {
                    if (node->child[i] != 0 && node->child[i] != nullptr && !isLeaf(node->child[i])) {
                        auto found = old_to_new.find(node->child[i]);
                        assert(found != old_to_new.end());
                        node->child[i] = found->second;
                    }
                }
                break;
            }
        }
    }

    /// Now lookup
    Node* new_root = old_to_new[tree];
    {
        int iteration = 5;
        for (int i = 0; i < iteration; ++i) {
            // Repeat lookup for small trees to get reproducable results
            uint64_t repeat = 10000000 / n;
            if (repeat < 1)
                repeat = 1;
            start = gettime();
            PerfEvent e_lookup;
            e_lookup.startCounters();
            for (uint64_t r = 0; r < repeat; r++) {
                for (uint64_t i = 0; i < n; i++) {
                    uint8_t key[8];
                    loadKey(lookup_keys[i], key);
                    Node *leaf = lookup(new_root, key, 8, 0, 8);
//                    assert(isLeaf(leaf) && getLeafValue(leaf) == lookup_keys[i]);
                }
            }
            double end = gettime();
            printf("lookup,%ld,%f\n", n, (n * repeat / 1000000.0) / (end - start));
            e_lookup.stopCounters();
            e_lookup.printReport(std::cout, n*repeat); // use n as scale factor
            std::cout << std::endl;

            std::string output = "|";
            output += std::to_string(alpha) + ",";
            const double throughput = (n * repeat / 1000000.0) / (end - start);
            output += std::to_string(throughput) + ",";
            double tlb_miss = 0;
            for (unsigned i = 0; i < e_lookup.events.size(); i++) {
                if (e_lookup.names[i] == "cycles" || e_lookup.names[i] == "L1-misses" ||
                    e_lookup.names[i] == "LLC-misses" || e_lookup.names[i] == "dTLB-load-misses") {
                    output += std::to_string(e_lookup.events[i].readCounter() / n*repeat) + ",";
                }
                if (e_lookup.names[i] == "dTLB-load-misses") {
                    tlb_miss = e_lookup.events[i].readCounter();
                }
            }
            output += std::to_string(100.0 * tlb_miss / ((end - start) * 1000000000.0)) + ",";
            output.pop_back();
            std::cout << output << std::endl;
        }
    }

    std::cout << "Number of huge pages for RE_Orgnize: " << art_allocator_reorginize.num_pages() << std::endl;

    delete [] keys;
   return 0;
}
