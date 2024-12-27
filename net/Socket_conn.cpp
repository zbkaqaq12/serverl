#include "CSocket.h"
#include "global.h"
#include "CMemory.h"
#include "Logger.h"
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
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

// HttpContext构造函数
connection_s::HttpContext::HttpContext() : parser(std::make_unique<http::HttpParser>()),
                                           keepAlive(false),
                                           lastActivityTime(0),
                                           state(ConnectionState::WAITING)
{
}

// HttpContext重置函数
void connection_s::HttpContext::reset()
{
    parser = std::make_unique<http::HttpParser>();
    request = http::HttpRequest();
    response = http::HttpResponse();
    keepAlive = false;
    lastActivityTime = time(NULL);
    state = ConnectionState::WAITING;
}

// Buffer构造函数
connection_s::Buffer::Buffer(size_t initial_size) : buffer_(initial_size),
                                                    readIndex_(0),
                                                    writeIndex_(0)
{
}

// Buffer追加数据
void connection_s::Buffer::append(const char *data, size_t len)
{
    if (writableBytes() < len)
    {
        makeSpace(len);
    }
    std::copy(data, data + len, beginWrite());
    hasWritten(len);
}

// Buffer检索数据
void connection_s::Buffer::retrieve(size_t len)
{
    if (len < readableBytes())
    {
        readIndex_ += len;
    }
    else
    {
        retrieveAll();
    }
}

// Buffer清空
void connection_s::Buffer::retrieveAll()
{
    readIndex_ = 0;
    writeIndex_ = 0;
}

// Buffer扩容
void connection_s::Buffer::makeSpace(size_t len)
{
    if (writableBytes() + readIndex_ < len)
    {
        buffer_.resize(writeIndex_ + len);
    }
    else
    {
        size_t readable = readableBytes();
        std::copy(begin() + readIndex_, begin() + writeIndex_, begin());
        readIndex_ = 0;
        writeIndex_ = readable;
    }
}

// connection_s构造函数
connection_s::connection_s() : fd(-1),
                               listening(nullptr),
                               events(0),
                               next(nullptr),
                               rhandler(nullptr),
                               whandler(nullptr),
                               inRecyTime(0),
                               iSendCount(0),
                               iThrowsendCount(0),
                               psendbuf(nullptr),
                               isendlen(0),
                               psendMemPointer(nullptr),
                               readBuffer(16384), // 16KB初始大小
                               writeBuffer(16384) // 16KB初始大小
{
    // 初始化HTTP上下文
    httpCtx = std::make_unique<HttpContext>();

    // 初始化安全控制
    security = std::make_unique<Security>();
}

// connection_s析构函数
connection_s::~connection_s()
{
    // 关闭socket
    if (fd != -1)
    {
        close(fd);
        fd = -1;
    }

    // 释放发送缓冲区
    if (psendbuf)
    {
        CMemory::GetInstance()->FreeMemory(psendbuf);
        psendbuf = nullptr;
    }

    // 释放发送内存指针
    if (psendMemPointer)
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = nullptr;
    }
}

// 初始化连接以供使用
void connection_s::GetOneToUse()
{
    // 重置安全状态
    if (security)
    {
        security->reset();
    }
    else
    {
        security = std::make_unique<Security>();
    }

    fd = -1;
    listening = nullptr;
    events = 0;
    rhandler = nullptr;
    whandler = nullptr;
    inRecyTime = 0;

    // 重置发送相关
    if (psendbuf)
    {
        CMemory::GetInstance()->FreeMemory(psendbuf);
        psendbuf = nullptr;
    }
    if (psendMemPointer)
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = nullptr;
    }
    isendlen = 0;
    iSendCount = 0;
    iThrowsendCount = 0;

    // 重置缓冲区
    readBuffer = Buffer(16384);
    writeBuffer = Buffer(16384);

    // 重置HTTP上下文
    if (httpCtx)
    {
        httpCtx->reset();
    }
    else
    {
        httpCtx = std::make_unique<HttpContext>();
    }

    // 重置安全控制
    if (security)
    {
        security->reset();
    }
    else
    {
        security = std::make_unique<Security>();
    }
}

// 将连接放回空闲池
void connection_s::PutOneToFree()
{
    // 重置安全状态
    if (security)
    {
        security->reset();
    }

    // 关闭socket
    if (fd != -1)
    {
        close(fd);
        fd = -1;
    }

    // 释放发送缓冲区
    if (psendbuf)
    {
        CMemory::GetInstance()->FreeMemory(psendbuf);
        psendbuf = nullptr;
        isendlen = 0;
    }

    // 释放发送内存指针
    if (psendMemPointer)
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = nullptr;
    }

    // 重置计数器和标志
    iSendCount = 0;
    iThrowsendCount = 0;
    events = 0;
    listening = nullptr;
    rhandler = nullptr;
    whandler = nullptr;
    inRecyTime = 0;

    // 重置HTTP上下文
    if (httpCtx)
    {
        httpCtx->reset();
    }

    // 重置安全控制
    if (security)
    {
        security->reset();
    }

    // 清空缓冲区
    readBuffer.retrieveAll();
    writeBuffer.retrieveAll();
}

// 以下是CSocket类的连接池管理函数实现

void CSocket::initconnection()
{
    auto logger = Logger::GetInstance();

    lpconnection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    int ilenconnpool = sizeof(connection_t);
    for (int i = 0; i < m_worker_connections; ++i)
    {
        // 创建连接对象
        p_Conn = (lpconnection_t)p_memory->AllocMemory(ilenconnpool, true);
        // 使用定位new调用构造函数
        p_Conn = new (p_Conn) connection_t();
        p_Conn->GetOneToUse();

        // 添加到连接列表
        m_connectionList.push_back(p_Conn);     // 所有连接都放在这个list
        m_freeconnectionList.push_back(p_Conn); // 空闲连接放在这个list
    }

    // 初始化连接计数
    m_free_connection_n = m_total_connection_n = m_connectionList.size();

    logger->logSystem(LogLevel::INFO,
                      "Connection pool initialized with %d connections", m_worker_connections);
}

void CSocket::clearconnection()
{
    lpconnection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    while (!m_connectionList.empty())
    {
        p_Conn = m_connectionList.front();
        m_connectionList.pop_front();

        // 手动调用析构函数并释放内存
        p_Conn->~connection_t();
        p_memory->FreeMemory(p_Conn);
    }
}

lpconnection_t CSocket::get_connection(int isock)
{
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    // 如果有空闲连接，从空闲列表中获取
    if (!m_freeconnectionList.empty())
    {
        lpconnection_t p_Conn = m_freeconnectionList.front();
        m_freeconnectionList.pop_front();
        p_Conn->GetOneToUse();
        --m_free_connection_n;
        p_Conn->fd = isock;
        return p_Conn;
    }

    // 如果没有空闲连接，创建新的连接
    CMemory *p_memory = CMemory::GetInstance();
    lpconnection_t p_Conn = (lpconnection_t)p_memory->AllocMemory(sizeof(connection_t), true);
    p_Conn = new (p_Conn) connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn);
    ++m_total_connection_n;
    p_Conn->fd = isock;
    return p_Conn;
}

void CSocket::free_connection(lpconnection_t pConn)
{
    std::lock_guard<std::mutex> lock(m_connectionMutex);

    // 重置连接状态
    pConn->PutOneToFree();

    // 加入空闲列表
    m_freeconnectionList.push_back(pConn);
    ++m_free_connection_n;
}

void CSocket::inRecyConnectQueue(lpconnection_t pConn)
{
    auto logger = Logger::GetInstance();

    std::lock_guard<std::mutex> lock(m_recyconnqueueMutex);

    // 检查是否已在回收队列中
    for (auto pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
    {
        if (*pos == pConn)
        {
            return; // 已在队列中，不重复添加
        }
    }

    // 记录回收时间
    pConn->inRecyTime = time(NULL);
    pConn->security->incrementSequence();

    // 添加到回收队列
    m_recyconnectionList.push_back(pConn);
    ++m_totol_recyconnection_n;
    --m_onlineUserCount;

    int current_recycle_count = m_totol_recyconnection_n.load();
    logger->logSystem(LogLevel::DEBUG,
                      "Connection added to recycle queue, total: %d", current_recycle_count);
}

void *CSocket::ServerRecyConnectionThread(void *threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem *>(threadData);
    CSocket *pSocketObj = pThread->_pThis;
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Recycle connection thread started");

    while (true)
    {
        // 每200ms检查一次
        usleep(200 * 1000);

        // 处理回收队列中的连接
        if (pSocketObj->m_totol_recyconnection_n > 0)
        {
            time_t currtime = time(NULL);
            std::lock_guard<std::mutex> lock(pSocketObj->m_recyconnqueueMutex);

            auto pos = pSocketObj->m_recyconnectionList.begin();
            while (pos != pSocketObj->m_recyconnectionList.end())
            {
                lpconnection_t p_Conn = *pos;

                // 检查是否到达回收时间
                if ((p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime && !g_stopEvent)
                {
                    ++pos;
                    continue;
                }

                // 回收连接
                --pSocketObj->m_totol_recyconnection_n;
                pos = pSocketObj->m_recyconnectionList.erase(pos);
                pSocketObj->free_connection(p_Conn);
            }
        }

        // 检查是否需要退出
        if (g_stopEvent)
        {
            // 强制回收所有连接
            if (pSocketObj->m_totol_recyconnection_n > 0)
            {
                std::lock_guard<std::mutex> lock(pSocketObj->m_recyconnqueueMutex);

                while (!pSocketObj->m_recyconnectionList.empty())
                {
                    lpconnection_t p_Conn = pSocketObj->m_recyconnectionList.front();
                    pSocketObj->m_recyconnectionList.pop_front();
                    --pSocketObj->m_totol_recyconnection_n;
                    pSocketObj->free_connection(p_Conn);
                }
            }
            break;
        }
    }

    return nullptr;
}

void CSocket::close_connection(lpconnection_t pConn)
{
    free_connection(pConn);
    if (pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd = -1;
    }
    return;
}