#pragma once
#include "Common.h"
#include <cstddef>
#include <map>
#include <mutex>

namespace MemoryPool {

class PageCache {
public:
  static const size_t PAGE_SIZE = 4096; // 4K 页大小

  static PageCache &getIntance() {
    static PageCache instance;
    return instance;
  }

private:
  PageCache() = default;

  // 向系统申请内存
  void stytemAlloc(size_t numPages);

private:
  struct Span {
    void *PageAddr;  // 页起始地址
    size_t numPages; // 页数
    Span *next;      // 链表指针
  };

  // 按页数管理空闲 span， 不同页数对应不同 Span 链表
  std::map<size_t, Span *> freeSpans_;
  // 页号 span 的映射,用于回收
  std::map<void *, Span *> spanMap_;
  std::mutex mutex_;
};

} // namespace MemoryPool
