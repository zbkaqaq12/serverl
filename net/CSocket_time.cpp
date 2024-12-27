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

/**
 * @brief 添加连接到定时器队列中
 * @details 为HTTP连接设置超时检查，包括keep-alive和请求处理超时
 *
 * @param pConn 需要添加到队列中的连接对象
 */
void CSocket::AddToTimerQueue(lpconnection_t pConn)
{
    if (!pConn || !pConn->httpCtx) return;

    time_t futtime = time(NULL);
    Timer::Type timerType;
    
    // 根据连接状态设置不同的超时时间和类型
    if (pConn->httpCtx->keepAlive) {
        futtime += 65;  // keep-alive连接65秒超时
        timerType = Timer::Type::KEEP_ALIVE;
    } else if (pConn->httpCtx->state == connection_s::HttpContext::ConnectionState::PROCESSING) {
        futtime += 30;  // 请求处理30秒超时
        timerType = Timer::Type::REQUEST;
    } else {
        futtime += 1800; // 空闲连接30分钟超时
        timerType = Timer::Type::IDLE;
    }

    std::lock_guard<std::mutex> lock(m_timequeueMutex);
    Timer timer(futtime, timerType, pConn, pConn->security->getSequence());
    m_timerQueue.insert(std::make_pair(futtime, timer));
    m_timer_value_ = m_timerQueue.begin()->first;
}

CSocket::Timer CSocket::GetOverTimeTimer(time_t cur_time)
{
    if (m_timerQueue.empty())
        return CSocket::Timer(0, CSocket::Timer::Type::IDLE, nullptr, 0);

    auto it = m_timerQueue.begin();
    if (it->first <= cur_time)
    {
        CSocket::Timer timer = it->second;
        m_timerQueue.erase(it);
        
        // 如果是keep-alive连接且仍然有效，则重新加入队列
        if (timer.type == CSocket::Timer::Type::KEEP_ALIVE && 
            timer.conn && 
            timer.conn->httpCtx && 
            timer.conn->httpCtx->keepAlive &&
            timer.sequence == timer.conn->security->getSequence())
        {
            time_t newExpireTime = cur_time + 65;  // 续期65秒
            CSocket::Timer newTimer(newExpireTime, CSocket::Timer::Type::KEEP_ALIVE, 
                         timer.conn, timer.sequence);
            m_timerQueue.insert(std::make_pair(newExpireTime, newTimer));
        }

        if (!m_timerQueue.empty()) {
            m_timer_value_ = m_timerQueue.begin()->first;
        }
        
        return timer;
    }
    
    return CSocket::Timer(0, CSocket::Timer::Type::IDLE, nullptr, 0);
}

void CSocket::DeleteFromTimerQueue(lpconnection_t pConn)
{
    if (!pConn) return;

    std::lock_guard<std::mutex> lock(m_timequeueMutex);
    
    for (auto it = m_timerQueue.begin(); it != m_timerQueue.end();) {
        if (it->second.conn == pConn) {
            it = m_timerQueue.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_timerQueue.empty()) {
        m_timer_value_ = m_timerQueue.begin()->first;
    }
}

void CSocket::clearAllFromTimerQueue()
{
    std::lock_guard<std::mutex> lock(m_timequeueMutex);
    m_timerQueue.clear();
    m_timer_value_ = 0;
}

/**
 * @brief HTTP连接定时器监控线程
 * @details 该线程定期检查连接的活跃状态，处理超时的HTTP连接，包括：
 *          1. 空闲连接超时
 *          2. keep-alive超时
 *          3. 请求处理超时
 * 
 * @param threadData 线程数据指针
 * @return void* 
 */
void* CSocket::ServerTimerQueueMonitorThread(void* threadData)
{
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CSocket* pSocketObj = pThread->_pThis;

    // 定义超时时间（秒）
    const time_t KEEPALIVE_TIMEOUT = 65;    // keep-alive超时时间
    const time_t IDLE_TIMEOUT = 1800;       // 空闲连接超时（30分钟）
    const time_t REQUEST_TIMEOUT = 30;      // 请求处理超时

    while (g_stopEvent == 0)
    {
        time_t currentTime = time(NULL);
        
        // 处理定时器队列中的超时连接
        Timer timer = pSocketObj->GetOverTimeTimer(currentTime);
        while (timer.conn != nullptr)
        {
            auto conn = timer.conn;
            if (conn->httpCtx)
            {
                // 根据定时器类型处理超时
                switch (timer.type)
                {
                    case Timer::Type::IDLE:
                    case Timer::Type::KEEP_ALIVE:
                        // 直接关闭空闲连接
                        conn->httpCtx->state = connection_s::HttpContext::ConnectionState::CLOSING;
                        conn->PutOneToFree();
                        break;

                    case Timer::Type::REQUEST:
                        // 发送408响应
                        if (conn->httpCtx->state == connection_s::HttpContext::ConnectionState::PROCESSING ||
                            conn->httpCtx->state == connection_s::HttpContext::ConnectionState::READING)
                        {
                            conn->httpCtx->response.setStatus(408);
                            conn->httpCtx->response.setHeader("Connection", "close");
                            (pSocketObj->*conn->whandler)(conn);
                        }
                        break;
                }
            }
            
            // 获取下一个超时定时器
            timer = pSocketObj->GetOverTimeTimer(currentTime);
        }

        // 每500ms检查一次
        usleep(500 * 1000);
    }

    return (void*)0;
}

