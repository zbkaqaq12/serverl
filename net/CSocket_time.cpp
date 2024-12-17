#include"global.h"
#include"CSocket.h"
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h>   //多线程
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include "CMemory.h"

//处理连接线程池(用multimap管理定时器)，如果用户成功连入，然后我们可以开启踢人开关。Sock_WaitTimeEnable = 1，此时这个连接就开始计时了
/**
 * @brief 添加连接到定时器队列中
 * @details 当用户成功连入并且开启了踢人开关时，此函数将连接对象添加到定时器队列中，设置一个未来时间，用于指定的时间到达时检查是否需要踢出连接。
 *
 * @param pConn 需要添加到队列中的连接对象
 */
void CSocket::AddToTimerQueue(lpconnection_t pConn)
{
	CMemory* p_memory = CMemory::GetInstance();

	time_t futtime = time(NULL);
	futtime += m_iWaitTime;  //20秒之后的时间

	//CLock lock(&m_timequeueMutex); //互斥，因为要操作m_timeQueuemap了
	std::lock_guard<std::mutex> lock(m_timequeueMutex);
	LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
	tmpMsgHeader->pConn = pConn;
	tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
	m_timerQueuemap.insert(std::make_pair(futtime, tmpMsgHeader)); //按键 自动排序 小->大
	m_cur_size_++;  //计时队列尺寸+1
	m_timer_value_ = GetEarliestTime(); //计时队列头部的时间值保存到m_timer_value_里
	return;
}

//从multimap中取得最早的时间返回去，调用者负责互斥，所以本函数不用互斥，调用者必须确保m_timeQueuemap中一定有数据
/**
 * @brief 获取定时器队列中的最早时间
 * @details 从定时器队列中获取最早时间值（优先队列的首节点时间）。
 * 
 * @return time_t 返回队列中最早的时间值
 */
time_t CSocket::GetEarliestTime()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
	pos = m_timerQueuemap.begin();
	return pos->first;
}

//从m_timeQueuemap移除最早的时间，并把最早的时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
/**
 * @brief 移除定时器队列中最早的节点
 * @details 从定时器队列中移除最早到期的节点，并返回该节点的数据。调用者需要确保调用此函数时队列不为空。
 *
 * @return LPSTRUC_MSG_HEADER 返回被移除的节点指针，如果队列为空则返回NULL
 */
LPSTRUC_MSG_HEADER CSocket::RemoveFirstTimer()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
	LPSTRUC_MSG_HEADER p_tmp;
	if (m_cur_size_ <= 0)
	{
		return NULL;
	}
	pos = m_timerQueuemap.begin(); //调用者负责互斥，所以本函数不用互斥
	p_tmp = pos->second;
	m_timerQueuemap.erase(pos);
	--m_cur_size_; //减去一个元素，必然要把尺寸减少1；
	return p_tmp;
}

//根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点
//调用者负责互斥，所以本函数不用互斥
/**
 * @brief 获取超时的定时器节点
 * @details 根据当前时间从定时器队列中获取所有超时的节点。超时的节点会被移除并返回，便于后续处理超时逻辑。
 *
 * @param cur_time 当前时间
 * @return LPSTRUC_MSG_HEADER 返回超时的定时器节点指针，如果没有超时节点则返回NULL
 */
LPSTRUC_MSG_HEADER CSocket::GetOverTimeTimer(time_t cur_time)
{
	CMemory* p_memory = CMemory::GetInstance();
	LPSTRUC_MSG_HEADER ptmp;

	if (m_cur_size_ == 0 || m_timerQueuemap.empty())
		return NULL; //队列为空

	time_t earliesttime = GetEarliestTime(); //到multimap中去查询
	if (earliesttime <= cur_time)
	{
		//这回确实有到期的节点【超时的节点】
		ptmp = RemoveFirstTimer();    //把这个超时的节点从m_timerQueuemap 删除并把这个节点的第二项（指针）返回

		if (/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick != 1)  //能调用到这里的，都是时间到了，超时的节点，要么要踢，要么要延长其生存时间
		{
			//如果不是要踢人，则最后一次超时的那个时间点开始重新计时
			//因为下次超时的时间点还是要判断的，所以还要把这个节点加回来        
			time_t newinqueutime = cur_time + (m_iWaitTime);
			LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(sizeof(STRUC_MSG_HEADER), false);
			tmpMsgHeader->pConn = ptmp->pConn;
			tmpMsgHeader->iCurrsequence = ptmp->iCurrsequence;
			m_timerQueuemap.insert(std::make_pair(newinqueutime, tmpMsgHeader)); //自动排序 小->大			
			m_cur_size_++;
		}

		if (m_cur_size_ > 0) //这个判断条件必要，因为以后可能在这里扩充别的代码
		{
			m_timer_value_ = GetEarliestTime(); //计时队列头部的时间值保存到m_timer_value_里
		}
		return ptmp;
	}
	return NULL;
}

/**
 * @brief 从定时器队列中移除指定连接
 * @details 根据传入的连接对象，从定时器队列中查找并移除该连接的定时器节点，释放相关内存。
 *
 * @param pConn 需要从定时器队列中移除的连接对象
 */
void CSocket::DeleteFromTimerQueue(lpconnection_t pConn)
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;
	CMemory* p_memory = CMemory::GetInstance();

	std::lock_guard<std::mutex> lock(m_timequeueMutex);

	//因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下遍历整个队列 来找这个对象 操作起来比较稳妥
lblMTQM:
	pos = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for (; pos != posend; ++pos)
	{
		if (pos->second->pConn == pConn)
		{
			p_memory->FreeMemory(pos->second);  //释放内存
			m_timerQueuemap.erase(pos);
			--m_cur_size_; //减去一个元素，必然要把尺寸减少1；								
			goto lblMTQM;
		}
	}
	if (m_cur_size_ > 0)
	{
		m_timer_value_ = GetEarliestTime();
	}
	return;
}

/**
 * @brief 清空定时器队列
 * @details 清空定时器队列中的所有节点，释放所有相关的内存。
 */
void CSocket::clearAllFromTimerQueue()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;

	CMemory* p_memory = CMemory::GetInstance();
	pos = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();
	for (; pos != posend; ++pos)
	{
		p_memory->FreeMemory(pos->second);
		--m_cur_size_;
	}
	m_timerQueuemap.clear();
}

/**
 * @brief 定时器队列监控线程
 * @details 该线程定期检查定时器队列，处理已超时的连接。超时的连接会被清理并进入后续的线程处理。
 *
 * @param threadData 线程传入的数据（包含指向CSocket对象的指针）
 * @return void* 返回值为0，表示线程执行完毕
 */
void* CSocket::ServerTimerQueueMonitorThread(void* threadData)
{
	ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CSocket* pSocketObj = pThread->_pThis;

    time_t absolute_time, cur_time;
    
    while (g_stopEvent == 0) // 线程退出的标志
    {
        if (pSocketObj->m_cur_size_ > 0) // 队列不为空
        {
            absolute_time = pSocketObj->m_timer_value_; // 取得最早的事件时间
            cur_time = time(NULL);
            if (absolute_time < cur_time) // 时间到期
            {
                std::list<LPSTRUC_MSG_HEADER> m_lsIdleList; // 保存要处理的内容
                LPSTRUC_MSG_HEADER result;

                // 批量取出所有超时的节点
                {
                    std::lock_guard<std::mutex> lock(pSocketObj->m_timequeueMutex);
                    // 获取所有超时的节点
                    while ((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL)
                    {
                        m_lsIdleList.push_back(result);
                    }
                }
                // 处理超时事件
                LPSTRUC_MSG_HEADER tmpmsg;
                while (!m_lsIdleList.empty())
                {
                    tmpmsg = m_lsIdleList.front();
                    m_lsIdleList.pop_front();
                    pSocketObj->procPingTimeOutChecking(tmpmsg, cur_time); // 处理超时消息
                }
            }
        }

        usleep(500 * 1000); // 休息 500 毫秒
    }

    return (void*)0;
}

//时间队列监视线程，处理到期的事件，本函数只是内存释放，子类应该重新实现该函数以实现具体的判断动作
void CSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
	CMemory* p_memory = CMemory::GetInstance();
	p_memory->FreeMemory(tmpmsg);
}

