#include "lingodb/runtime/PerfectHashTable.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <cstring>

void lingodb::runtime::PerfectHashView::constructTable() {
      // 5. 构建最终哈希表
   Entry emptyEntry;
   emptyEntry.hash = 0;
   emptyEntry.secondaryHash = 0;
   table.assign(tableSize, emptyEntry);
   for (size_t i = 0; i < buckets.size(); ++i) {
      const auto& bucket = buckets[i];
      if (bucket.keys.empty()) continue;

      size_t m = bucket.keys.size() * bucket.keys.size();

      for (const auto& key : bucket.keys) {
         size_t pos = (universalHash(key, bucket.hashA, bucket.hashB) % bucket.m) + bucket.offset;
         if (pos >= tableSize || table[pos].hash != 0) {
            throw std::runtime_error("Hash collision detected in final table");
         }
         table[pos].hash = universalHash(key, universalHashA, universalHashB);
         table[pos].secondaryHash = universalHash(key, bucket.hashA, bucket.hashB);
         table[pos].key = VarLen32::fromString(key);
      }
   }
}

size_t lingodb::runtime::PerfectHashView::universalHash(const std::string& key, size_t a, size_t b) const {
   // size_t hash = 0;
   // for (char c : key) {
   //    hash = (hash * a + c) % prime;
   // }
   // // make sure non zero
   // return (hash + b) % prime + 1;

   size_t hash = 0;
   const uint32_t prime = 0x7FFFFFFF; // 2^31 - 1
   size_t keyLen = key.size();
   auto* keyPtr = key.data();

   // 4 bytes as a unit to compute hash
   int i = 0;
   for (; i < keyLen - 4; i += 4) {
      uint32_t c;
      std::memcpy(&c, keyPtr+i, sizeof(uint32_t));
      hash = (hash * a + c) & prime;
   }

   // deal with not mutiply of 4 part
   size_t restLen = keyLen - i;
   if (restLen == 3) {
      uint32_t c;
      std::memcpy(&c, keyPtr+i, sizeof(uint32_t));
      c &= 0x00FFFFFF;
      hash = (hash * a + c) & prime;
   } else if (restLen == 2) {
      uint16_t c;
      std::memcpy(&c, keyPtr+i, sizeof(uint16_t));
      hash = (hash * a + c) & prime;
      restLen -= 2;
   } else if (restLen == 1) {
      uint8_t c = static_cast<uint8_t>(*(keyPtr+restLen));
      hash = (hash * a + c) & prime;
   }

   // printf("*** key %s %u\n", key.c_str(), (hash + b) % prime + 1);
   return (hash + b) % prime + 1;
}

bool lingodb::runtime::PerfectHashView::hasCollision(const std::vector<std::string>& keys, size_t a, size_t b, size_t m) const {
   // printf("*** hasCollision\n");
   std::vector<bool> occupied(m, false);
   for (const auto& key : keys) {
      size_t h = universalHash(key, a, b) % m;
      if (occupied[h]) {
      return true;
      }
      occupied[h] = true;
   }
   return false;
}

void lingodb::runtime::PerfectHashView::findHashParams(Bucket& bucket, size_t m) {
   const size_t max_attempts = 1000;
   for (size_t i = 0; i < max_attempts; ++i) {
      bucket.hashA = random(prime - 1) + 1;
      bucket.hashB = random(prime);

      if (!hasCollision(bucket.keys, bucket.hashA, bucket.hashB, m)) {
      return;
      }
   }
   throw std::runtime_error("Failed to find perfect hash function for bucket");
}

size_t lingodb::runtime::PerfectHashView::nextPrime(size_t n) const {
   if (n <= 1) return 2;
   while (true) {
      bool isPrime = true;
      for (size_t i = 2; i * i <= n; ++i) {
      if (n % i == 0) {
         isPrime = false;
         break;
      }
      }
      if (isPrime) return n;
      ++n;
   }
}

lingodb::runtime::PerfectHashView* lingodb::runtime::PerfectHashView::build(FlexibleBuffer* keyValues, FlexibleBuffer* paramValues) {
   lingodb::runtime::PerfectHashView* ph = new lingodb::runtime::PerfectHashView();
   auto* executionContext = runtime::getCurrentExecutionContext();
   executionContext->registerState({ph, [](void* ptr) { delete reinterpret_cast<lingodb::runtime::PerfectHashView*>(ptr); }});

   size_t vIdx = 0;
   size_t paramLen = paramValues->getLen();
   size_t bucketSize = paramLen -1;
   ph->buckets.reserve(bucketSize);
   Bucket b;
   printf("~~~ PerfectHashView::build %lu\n", bucketSize);
   paramValues->iterate([&](uint8_t* ptr) {
      if (vIdx == 0) {
         uint32_t* v = reinterpret_cast<uint32_t*>(ptr);
         ph->universalHashA = v[0];
         ph->universalHashB = v[1];
         ph->tableSize = v[2];
         ph->prime = v[3];
      } else {
         uint32_t* v = reinterpret_cast<uint32_t*>(ptr);
         b.hashA = v[0];
         b.hashB = v[1];
         b.m = v[2];
         b.offset = v[3];
         ph->buckets.push_back(b);

         // printf("~~~ bucket %u %u %u %u\n", b.hashA, b.hashA, b.m, b.offset);
      }

      vIdx++;
   });

   printf("~~~ prime %lu, a %lu, b %lu, tableSize %lu\n", ph->prime, ph->universalHashA, ph->universalHashB, ph->tableSize);

   keyValues->iterate([&](uint8_t* ptr) {
      VarLen32 v;
      std::memcpy(&v, ptr, sizeof(v));
      std::string key = v.str();
      size_t bucket_idx = ph->universalHash(key, ph->universalHashA, ph->universalHashB) % bucketSize;
      ph->buckets[bucket_idx].keys.push_back(key);
   });
   ph->constructTable();
   return ph;
}

lingodb::runtime::PerfectHashView* lingodb::runtime::PerfectHashView::construct(const std::vector<std::string>& keys) {
   lingodb::runtime::PerfectHashView* ph = new lingodb::runtime::PerfectHashView();
   auto* executionContext = runtime::getCurrentExecutionContext();
   executionContext->registerState({ph, [](void* ptr) { delete reinterpret_cast<lingodb::runtime::PerfectHashView*>(ptr); }});
   ph->constructUp(keys);
   return ph;
}

lingodb::runtime::PerfectHashView* lingodb::runtime::PerfectHashView::constructUp(const std::vector<std::string>& keys) {
   if (keys.empty()) {
      tableSize = 0;
      return this;
   }

   auto timeStart = std::chrono::high_resolution_clock::now();

   // 1. 选择通用哈希函数的参数和质数
   prime = nextPrime(keys.size() * keys.size());

   universalHashA = random(prime - 1) + 1;
   universalHashB = random(prime);

   // 2. 第一级哈希：将键分配到桶中
   size_t bucketSize = keys.size();
   buckets.resize(bucketSize);

   for (const auto& key : keys) {
      size_t bucket_idx = universalHash(key, universalHashA, universalHashB) % bucketSize;
      buckets[bucket_idx].keys.push_back(key);
   }

   auto hashEnd = std::chrono::high_resolution_clock::now();
   auto p1 = std::chrono::duration_cast<std::chrono::microseconds>(hashEnd - timeStart).count() / 1000.0;
   printf("--- hash calc: %lf\n", p1);

   // 3. 计算第二级哈希表的大小
   size_t total_slots = 0;
   for (const auto& bucket : buckets) {
      size_t m = nextPrime(bucket.keys.size() * bucket.keys.size());
      if (m == 0) m = 1; // 空桶至少一个槽位
      total_slots += m;
   }
   tableSize = nextPrime(total_slots);

   printf("<<< keys.size() %lu, prime %lu, a %lu, b %lu, tableSize %lu\n", keys.size(), prime, universalHashA, universalHashB, tableSize);

   auto sizeEnd = std::chrono::high_resolution_clock::now();
   auto p2 = std::chrono::duration_cast<std::chrono::microseconds>(sizeEnd - hashEnd).count() / 1000.0;
   printf("--- table size calc: %lf\n", p2);

   // 4. 为每个桶找到无冲突的哈希参数
   size_t offset = 0;
   for (auto& bucket : buckets) {
      size_t m = nextPrime(bucket.keys.size() * bucket.keys.size());
      if (m == 0) m = 1; // 空桶至少一个槽位

      bucket.m = m;
      bucket.offset = offset;
      if (!bucket.keys.empty()) {
         findHashParams(bucket, m);
      }
      offset += m;

      // printf("<<< bucket.keys.size() %lu %u %u %u %u\n", bucket.keys.size(), bucket.hashA, bucket.hashA, bucket.m, bucket.offset);
   }

   auto collideEnd = std::chrono::high_resolution_clock::now();
   auto p3 = std::chrono::duration_cast<std::chrono::microseconds>(collideEnd - sizeEnd).count() / 1000.0;
   printf("--- collide calc: %lf\n", p3);

   constructTable();

   auto constructEnd = std::chrono::high_resolution_clock::now();
   auto p4 = std::chrono::duration_cast<std::chrono::microseconds>(constructEnd - collideEnd).count() / 1000.0;
   printf("--- table construct calc: %lf\n", p4);

   auto timeEnd = std::chrono::high_resolution_clock::now();
   auto p5 = std::chrono::duration_cast<std::chrono::microseconds>(timeEnd - timeStart).count() / 1000.0;
   printf("--- all construct calc: %lf\n", p5);

   return this;
}
