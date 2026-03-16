#include "../include/MemoryPool.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>

namespace MemoryPool {
MemoryPool::MemoryPool(size_t BlockSize)
    : BlockSize_(BlockSize), SlotSize_(0), firstBlock_(nullptr),
      curSlot_(nullptr), freeList_(nullptr), lastSlot_(nullptr) {}

MemoryPool::~MemoryPool() {
  Slot *cur = firstBlock_;
  while (cur) {
    Slot *next = cur->next;
    // 等同与 free(reinterpret_cast<void*>(firstBlock_));
    // 转化为 void 指针, 因为 void 类型不需要调用析构函数, 只释放空间
    operator delete(reinterpret_cast<void *>(cur));
    cur = next;
  }
}
void MemoryPool::init(size_t size) {
  assert(size > 0);
  SlotSize_ = size;
  firstBlock_ = nullptr;
  curSlot_ = nullptr;
  freeList_ = nullptr;
  lastSlot_ = nullptr;
}

void *MemoryPool::allocate() {
  // 优先使用空闲链表中的内存槽
  Slot *slot = popFreeList();
  if (slot != nullptr) {
    return slot;
  }

  Slot *temp;
  {
    std::lock_guard<std::mutex> lock(mutexForBlock_);
    if (curSlot_ >= lastSlot_) {
      allocateNewBlock();
    }
    temp = curSlot_;
    curSlot_ += SlotSize_ / sizeof(Slot);
  }

  return temp;
}

void MemoryPool::deallocate(void *ptr) {
  if (!ptr)
    return;
  Slot *slot = reinterpret_cast<Slot *>(ptr);
  pushFreeList(slot);
}

void MemoryPool::allocateNewBlock() {
  // 头插法插入新的内存块
  void *newBlock = operator new(BlockSize_);
  reinterpret_cast<Slot *>(newBlock)->next = firstBlock_;
  firstBlock_ = reinterpret_cast<Slot *>(newBlock);
  char *body = reinterpret_cast<char *>(newBlock) + sizeof(Slot *);
  size_t paddingSize = padPointer(body, SlotSize_); // 计算需要填充内存的大小
  curSlot_ = reinterpret_cast<Slot *>(body + paddingSize);

  lastSlot_ = reinterpret_cast<Slot *>(reinterpret_cast<size_t>(newBlock) +
                                       BlockSize_ - SlotSize_ + 1);
}

// 让指针对齐到槽大小的倍数位置
size_t MemoryPool::padPointer(char *p, size_t align) {
  size_t rem = (reinterpret_cast<size_t>(p) % align);
  return rem == 0 ? 0 : (align - rem);
}

// 实现无锁入队操作
bool MemoryPool::pushFreeList(Slot *slot) {
  while (true) {
    // 获取当前头节点
    Slot *oldHead = freeList_.load(std::memory_order_relaxed);
    // 将新的节点的 next 指向当前头节点
    slot->next.store(oldHead, std::memory_order_relaxed);

    // 尝试将新节点设置为头节点
    if (freeList_.compare_exchange_weak(oldHead, slot,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
      return true;
    }

    // 失败：说明另一个线程可能已经修改了 freeList_
    // CAS 失败则重试
  }
}

// 实现无锁出队操作

Slot *MemoryPool::popFreeList() {
  while (true) {
    Slot *oldHead = freeList_.load(std::memory_order_acquire);
    if (oldHead == nullptr)
      return nullptr; // 队列为空

    // 在访问 newhead 之前再次验证 oldhead 的有效性
    Slot *newhead = nullptr;
    try {
      newhead = oldHead->next.load(std::memory_order_relaxed);
    } catch (...) {
      // 如果返回失败，则 continue 重新尝试申请内存
      continue;
    }

    // 尝试更新头节点
    // 原子性地尝试将 freeList_ 从 oldHead 更新为 newhead
    if (freeList_.compare_exchange_weak(oldHead, newhead,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
      return oldHead;
    }

    // 失败：说明另一个线程可能已经修改了 freeList_
    // CAS 失败则重试
  }
}

void HashBucket::initMemoryPool() {
  for (int i = 0; i < MEMORY_POOL_NUM; i++) {
    getMemoryPool(i).init((i + 1) * SLOT_BASE_SIZE);
  }
}

// 单例模式
MemoryPool &HashBucket::getMemoryPool(int index) {
  static MemoryPool memoryPool[MEMORY_POOL_NUM];
  return memoryPool[index];
}

} // namespace MemoryPool
