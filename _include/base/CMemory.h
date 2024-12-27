#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <stdexcept>

// 内存池类
class CMemory {
public:
	// 内存统计信息
	struct MemoryStats {
		std::atomic<size_t> totalAllocations{0};	// 总分配次数
		std::atomic<size_t> totalDeallocations{0};	// 总释放次数
		std::atomic<size_t> currentUsage{0};		// 当前使用的内存量
		std::atomic<size_t> peakUsage{0};		// 峰值内存使用量

		// 拷贝构造函数
		MemoryStats(const MemoryStats& other) {
			totalAllocations.store(other.totalAllocations.load());
			totalDeallocations.store(other.totalDeallocations.load());
			currentUsage.store(other.currentUsage.load());
			peakUsage.store(other.peakUsage.load());
		}

		// 赋值运算符
		MemoryStats& operator=(const MemoryStats& other) {
			if (this != &other) {
				totalAllocations.store(other.totalAllocations.load());
				totalDeallocations.store(other.totalDeallocations.load());
				currentUsage.store(other.currentUsage.load());
				peakUsage.store(other.peakUsage.load());
			}
			return *this;
		}

		MemoryStats() = default;
	};

	// 用于返回统计信息的非原子结构体
	struct MemoryStatsSnapshot {
		size_t totalAllocations;
		size_t totalDeallocations;
		size_t currentUsage;
		size_t peakUsage;
	};

private:
	// 内存块结构
	struct MemoryBlock {
		char* memory;			// 实际的内存块
		size_t blockSize;		// 块大小
		bool* usedFlags;		// 使用标志数组
		size_t totalSlots;		// 总槽位数
		size_t usedSlots;		// 已使用槽位数
		
		MemoryBlock(size_t size, size_t count) : 
			blockSize(size), 
			totalSlots(count), 
			usedSlots(0) {
			memory = new char[size * count];
			usedFlags = new bool[count]();	// 初始化为false
		}
		
		~MemoryBlock() {
			delete[] memory;
			delete[] usedFlags;
		}
	};

	// 内存池配置
	struct PoolConfig {
		size_t blockSize;		// 块大小
		size_t initialBlocks;	// 初始块数
		size_t maxBlocks;		// 最大块数
	};

private:
	static CMemory* m_instance;
	static std::mutex m_instanceMutex;
	
	std::vector<std::unique_ptr<MemoryBlock>> m_smallPool;	// 小块内存池 (<=128字节)
	std::vector<std::unique_ptr<MemoryBlock>> m_mediumPool;	// 中块内存池 (<=1024字节)
	std::vector<std::unique_ptr<MemoryBlock>> m_largePool;	// 大块内存池 (<=4096字节)
	
	std::mutex m_smallPoolMutex;
	std::mutex m_mediumPoolMutex;
	std::mutex m_largePoolMutex;
	
	MemoryStats m_stats;
	
	// 内存池配置
	const size_t SMALL_BLOCK_SIZE = 128;
	const size_t MEDIUM_BLOCK_SIZE = 1024;
	const size_t LARGE_BLOCK_SIZE = 4096;
	const size_t BLOCKS_PER_CHUNK = 32;	// 每个内存块包含的对象数

	// 内存块追踪
	std::unordered_map<void*, MemoryBlock*> m_memoryMap;
	std::mutex m_mapMutex;

private:
	CMemory();	// 私有构造函数
	~CMemory();
	CMemory(const CMemory&) = delete;
	CMemory& operator=(const CMemory&) = delete;

	void* AllocateFromPool(std::vector<std::unique_ptr<MemoryBlock>>& pool, 
						  std::mutex& poolMutex, 
						  size_t size);
	bool DeallocateFromPool(std::vector<std::unique_ptr<MemoryBlock>>& pool,
						   std::mutex& poolMutex,
						   void* pointer);
	MemoryBlock* CreateNewBlock(size_t blockSize, size_t blockCount);

public:
	static CMemory* GetInstance() {
		if (m_instance == nullptr) {
			std::lock_guard<std::mutex> lock(m_instanceMutex);
			if (m_instance == nullptr) {
				m_instance = new CMemory();
			}
		}
		return m_instance;
	}

	// 内存分配接口
	void* AllocMemory(size_t size, bool ifmemset = false);
	void FreeMemory(void* pointer);
	
	// 获取统计信息
	MemoryStatsSnapshot GetStats() const {
		return {
			m_stats.totalAllocations.load(),
			m_stats.totalDeallocations.load(),
			m_stats.currentUsage.load(),
			m_stats.peakUsage.load()
		};
	}
	
	// 内存池状态
	void PrintStats() const;
	void Cleanup();	// 清理未使用的内存块
};


