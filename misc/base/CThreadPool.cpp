#include "CThreadPool.h"
#include "CMemory.h"
#include "global.h"
#include "router.h"
#include <unistd.h>
#include <cstring>  // for memcpy

// 静态成员变量定义
std::mutex CThreadPool::m_mutex;                // 互斥锁
std::condition_variable CThreadPool::m_cond;     // 条件变量
bool CThreadPool::m_shutdown = false;            // 线程退出标志，false不退出，true退出

CThreadPool::~CThreadPool()
{
    StopAll();
}

bool CThreadPool::Create(int threadNum)
{
    m_iThreadNum = threadNum;

    // 创建线程池中的线程
    for (int i = 0; i < m_iThreadNum; ++i) {
        auto pNew = new ThreadItem(this);
        m_threadVector.push_back(pNew);
        m_threadVector[i]->_Handle = std::thread(&CThreadPool::ThreadFunc,pNew);
    }

    // 等待所有线程创建完成
lblfor:
    for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if ((*iter)->ifRunning == false) // 若为false，表示该线程还未完全启动，需要等待所有线程都启动完成
        {
            // 说明线程还没有完全启动
            usleep(100 * 1000);  // 单位是微秒，这里就是100毫秒
            goto lblfor;
        }
    }
    return true;
}

void CThreadPool::StopAll() {
    if (m_shutdown) {
        return;
    }

    m_shutdown = true;

    // 唤醒所有线程
    m_cond.notify_all();

    // 等待线程结束
    for (auto& threadItem : m_threadVector) {
        if (threadItem->_Handle.joinable()) {
            threadItem->_Handle.join();
        }
    }

    clearMsgRecvQueue();  // 清理消息队列
    m_threadVector.clear();
    Logger::GetInstance()->logSystem(LogLevel::INFO, "CThreadPool::StopAll()成功退出，线程池中线程全部正常结束!");
}

void CThreadPool::inMsgRecvQueueAndSignal(char* buf) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_MsgRecvQueue.push_back(buf);
        ++m_iRecvMsgQueueCount;
    }

    // 唤醒一个线程来处理消息
    Call();
}

void CThreadPool::Call() {
    if (m_iRunningThreadNum < m_iThreadNum) {
        m_cond.notify_one();  // 唤醒一个线程
    }
    else {
        // 所有线程都在忙，需要扩展线程池
        Logger::GetInstance()->logSystem(LogLevel::ERROR, "CThreadPool::Call()中线程池中当前空闲线程数为0，需要扩展线程池!");
    }
}

int CThreadPool::getRecvMsgQueueCount() const
{
    return m_iRecvMsgQueueCount;
}

void CThreadPool::ThreadFunc(void* threadData)
{
    // 这是静态成员函数，没有this指针
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool* pThreadPoolObj = pThread->_pThis;
    auto logger = Logger::GetInstance();

    while (true) {
        std::unique_lock<std::mutex> lock(CThreadPool::m_mutex);

        // 如果线程池关闭，退出
        if (m_shutdown) {
            break;
        }

        // 等待消息或线程池关闭信号
        while (pThreadPoolObj->m_MsgRecvQueue.empty() && !m_shutdown) {
            if (pThread->ifRunning == false)
                pThread->ifRunning = true;
            CThreadPool::m_cond.wait(lock);
        }

        // 如果线程池关闭了，退出
        if (m_shutdown) {
            lock.unlock();
            break;
        }

        ++pThreadPoolObj->m_iRunningThreadNum;    // 原子+1，记录正在干活的线程数量，这个+1必须放在这里

        // 处理消息
        char* jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;
        
        if(jobbuf == nullptr) {
            logger->logSystem(LogLevel::ERROR, "ThreadFunc received null message buffer");
            lock.unlock();
            --pThreadPoolObj->m_iRunningThreadNum;
            continue;
        }
        lock.unlock();

        // 从消息中获取连接指针
        lpconnection_t pConn;
        memcpy(&pConn, jobbuf, sizeof(lpconnection_t));
        
        // 检查连接是否有效
        if (!pConn || pConn->fd == -1) {
            logger->logSystem(LogLevel::ERROR, "Invalid connection in ThreadFunc");
            delete[] jobbuf;
            --pThreadPoolObj->m_iRunningThreadNum;
            continue;
        }
        
        // 检查连接状态
        if (pConn->httpCtx->state != connection_s::HttpContext::ConnectionState::PROCESSING) {
            logger->logSystem(LogLevel::ERROR, "Connection is not in processing state");
            delete[] jobbuf;
            --pThreadPoolObj->m_iRunningThreadNum;
            continue;
        }
        
        // 使用路由系统处理请求
        bool result = Router::GetInstance()->handleRequest(pConn);
        if (!result) {
            // 如果路由处理失败，返回500错误
            std::string errorResponse = "HTTP/1.1 500 Internal Server Error\r\n"
                                 "Content-Length: 21\r\n"
                                 "Connection: close\r\n"
                                 "\r\n"
                                 "Internal Server Error";
            pConn->writeBuffer.append(errorResponse.c_str(), errorResponse.length());
            pConn->httpCtx->keepAlive = false;
        }
        
        // 更新连接状态
        pConn->httpCtx->state = connection_s::HttpContext::ConnectionState::WRITING;

        delete[] jobbuf;  // 释放消息内存
        --pThreadPoolObj->m_iRunningThreadNum;
    }
}

void CThreadPool::clearMsgRecvQueue() {
    while (!m_MsgRecvQueue.empty()) {
        char* msg = m_MsgRecvQueue.front();
        delete[] msg;  // 释放内存
        m_MsgRecvQueue.pop_front();  // 从队列中移除元素
    }
}