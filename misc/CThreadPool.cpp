#include "CThreadPool.h"
#include "CMemory.h"
#include"global.h"
#include<unistd.h>

// ���徲̬��Ա����
std::mutex CThreadPool::m_mutex;                // ���徲̬������
std::condition_variable CThreadPool::m_cond;     // ���徲̬��������
bool CThreadPool::m_shutdown= false;                       // �߳��˳���־��false���˳���true�˳�


CThreadPool::~CThreadPool()
{
    StopAll();
}

bool CThreadPool::Create(int threadNum)
{
    m_iThreadNum = threadNum;

    // �����̲߳�����
    for (int i = 0; i < m_iThreadNum; ++i) {
        auto pNew = new ThreadItem(this);
        m_threadVector.push_back(pNew);
        m_threadVector[i]->_Handle = std::thread(&CThreadPool::ThreadFunc,pNew);
    }

    // �ȴ������߳��������
lblfor:
    for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if ((*iter)->ifRunning == false) //���������֤�����߳���ȫ�����������Ա�֤�����̳߳��е��߳�����������
        {
            //��˵����û��������ȫ���߳�
            usleep(100 * 1000);  //��λ��΢��,����Ϊ1����=1000΢����� 100 *1000 = 100����
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

    // ���������߳�
    m_cond.notify_all();

    // �ȴ��߳̽���
    for (auto& threadItem : m_threadVector) {
        if (threadItem->_Handle.joinable()) {
            threadItem->_Handle.join();
        }
    }

    clearMsgRecvQueue();  // ������Ϣ����
    m_threadVector.clear();
    globallogger->clog(LogLevel::NOTICE, "CThreadPool::StopAll()�ɹ����أ��̳߳����߳�ȫ����������!" );
}

void CThreadPool::inMsgRecvQueueAndSignal(char* buf) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_MsgRecvQueue.push_back(buf);
        ++m_iRecvMsgQueueCount;
    }

    // ����һ���߳�����������
    Call();
}


void CThreadPool::Call() {
    if (m_iRunningThreadNum < m_iThreadNum) {
        m_cond.notify_one();  // ����һ���߳�
    }
    else {
        // ��������̶߳�æ��������Ҫ�����̳߳�
        globallogger->flog(LogLevel::ERROR, "CThreadPool::Call()�����̳߳��е�ǰ�����߳�����Ϊ0�����������̳߳�!" );
    }
}

int CThreadPool::getRecvMsgQueueCount() const
{
    return m_iRecvMsgQueueCount;
}

void CThreadPool::ThreadFunc(void* threadData)
{
    //����Ǿ�̬��Ա�������ǲ�����thisָ��ģ�
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool* pThreadPoolObj = pThread->_pThis;

    while (true) {
        std::unique_lock<std::mutex> lock(CThreadPool::m_mutex);

        // ����̳߳عرգ��˳�
        if (m_shutdown) {
            break;
        }

        // �ȴ���Ϣ�����̳߳عر��ź�
        while (pThreadPoolObj->m_MsgRecvQueue.empty() && !m_shutdown) {
            if (pThread->ifRunning == false)
                pThread->ifRunning = true;
            CThreadPool::m_cond.wait(lock);
        }

        // ����̳߳عر��ˣ��˳�
        if (m_shutdown ) {
            lock.unlock();
            break;
        }

        ++pThreadPoolObj->m_iRunningThreadNum;    //ԭ��+1����¼���ڸɻ���߳���������1������Ȼ�����Ҫ��ܶ�


        // ������Ϣ
        char* jobbuf = pThreadPoolObj->m_MsgRecvQueue.front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;
        if(jobbuf == nullptr)
            std::cout <<" xiaoxi : " << jobbuf << std::endl;
        lock.unlock();

        // ������յ�����Ϣ
        g_socket.threadRecvProcFunc(jobbuf); // ������Ϣ�ĺ���

        delete[] jobbuf;  // �ͷ���Ϣ�ڴ�
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