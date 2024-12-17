#pragma once
#include <stddef.h>  //NULL

//内存相关类
class CMemory
{
private:
	CMemory() {}  //构造函数，因为要做成单例类，所以构造函数是私有的

public:
	~CMemory() {};

private:
	static CMemory* m_instance;

public:
	static CMemory* GetInstance() //单例
	{
		if (m_instance == NULL)
		{
			//锁
			if (m_instance == NULL)
			{
				m_instance = new CMemory(); //第一次调用不应该放在线程中，应该放在主进程中，以免和其他线程调用冲突而导致同时执行两次new CMemory()
				static CGarhuishou cl;
			}
			//放锁
		}
		return m_instance;
	}
	class CGarhuishou  //内部类，用于释放对象
	{
	public:
		~CGarhuishou()
		{
			if (CMemory::m_instance)
			{
				delete CMemory::m_instance; //这个释放是整个系统退出的时候，系统来调用释放内存的哦
				CMemory::m_instance = NULL;
			}
		}
	};

public:
	void* AllocMemory(int memCount, bool ifmemset);  //分配内存
	void FreeMemory(void* point);                    //释放内存
};


