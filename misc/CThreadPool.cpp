#include "CThreadPool.h"
#include "CMemory.h"
#include"global.h"
#include<unistd.h>

// 定义静态成员变量
std::mutex CThreadPool::m_mutex;                // 定义静态互斥量
std::condition_variable CThreadPool::m_cond;     // 定义静态条件变量
bool CThreadPool::m_shutdown= false;                       // 线程退出标志，false不退出，true退出


CThreadPool::~CThreadPool()
{
    StopAll();
}

bool CThreadPool::Create(int threadNum)
{
    m_iThreadNum = threadNum;

    // 创建线程并启动
    for (int i = 0; i < m_iThreadNum; ++i) {
        auto pNew = new ThreadItem(this);
        m_threadVector.push_back(pNew);
        m_threadVector[i]->_Handle = std::thread(&CThreadPool::ThreadFunc,pNew);
    }

    // 等待所有线程启动完毕
lblfor:
    for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if ((*iter)->ifRunning == false) //这个条件保证所有线程完全启动起来，以保证整个线程池中的线程正常工作；
        {
            //这说明有没有启动完全的线程
            usleep(100 * 1000);  //单位是微妙,又因为1毫秒=1000微妙，所以 100 *1000 = 100毫秒
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
    globallogger->clog(LogLevel::NOTICE, "CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!" );
}

void CThreadPool::inMsgRecvQueueAndSignal(char* buf) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_MsgRecvQueue.push_back(buf);
        ++m_iRecvMsgQueueCount;
    }

    // 唤醒一个线程来处理任务
    Call();
}


void CThreadPool::Call() {
    if (m_iRunningThreadNum < m_iThreadNum) {
        m_cond.notify_one();  // 唤醒一个线程
    }
    else {
        // 如果所有线程都忙，可能需要扩容线程池
        globallogger->flog(LogLevel::ERROR, "CThreadPool::Call()发现线程池中当前空闲线程数量为0，考虑扩容线程池!" );
    }
}

int CThreadPool::getRecvMsgQueueCount() const
{
    return m_iRecvMsgQueueCount;
}

void CThreadPool::ThreadFunc(void* threadData)
{
    //这个是静态成员函数，是不存在this指针的；
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool* pThreadPoolObj = pThread->_pThis;

    while (true) {
        std::unique_lock<std::mutex> lock(CThreadPool::m_mutex);

        // 如果线程池关闭，退出
        if (m_shutdown) {
            break;
        }

        // 等待消息或者线程池关闭信号
        while (pThreadPoolObj->m_MsgRecvQueue.empty() && !m_shutdown) {
            if (pThread->ifRunning == false)
                pThread->ifRunning = true;
            CThreadPool::m_cond.wait(lock);
        }

        // 如果线程池关闭了，退出
        if (m_shutdown ) {
            lock.unlock();
            break;
        }

        ++pThreadPoolObj->m_iRunningThreadNum;    //原子+1【记录正在干活的线程数量增加1】，这比互斥量要快很多


        // 处理消息
        char* jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;
        if(jobbuf == nullptr)
            std::cout <<" xiaoxi : " << jobbuf << std::endl;
        lock.unlock();

        // 处理接收到的消息
        g_socket.threadRecvProcFunc(jobbuf); // 处理消息的函数

        delete[] jobbuf;  // 释放消息内存
        --pThreadPoolObj->m_iRunningThreadNum;
    }
}

void CThreadPool::clearMsgRecvQueue() {
    while (!m_MsgRecvQueue.empty()) {
        char* msg = m_MsgRecvQueue.front();
        delete[] msg;  // Deallocate the memory
        m_MsgRecvQueue.pop_front();  // Remove the element from the queue
    }
}