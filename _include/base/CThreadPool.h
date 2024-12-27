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
    // 构造函数
    CThreadPool() : m_iRunningThreadNum(0), m_iRecvMsgQueueCount(0) 
    {

    };

    // 析构函数
    ~CThreadPool();

public:
    bool Create(int threadNum);                   // 创建线程池中的所有线程
    void StopAll();                               // 使线程池中的所有线程退出

    void inMsgRecvQueueAndSignal(char* buf);      // 收到一个完整消息后入消息队列，并触发线程池中的线程来处理该消息
    void Call();                                  // 调用一个线程池中的线程来处理消息
    int getRecvMsgQueueCount() const;             // 获取接收消息队列大小

private:
    static void ThreadFunc(void* threadData);      // 新线程的线程回调函数
    void clearMsgRecvQueue();                     // 清理接收消息队列

private:
    struct ThreadItem
    {
        std::thread _Handle;                      // 线程对象
        bool ifRunning;                           // 标记线程是否正在运行中
        CThreadPool* _pThis;
        ThreadItem(CThreadPool* pthis) : ifRunning(false),_pThis(pthis) {}
    };

private:
    static bool m_shutdown;                       // 线程退出标志，false不退出，true退出
    int m_iThreadNum;                             // 要创建的线程数量

    std::atomic<int> m_iRunningThreadNum;         // 当前运行的线程数
    std::chrono::time_point<std::chrono::steady_clock> m_iLastEmgTime; // 上次接收事件的时间
    std::vector<ThreadItem*> m_threadVector;      // 线程池容器
    std::list<char*> m_MsgRecvQueue;             // 消息接收队列
    std::atomic<int> m_iRecvMsgQueueCount;       // 接收消息队列大小
    static std::mutex m_mutex;                    // 线程同步互斥量
    static std::condition_variable m_cond;        // 线程同步条件变量
};


