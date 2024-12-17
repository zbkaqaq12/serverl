#include"CSocket.h"
#include"global.h"
#include"CMemory.h"

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
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

//---------------------------------------------------------------
/**
 * @brief 构造函数，初始化连接对象
 * @details 初始化连接对象的成员变量，设定初始状态。`iCurrsequence` 设置为0，`fd` 设置为-1，表示尚未建立连接。
 */
connection_s::connection_s()//构造函数
{
    iCurrsequence = 0;
    //pthread_mutex_init(&logicPorcMutex, NULL); //互斥量初始化
}

/**
 * @brief 析构函数，释放资源
 * @details 析构函数中释放该连接对象资源（此函数目前仅释放了互斥量，注释掉了相关代码）。
 */
connection_s::~connection_s()//析构函数
{
    //pthread_mutex_destroy(&logicPorcMutex);    //互斥量释放
}

/**
 * @brief 初始化连接对象准备使用
 * @details 当一个连接被拿来使用时，调用此函数来初始化连接的状态和必要的成员变量，主要包括如下工作：
 * - 更新当前序列号 `iCurrsequence`
 * - 将 `fd` 设置为-1
 * - 初始化包头接收状态和接收缓冲区 `precvbuf`
 * - 初始化发送缓冲区指针 `psendMemPointer`
 * - 初始化发送队列计数器 `iThrowsendCount`
 * - 更新时间戳 `lastPingTime` 和防止Flood攻击相关计数
 */
void connection_s::GetOneToUse()
{
    ++iCurrsequence;

    fd = -1;                                         //初始先给-1
    curStat = _PKG_HD_INIT;                           //收包状态机的 初始状态，准备接收数据包头的状态
    precvbuf = dataHeadInfo;                          //收包要先收到这里来，因为要先收包头，所以收数据的buff直接就是dataHeadInfo
    irecvlen = sizeof(COMM_PKG_HEADER);               //这里指定收数据的长度，这里先要收包头这么长字节的数据

    precvMemPointer = NULL;                         //既然没new内存，那自然指向的内存地址先给NULL
    iThrowsendCount = 0;                            //原子的
    psendMemPointer = NULL;                         //发送数据头指针记录
    events = 0;                            //epoll事件先给0 
    lastPingTime = time(NULL);                   //上次ping的时间

    FloodkickLastTime = 0;                            //Flood攻击上次收到包的时间
    FloodAttackCount = 0;	                          //Flood攻击在该时间内收到包的次数统计
    iSendCount = 0;                            //发送队列中有的数据条目数，若client只发不收，则可能造成此数据的不断增长 
}

/**
 * @brief 回收连接并释放资源
 * @details 当连接被回收时，调用此函数来释放已分配资源，主要包括如下工作：
 * - 更新当前序列号 `iCurrsequence`
 * - 如果接收缓冲区内存被分配，则释放该内存
 * - 如果发送缓冲区内存被分配，则释放该内存
 * - 重置发送计数器 `iThrowsendCount`
 */
void connection_s::PutOneToFree()
{
    ++iCurrsequence;
    if (precvMemPointer != NULL)//如果该指针不为空，则表示有内存分配，需要释放内存
    {
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;
    }
    if (psendMemPointer != NULL) //如果该指针不为空，则表示有内存分配，需要释放内存
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;                              //设置回原值，这个感觉应该用原子操作         
}

//---------------------------------------------------------------
/**
 * @brief 初始化连接池
 * @details 负责初始化连接池。在初始化过程中：
 * - 为每个连接分配内存，并调用构造函数来初始化连接对象
 * - 初始化总连接列表和空闲列表
 * - 设置连接池的总连接数 `m_total_connection_n` 和可用连接数 `m_free_connection_n`
 */
void CSocket::initconnection()
{
    lpconnection_t p_Conn;
    CMemory* p_memory = CMemory::GetInstance();

    int ilenconnpool = sizeof(connection_t);
    for (int i = 0; i < m_worker_connections; ++i) //先创建这么多个连接，后续不够再增加
    {
        p_Conn = (lpconnection_t)p_memory->AllocMemory(ilenconnpool, true); //创建内存，因为这里涉及到内存分配new char，所以无法执行构造函数，所以这里使用
        //手工调用构造函数，因为AllocMemory里无法调用构造函数
        p_Conn = new(p_Conn) connection_t();  //定位new，释放则显式调用p_Conn->~ngx_connection_t();		
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);     //所有连接【不管是否空闲】都放在这个list
        m_freeconnectionList.push_back(p_Conn); //空闲连接会放在这个list
    } //end for
    m_free_connection_n = m_total_connection_n = m_connectionList.size(); //初始化都是空闲的
    return;
}

/**
 * @brief 回收并清理连接池中的所有连接
 * @details 清理连接池中的所有连接，手动调用每个连接的析构函数并释放内存。此函数在销毁连接池时调用。
 */
void CSocket::clearconnection()
{
    lpconnection_t p_Conn;
    CMemory* p_memory = CMemory::GetInstance();

    while (!m_connectionList.empty())
    {
        p_Conn = m_connectionList.front();
        m_connectionList.pop_front();
        p_Conn->~connection_t();     //手工调用析构函数
        p_memory->FreeMemory(p_Conn);
    }
}

/**
 * @brief 从连接池中获取一个空闲连接
 * @param isock 该连接的套接字
 * @return lpconnection_t 返回一个可用的连接对象
 * @details 如果连接池中有空闲连接，则从空闲连接列表中返回一个并初始化。如果没有空闲连接，则创建一个新的连接并返回。每个连接绑定一个TCP连接的套接字。
 */
lpconnection_t CSocket::get_connection(int isock)
{
    //因为可能有其他线程要访问m_freeconnectionList，m_connectionList【比如可能有专门的释放线程要释放/或者主线程要释放】之类的，所以应该临界一下
    // 使用 std::lock_guard 来自动加锁和解锁
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    if (!m_freeconnectionList.empty())
    {
        //有空闲的，自然是从空闲的中摘取
        lpconnection_t p_Conn = m_freeconnectionList.front(); //返回第一个元素但不检查元素存在与否
        m_freeconnectionList.pop_front();                         //移除第一个元素但不返回	
        p_Conn->GetOneToUse();
        --m_free_connection_n;
        p_Conn->fd = isock;
        return p_Conn;
    }

    //走到这里表示没有空闲的连接了，那就考虑重新创建一个连接
    CMemory* p_memory = CMemory::GetInstance();
    lpconnection_t p_Conn = (lpconnection_t)p_memory->AllocMemory(sizeof(connection_t), true);
    p_Conn = new(p_Conn) connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn); //入到总表中来，但不能入到空闲表中来，因为这个连接即将被使用
    ++m_total_connection_n;
    p_Conn->fd = isock;
    return p_Conn;
}

/**
 * @brief 将连接归还到连接池
 * @param pConn 需要归还的连接对象
 * @details 将指定连接归还到连接池，连接对象的相关资源会被释放，并将连接放入空闲列表中。
 *          使用互斥锁确保在多线程环境下安全地操作连接池。
 */
void CSocket::free_connection(lpconnection_t pConn)
{
    //因为有线程可能要访问连接池中的连接，所以在合理互斥也是必要的
    // 使用 std::lock_guard 来自动加锁和解锁
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    //首先明确一点，连接，所有连接全部都在m_connectionList里；
    pConn->PutOneToFree();

    //加到空闲连接列表中
    m_freeconnectionList.push_back(pConn);

    //空闲连接数+1
    ++m_free_connection_n;

    return;
}

/**
 * @brief 将连接放入回收队列，以后由专门线程处理
 * @param pConn 需要回收的连接对象
 * @details 如果连接对象没有被重复处理，则将连接对象放入回收队列 `m_recyconnectionList` 中，并记录回收时间。
 *          该队列中的连接将会以后由 `ServerRecyConnectionThread` 线程进行处理。使用互斥锁确保在多线程环境下安全地操作回收队列。
 */
void CSocket::inRecyConnectQueue(lpconnection_t pConn)
{
    std::list<lpconnection_t>::iterator pos;
    bool iffind = false;

    // 使用 std::lock_guard 来自动加锁和解锁
    std::lock_guard<std::mutex> lock(m_connectionMutex); //连接回收队列的互斥量，因为线程ServerRecyConnectionThread()也要用到这个回收列表

    //判断是否已经在队列中
    for (pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
    {
        if ((*pos) == pConn)
        {
            iffind = true;
            break;
        }
    }
    if (iffind == true) //找到了，不重复放入
    {
        //我们不希望同一个连接被重复放入队列
        return;
    }

    pConn->inRecyTime = time(NULL);        //记录回收时间
    ++pConn->iCurrsequence;
    m_recyconnectionList.push_back(pConn); //等待ServerRecyConnectionThread线程自会处理 
    ++m_totol_recyconnection_n;            //待释放连接队列大小+1
    --m_onlineUserCount;                   //连入用户数量-1
    return;
}

/**
 * @brief 连接回收清理线程
 * @param threadData 线程数据，包含线程回调信息
 * @return 返回空指针
 * @details 此函数作为一个线程循环运行，用于检查并处理已到期的连接。每200毫秒执行一次检查，
 *          如果发现到期的连接，则将这些连接从回收队列移除并归还到连接池中。
 *          如果程序正在退出，则将所有未回收的连接进行强制回收。
 */
void* CSocket::ServerRecyConnectionThread(void* threadData)
{
    ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
    CSocket* pSocketObj = pThread->_pThis;

    time_t currtime;
    //int err;
    std::list<lpconnection_t>::iterator pos, posend;
    lpconnection_t p_Conn;

    while (true)
    {
        // 每次休息200毫秒
        usleep(200 * 1000);  //单位是微秒，200毫秒

        // 处理连接回收
        if (pSocketObj->m_totol_recyconnection_n > 0)
        {
            currtime = time(NULL);

            // 使用 std::lock_guard 来自动加锁
            {
                std::lock_guard<std::mutex> lock(pSocketObj->m_recyconnqueueMutex);

                pos = pSocketObj->m_recyconnectionList.begin();
                posend = pSocketObj->m_recyconnectionList.end();

                while (pos != posend)
                {
                    p_Conn = (*pos);

                    // 判断连接是否已到回收时间
                    if ((p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime && g_stopEvent == 0)
                    {
                        ++pos;
                        continue; //没到释放时间，继续
                    }

                    //到释放时间了，且 iThrowsendCount == 0 才能释放
                    if (p_Conn->iThrowsendCount > 0)
                    {
                        globallogger->clog(LogLevel::ERROR, "CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount != 0，这个不该发生");
                    }

                    // 执行连接回收
                    --pSocketObj->m_totol_recyconnection_n;
                    pSocketObj->m_recyconnectionList.erase(pos); //删除已回收的连接
                    pSocketObj->free_connection(p_Conn); //归还连接

                    //迭代器已经失效，重新开始遍历
                    pos = pSocketObj->m_recyconnectionList.begin();
                    posend = pSocketObj->m_recyconnectionList.end();
                }
            }
        }

        //如果程序要退出
        if (g_stopEvent == 1)
        {
            //强制回收所有连接
            if (pSocketObj->m_totol_recyconnection_n > 0)
            {
                std::lock_guard<std::mutex> lock(pSocketObj->m_recyconnqueueMutex);

                pos = pSocketObj->m_recyconnectionList.begin();
                posend = pSocketObj->m_recyconnectionList.end();

                while (pos != posend)
                {
                    p_Conn = (*pos);
                    --pSocketObj->m_totol_recyconnection_n;
                    pSocketObj->m_recyconnectionList.erase(pos);
                    pSocketObj->free_connection(p_Conn);
                    pos = pSocketObj->m_recyconnectionList.begin();
                    posend = pSocketObj->m_recyconnectionList.end();
                }
            }
            break; //要退出了，跳出循环
        }
    }

    return nullptr;
}

/**
 * @brief 关闭并释放连接
 * @param pConn 需要关闭和释放的连接对象
 * @details 该函数用于关闭连接并释放相关资源。先将连接归还到连接池中，如果连接的文件描述符（fd）有效，
 *          则关闭连接的文件描述符并设置为无效（fd = -1）。
 */
void CSocket::close_connection(lpconnection_t pConn)
{
    //pConn->fd = -1; //官方nginx这么写，这么写有意义；    要处理代码遗留问题，后续要对这个socket做其他动作
    free_connection(pConn);
    if (pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd = -1;
    }
    return;
}