#ifndef LINGODB_RUNTIME_PERFECTHASHTABLE_H
#define LINGODB_RUNTIME_PERFECTHASHTABLE_H
#include "lingodb/runtime/Buffer.h"
#include "lingodb/runtime/helpers.h"
#include "lingodb/runtime/StringRuntime.h"
#include <cstring>
#include <random>

namespace lingodb::runtime {
class GrowingBuffer;
class RandomNumberGenerator {
public:
   RandomNumberGenerator(uint32_t min, uint32_t max) : dist_(min, max) {
      std::random_device rd;
      engine_.seed(rd());
   }

   uint32_t generate() {
      return dist_(engine_);
   }

private:
   std::mt19937 engine_;
   std::uniform_int_distribution<uint32_t> dist_;
};

class PerfectHashView {
   struct Bucket {
      uint32_t hashA = 0;
      uint32_t hashB = 0;
      uint32_t m = 0;
      uint32_t offset = 0;
      std::vector<std::string> keys;
   };

   struct Entry {
      uint64_t hash;
      uint64_t secondaryHash;
      VarLen32 key;
   };

public:
   Bucket* bucketsData;
   Entry* tableData;
   uint32_t universalHashA = 0;
   uint32_t universalHashB = 0;
   uint32_t tableSize = 0;
   uint32_t prime = 0;
   uint32_t bucketSize = 0;
   std::vector<Bucket> buckets;
   std::vector<Entry> table;

   RandomNumberGenerator rndG = RandomNumberGenerator(1, 0x7FFFFFFE);

   uint32_t random(uint32_t max) {
      return rndG.generate() % max;
   }

   size_t universalHash(const std::string& key, size_t a, size_t b) const;

   // TODO DELETE USE AS LAMBDA
   bool hasCollision(const std::vector<std::string>& keys, size_t a, size_t b, size_t m) const;

   // TODO DELETE USE AS LAMBDA
   void findHashParams(Bucket& bucket, size_t& m);

   // Calculate next prime
   size_t nextPrime(size_t n) const;

   void constructTable();

   static lingodb::runtime::PerfectHashView* build(FlexibleBuffer* keyValues, VarLen32 paramValues);
   static lingodb::runtime::PerfectHashView* construct(const std::vector<std::string>& keys);
   lingodb::runtime::PerfectHashView* constructUp(const std::vector<std::string>& keys);

   // IR LOGIC
   // keyHash1 = universalHash(key, a, b)
   // bukcetPos = keyHash1 % buckets.size()
   // bucket = buckets[bukcetPos]
   // if bucket.m == 1:
   //   if bucket has key and table[0].hash1 == keyHash1:
   //    if table[0].key == key:
   //      materialize true
   // else:
   //   keyHash2 = universalHash(key, bucket.a, bucket.b)
   //   tablePos = keyHash2 % bucket.m + bucket.offset
   //   slot = table[tablePos]
   //   if slot.hash1 == keyHash1 && slot.hash2 == keyHash2
   //    if slot.key == key
   //      materialize true
   
   
   size_t computeHash(uint8_t* keyPtr) {
      lingodb::runtime::VarLen32 key;
      std::memcpy(&key, keyPtr, sizeof(key));
      // lingodb::runtime::VarLen32& key = *(reinterpret_cast<lingodb::runtime::VarLen32*>(keyPtr));
      return universalHash(key, universalHashA, universalHashB);
   }

   void* computeBucket(size_t hash) {
      size_t bucket_idx = hash % buckets.size();
      auto& bucket = buckets[bucket_idx];
      return &bucket;
   }

   void* computeEntry(uint8_t* keyPtr, void* bucketPtr) {
      lingodb::runtime::VarLen32 key;
      std::memcpy(&key, keyPtr, sizeof(key));

      Bucket* bucket = (Bucket*) bucketPtr;
      size_t secondaryHash = universalHash(key, bucket->hashA, bucket->hashB);
      // printf("!! secondaryHash %lu %u %u\n", secondaryHash, bucket->m, bucket->offset);

      size_t pos = (secondaryHash % bucket->m) + bucket->offset;
      auto& entry = table[pos];
      return &entry;
   }

   // size_t computeSecondaryHash(uint8_t* keyPtr, Bucket& bucket) {
   //    lingodb::runtime::VarLen32 key;
   //    std::memcpy(&key, keyPtr, sizeof(key));
   //    return universalHash(key, bucket.hashA, bucket.hashB);
   // }


   // // TODO
   // void* containHash(size_t hash, size_t secondaryHash) {
   //    size_t bucket_idx = hash % table.size();
   //    const auto& bucket = buckets[bucket_idx];
      
   //    size_t pos = (secondaryHash % bucket.m) + bucket.offset;
   //    auto& entry = table[pos];
   //    return &entry;
   // }
   // void* containHash(size_t hash) {
   //    size_t bucket_idx = hash % table.size();
   //    const auto& bucket = buckets[bucket_idx];
   //    auto& entry = table[bucket.offset];
   //    return &entry;
   // }

   // // 查找键
   // bool contains(const std::string& key) const {
   //    if (table.empty()) return false;

   //    // 第一级哈希确定桶
   //    size_t bucket_idx = universalHash(key, universalHashA, universalHashB) % buckets.size();
   //    const auto& bucket = buckets[bucket_idx];

   //    if (bucket.keys.empty()) return false;

   //    // 第二级哈希查找精确位置
   //    size_t pos =    secondaryHash(key, bucket);
   //    if (pos >= tableSize) return false;

   //    return table[pos] == bucket_idx;
   // }

   size_t size() const {
      return tableSize;
   }
};
} // end namespace lingodb::runtime
#endif // LINGODB_RUNTIME_PERFECTHASHTABLE_H
