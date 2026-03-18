#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"
#include <cstddef>
#include <cstdlib>

namespace MemoryPool {

void *ThreedCache::allocate(size_t size) {
  // 处理 0 大小的分配请求
  if (size == 0) {
    size = ALIGNMENT;
  }

  if (size > MAX_BYTES) {
    // 大对象直接从系统分配
    return malloc(size);
  }

  size_t index = SizeClass::getIndex(size);

  // 更新对应自由链表的长度计数
  freeListSize_[index]--;

  // 检查线程本地自由链表
  // 如果 freeList_[index] 不为空，表示该链表中有可用的内存块
  if (void *ptr = freeList_[index]) {
    freeList_[index] = *reinterpret_cast<void **>(
        ptr); // 将 freeList_[index]
              // 指向内存块的下一个内存块地址（取决于内存块的实现）
    return ptr;
  }

  // 如果线程本地自由链表为空，则从中心缓存获取一批内存
  return fetchFromCentralCache(index);
}

void ThreedCache::deallocate(void *ptr, size_t size) {
  if (size > MAX_BYTES) {
    free(ptr);
    return;
  }

  size_t index = SizeClass ::getIndex(size);

  // 插入到线程本地自由链表
  *reinterpret_cast<void **>(ptr) = freeList_[index];
  freeList_[index] = ptr;

  // 更新对应自由链表的长度计数
  freeListSize_[index]++;

  // 判断是否需要将部分内存回收给中心缓存
  if (shouldReturnToCentralCache(index)) {
    returnToCentralCache(freeList_[index], size);
  }
}

bool ThreedCache::shouldReturnToCentralCache(size_t index) {
  // 设定阈值，例如：当自由链表的大小超过一定数量时
  size_t threshold = 256;
  return (freeListSize_[index] > threshold);
}

void *ThreedCache::fetchFromCentralCache(size_t index) {
  // 从中心缓存批量获取内存
  void *start = CentralCache::getInstance().fetchRange(index);
  if (!start)
    return nullptr;

  void *result = start;
  freeList_[index] = *reinterpret_cast<void **>(start);

  // 更新自由链表的大小
  size_t batchNum = 0;
  void *current = start; // 从 start 开始遍历

  // 计算从中心缓存获取的内存数量
  while (current != nullptr) {
    batchNum++;
    current = *reinterpret_cast<void **>(current); // 遍历下一个内存块
  }

  // 更新 freeListSize_, 增加获取的内存块数量
  freeListSize_[index] += batchNum;

  return result;
}

void ThreedCache::returnToCentralCache(void *start, size_t size) {
  // 根据大小计算对应的索引
  size_t index = SizeClass::getIndex(size);

  // 获取对齐后的实际块大小
  size_t alignedSize = SizeClass::roundUp(size);

  // 计算要归还内存块数量
  size_t batchNum = freeListSize_[index];
  if (batchNum <= 1)
    return; // 如果只有一个块，则不归还

  // 保留一部分在 ThreedCache 中 （比如保留 1/4）
  size_t keepNum = std::max(batchNum / 4, size_t(1));
  size_t returnNum = batchNum - keepNum;

  // 将内存块串成链表
  char *current = static_cast<char *>(start);
  // 使用对齐后的大小计算分割点
  char *splitNode = current;
  for (size_t i = 0; i < keepNum - 1; ++i) {
    splitNode = reinterpret_cast<char *>(*reinterpret_cast<void **>(splitNode));
    if (splitNode == nullptr) {
      // 如果链表提前结束，更新实际的返回数量
      returnNum = batchNum - (i + 1);
      break;
    }
  }

  if (splitNode != nullptr) {
    // 将要返回的部分和要保留的部分断开
    void *nextNode = *reinterpret_cast<void **>(splitNode);
    *reinterpret_cast<void **>(splitNode) = nullptr; // 断开连接

    // 更新 ThreedCache 的空闲链表
    freeList_[index] = start;

    // 更新自由链表的大小
    freeListSize_[index] = keepNum;

    // 将剩余部分返回给 CentralCache
    if (returnNum > 0 && nextNode != nullptr) {
      CentralCache::getInstance().returnRange(nextNode, returnNum * alignedSize,
                                              index);
    }
  }
}

} // namespace MemoryPool
