#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <chrono>
#include<list>

class CThreadPool
{
public:
    // ���캯��
    CThreadPool() : m_iRunningThreadNum(0), m_iRecvMsgQueueCount(0) 
    {

    };

    // ��������
    ~CThreadPool();

public:
    bool Create(int threadNum);                   // �����̳߳��е������߳�
    void StopAll();                               // ʹ�̳߳��е������߳��˳�

    void inMsgRecvQueueAndSignal(char* buf);  // �յ�һ��������Ϣ������Ϣ���У��������̳߳��е��߳�����������Ϣ
    void Call();                                  // ����һ���̳߳��е��߳�����������
    int getRecvMsgQueueCount() const;             // ��ȡ������Ϣ���д�С

private:
    static void ThreadFunc(void* threadData);      //���̵߳��̻߳ص�����
    void clearMsgRecvQueue();                     // ����������Ϣ����

private:
    struct ThreadItem
    {
        std::thread _Handle;                      // �̶߳���
        bool ifRunning;                            // ����߳��Ƿ���������
        CThreadPool* _pThis;
        ThreadItem(CThreadPool* pthis) : ifRunning(false),_pThis(pthis) {}
    };

private:
    static bool m_shutdown;                       // �߳��˳���־��false���˳���true�˳�
    int m_iThreadNum;                             // Ҫ�������߳�����

    std::atomic<int> m_iRunningThreadNum;         // ��ǰ���е��߳���
    std::chrono::time_point<std::chrono::steady_clock> m_iLastEmgTime; // �ϴν����¼���ʱ��
    std::vector<ThreadItem*> m_threadVector;  // �̳߳�����
    std::list<char*> m_MsgRecvQueue;      // ��Ϣ���ն���
    std::atomic<int> m_iRecvMsgQueueCount;                   // ������Ϣ���д�С
    static std::mutex m_mutex;                                 // �߳�ͬ��������
    static std::condition_variable m_cond;                      // �߳�ͬ����������
};


