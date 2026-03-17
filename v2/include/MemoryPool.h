#pragma once
#include "ThreadCache.h"
#include <cstddef>

namespace MemoryPool {

class MemoryPool {

public:
  static void *allocate(size_t size) {
    return ThreedCache::getInstance()->allocate(size);
  }

  static void deallocate(void *ptr, size_t size) {
    ThreedCache::getInstance()->deallocate(ptr, size);
  }
};

} // namespace MemoryPool
