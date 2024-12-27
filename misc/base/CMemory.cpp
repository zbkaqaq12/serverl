#include "CMemory.h"
#include <iostream>
#include <cstring>
#include "Logger.h"

// 静态成员初始化
CMemory* CMemory::m_instance = nullptr;
std::mutex CMemory::m_instanceMutex;

CMemory::CMemory() {
	// 预分配内存池
	auto logger = Logger::GetInstance();
	try {
		// 初始化小块内存池
		m_smallPool.push_back(std::make_unique<MemoryBlock>(SMALL_BLOCK_SIZE, BLOCKS_PER_CHUNK));
		// 初始化中块内存池
		m_mediumPool.push_back(std::make_unique<MemoryBlock>(MEDIUM_BLOCK_SIZE, BLOCKS_PER_CHUNK));
		// 初始化大块内存池
		m_largePool.push_back(std::make_unique<MemoryBlock>(LARGE_BLOCK_SIZE, BLOCKS_PER_CHUNK));
		
		logger->logSystem(LogLevel::INFO, "Memory pool initialized successfully");
	} catch (const std::exception& e) {
		logger->logSystem(LogLevel::ERROR, "Failed to initialize memory pool: %s", e.what());
		throw;
	}
}

CMemory::~CMemory() {
	Cleanup();
}

void* CMemory::AllocMemory(size_t size, bool ifmemset) {
	void* memory = nullptr;
	
	// 根据大小选择合适的内存池
	if (size <= SMALL_BLOCK_SIZE) {
		memory = AllocateFromPool(m_smallPool, m_smallPoolMutex, size);
	} else if (size <= MEDIUM_BLOCK_SIZE) {
		memory = AllocateFromPool(m_mediumPool, m_mediumPoolMutex, size);
	} else if (size <= LARGE_BLOCK_SIZE) {
		memory = AllocateFromPool(m_largePool, m_largePoolMutex, size);
	} else {
		// 对于超大块，直接使用new分配
		memory = new char[size];
	}
	
	if (memory && ifmemset) {
		memset(memory, 0, size);
	}
	
	// 更新统计信息
	if (memory) {
		m_stats.totalAllocations++;
		m_stats.currentUsage += size;
		m_stats.peakUsage = std::max(m_stats.peakUsage.load(), m_stats.currentUsage.load());
	}
	
	return memory;
}

void CMemory::FreeMemory(void* pointer) {
	if (!pointer) return;
	
	bool freed = false;
	{
		std::lock_guard<std::mutex> lock(m_mapMutex);
		auto it = m_memoryMap.find(pointer);
		if (it != m_memoryMap.end()) {
			MemoryBlock* block = it->second;
			size_t size = block->blockSize;
			
			// 根据块大小选择正确的池进行释放
			if (size <= SMALL_BLOCK_SIZE) {
				freed = DeallocateFromPool(m_smallPool, m_smallPoolMutex, pointer);
			} else if (size <= MEDIUM_BLOCK_SIZE) {
				freed = DeallocateFromPool(m_mediumPool, m_mediumPoolMutex, pointer);
			} else if (size <= LARGE_BLOCK_SIZE) {
				freed = DeallocateFromPool(m_largePool, m_largePoolMutex, pointer);
			}
			
			if (freed) {
				m_memoryMap.erase(it);
				m_stats.totalDeallocations++;
				m_stats.currentUsage -= size;
			}
		}
	}
	
	if (!freed) {
		// 如果不在内存池中，假设是直接分配的大块内存
		delete[] static_cast<char*>(pointer);
		m_stats.totalDeallocations++;
	}
}

void* CMemory::AllocateFromPool(std::vector<std::unique_ptr<MemoryBlock>>& pool,
							   std::mutex& poolMutex,
							   size_t size) {
	std::lock_guard<std::mutex> lock(poolMutex);
	
	// 在现有块中查找空闲槽位
	for (auto& block : pool) {
		if (block->usedSlots < block->totalSlots) {
			// 查找第一个未使用的槽位
			for (size_t i = 0; i < block->totalSlots; ++i) {
				if (!block->usedFlags[i]) {
					block->usedFlags[i] = true;
					block->usedSlots++;
					void* memory = block->memory + (i * block->blockSize);
					
					// 记录内存块信息
					{
						std::lock_guard<std::mutex> mapLock(m_mapMutex);
						m_memoryMap[memory] = block.get();
					}
					
					return memory;
				}
			}
		}
	}
	
	// 如果没有可用槽位，创建新块
	try {
		auto newBlock = std::make_unique<MemoryBlock>(size, BLOCKS_PER_CHUNK);
		newBlock->usedFlags[0] = true;
		newBlock->usedSlots = 1;
		void* memory = newBlock->memory;
		
		// 记录内存块信息
		{
			std::lock_guard<std::mutex> mapLock(m_mapMutex);
			m_memoryMap[memory] = newBlock.get();
		}
		
		pool.push_back(std::move(newBlock));
		return memory;
	} catch (const std::bad_alloc& e) {
		auto logger = Logger::GetInstance();
		logger->logSystem(LogLevel::ERROR, "Failed to allocate new memory block: %s", e.what());
		return nullptr;
	}
}

bool CMemory::DeallocateFromPool(std::vector<std::unique_ptr<MemoryBlock>>& pool,
								std::mutex& poolMutex,
								void* pointer) {
	std::lock_guard<std::mutex> lock(poolMutex);
	
	// 查找指针所属的内存块
	for (auto& block : pool) {
		char* blockStart = block->memory;
		char* blockEnd = blockStart + (block->totalSlots * block->blockSize);
		
		if (pointer >= blockStart && pointer < blockEnd) {
			// 计算槽位索引
			size_t index = (static_cast<char*>(pointer) - blockStart) / block->blockSize;
			if (index < block->totalSlots && block->usedFlags[index]) {
				block->usedFlags[index] = false;
				block->usedSlots--;
				return true;
			}
		}
	}
	
	return false;
}

void CMemory::PrintStats() const {
	auto logger = Logger::GetInstance();
	logger->logSystem(LogLevel::INFO, "Memory Pool Statistics:");
	logger->logSystem(LogLevel::INFO, "Total Allocations: %zu", m_stats.totalAllocations.load());
	logger->logSystem(LogLevel::INFO, "Total Deallocations: %zu", m_stats.totalDeallocations.load());
	logger->logSystem(LogLevel::INFO, "Current Memory Usage: %zu bytes", m_stats.currentUsage.load());
	logger->logSystem(LogLevel::INFO, "Peak Memory Usage: %zu bytes", m_stats.peakUsage.load());
}

void CMemory::Cleanup() {
	// 清理小块内存池
	{
		std::lock_guard<std::mutex> lock(m_smallPoolMutex);
		m_smallPool.clear();
	}
	
	// 清理中块内存池
	{
		std::lock_guard<std::mutex> lock(m_mediumPoolMutex);
		m_mediumPool.clear();
	}
	
	// 清理大块内存池
	{
		std::lock_guard<std::mutex> lock(m_largePoolMutex);
		m_largePool.clear();
	}
	
	// 清理内存映射
	{
		std::lock_guard<std::mutex> lock(m_mapMutex);
		m_memoryMap.clear();
	}
	
	// 重置统计信息
	m_stats.currentUsage = 0;
	auto logger = Logger::GetInstance();
	logger->logSystem(LogLevel::INFO, "Memory pool cleaned up successfully");
}