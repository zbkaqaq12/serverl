#pragma once

#include <vector>      //vector
#include <list>        //list
#include <sys/epoll.h> //epoll
#include <sys/socket.h>
#include <pthread.h>   //多线程
#include <semaphore.h> //信号量
#include <atomic>      //c++11里的原子操作
#include <map>         //multimap
#include <mutex>
#include <memory>
#include <thread>

#include "http/parser.h"
#include "response.h"
#include "request.h"
#include "Security.h"

#define LISTEN_BACKLOG 511 // 已完成连接的队列
#define MAX_EVENTS 512     // epoll_wait一次最多接收这么多个事件

typedef struct listening_s listening_t, *lplistening_t;
typedef struct connection_s connection_t, *lpconnection_t;
typedef class CSocket CSocket;

typedef void (CSocket::*event_handler_pt)(lpconnection_t c); // 定义成员函数指针

//--------------------------------------------
// 一些专用结构定义放在这里，暂时不考虑放global.h里了

/**
 * @struct listening_s
 * @brief 监听端口相关的信息
 *
 * 该结构体用于保存监听套接字相关信息，包括端口号、套接字句柄和连接池中的连接指针。
 */
struct listening_s
{
    int port;                  // 监听的端口号
    int fd;                    // 套接字句柄socket
    lpconnection_t connection; // 连接池中的一个连接，注意这是个指针
};

// 以下三个结构是非常重要的三个结构，我们遵从官方nginx的写法；
/**
 * @struct connection_s
 * @brief 代表一个客户端与服务器的TCP连接
 *
 * 该结构体用于描述一个TCP连接的相关信息，包含连接的状态、数据缓冲区、事件处理等。
 */
struct connection_s
{
    // 构造和析构
    connection_s();
    virtual ~connection_s();

    // 连接池管理
    void GetOneToUse();
    void PutOneToFree();
    lpconnection_t next;

    // 基础网络信息
    int fd;                              // socket文件描述符
    lplistening_t listening;             // 监听socket信息
    struct sockaddr s_sockaddr;          // 地址信息
    uint32_t events;                     // epoll事件标志
    std::atomic<int> iThrowsendCount{0}; // 发送计数

    // 事件处理
    event_handler_pt rhandler; // 读事件的相关处理方法
    event_handler_pt whandler; // 写事件的相关处理方法

    // 同步和状态
    std::mutex logicProcMutex; // 互斥锁
    time_t inRecyTime;         // 回收时间

    // 发送相关
    std::atomic<int> iSendCount{0};      // 发送队列中的数据条目数
    char *psendbuf{nullptr};             // 发送数据的缓冲区指针
    size_t isendlen{0};                  // 要发送的数据长度
    char *psendMemPointer{nullptr};

    // HTTP处理相关
    struct HttpContext
    {
        std::unique_ptr<http::HttpParser> parser; // HTTP解析器
        http::HttpRequest request;                // 当前请求
        http::HttpResponse response;              // 当前响应
        bool keepAlive{false};                    // 保持连接
        time_t lastActivityTime;

        // 连接生命周期状态
        enum class ConnectionState
        {
            WAITING,    // 等待新请求
            READING,    // 正在读取请求
            PROCESSING, // 正在处理请求
            WRITING,    // 正在发送响应
            CLOSING     // 准备关闭
        } state{ConnectionState::WAITING};

        HttpContext();
        void reset();
    };
    std::unique_ptr<HttpContext> httpCtx;

    // 缓冲区管理
    class Buffer
    {
    public:
        Buffer(size_t initial_size = 16384); // 16KB 初始大小

        // 基本操作
        void append(const char *data, size_t len);
        void retrieve(size_t len); // 从缓冲区中移除已经读取的数据
        void retrieveAll();        // 清空缓冲区

        // 读写接口
        const char *peek() const { return begin() + readIndex_; }
        void hasWritten(size_t len) { writeIndex_ += len; }
        char *beginWrite() { return begin() + writeIndex_; }

        // 容量管理
        size_t readableBytes() const { return writeIndex_ - readIndex_; }
        size_t writableBytes() const { return buffer_.size() - writeIndex_; }

    private:
        std::vector<char> buffer_;
        size_t readIndex_{0};
        size_t writeIndex_{0};

        char *begin() { return buffer_.data(); }
        const char *begin() const { return buffer_.data(); }
        void makeSpace(size_t len);
    };

    Buffer readBuffer;  // 读缓冲区
    Buffer writeBuffer; // 写缓冲区

    // 安全相关（可选）
    std::unique_ptr<Security> security; // 安全控制
  };

/**
 * @class CSocket
 * @brief 用于处理套接字连接和网络事件的类
 *
 * 本类负责初始化和管理与客户端的连接，包括接收和发送数据，处理心跳包，管理 epoll 事件等。
 * 还包括连接池管理、消息队列处理、网络安全措施（如 Flood 攻击检测）等功能。
 */
class CSocket
{
public:
    CSocket();                         ///< 构造函数
    virtual ~CSocket();                ///< 析构函数
    virtual bool Initialize();         ///< 初始化函数
    virtual bool Initialize_subproc(); ///< 子进程初始化
    virtual void Shutdown_subproc();   ///< 子进程资源清理

    void printTDInfo(); ///< 打印线程数据

    int epoll_init();                                                                                    ///< 初始化 epoll 功能
    int epoll_process_events(int timer);                                                                 ///< 处理 epoll 事件
    int epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpconnection_t pConn); ///< epoll操作事件

protected:
    bool msgSend(lpconnection_t pConn, const char* data, size_t len);  // 发送HTTP响应数据
    void zdClosesocketProc(lpconnection_t p_Conn);                     // 关闭连接

private:
    void ReadConf();                 ///< 读取配置
    bool open_listening_sockets();   ///< 打开监听套接字
    void close_listening_sockets();  ///< 关闭监听套接字
    bool setnonblocking(int sockfd); ///< 设置非阻塞模式

    // HTTP 处理相关
    bool initHttpHandlers();
    void setupDefaultHandlers();
    void setupMimeTypes();
    void handleHttpRequest(lpconnection_t c);

    // 一些业务处理函数handler
    void event_accept(lpconnection_t oldc);           // 建立新连接
    void read_request_handler(lpconnection_t pConn);  // 设置数据来时的读处理函数
    void write_request_handler(lpconnection_t pConn); // 设置数据发送时的写处理函数
    bool processHttpRequest(lpconnection_t pConn);    // 处理HTTP请求

    // 连接管理
    void initconnection();
    void clearconnection();
    lpconnection_t get_connection(int isock);
    void free_connection(lpconnection_t pConn);
    void close_connection(lpconnection_t pConn); // 通用连接关闭函数，资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
    void inRecyConnectQueue(lpconnection_t pConn);

    // 数据收发
    ssize_t recvproc(lpconnection_t pConn, char *buff, ssize_t buflen); // 接收从客户端来的数据专用函数
    ssize_t sendproc(lpconnection_t c, char *buff, ssize_t size);       // 将数据发送到客户端

    // 定时器管理
    struct Timer {
        enum class Type {
            IDLE,           // 空闲连接定时器
            KEEP_ALIVE,     // keep-alive定时器
            REQUEST         // 请求处理定时器
        };

        time_t expireTime;        // 过期时间
        Type type;                // 定时器类型
        lpconnection_t conn;      // 关联的连接
        uint64_t sequence;        // 连接序号，用于验证连接是否有效

        Timer(time_t expire, Type t, lpconnection_t c, uint64_t seq) 
            : expireTime(expire), type(t), conn(c), sequence(seq) {}
    };

    void AddTimer(lpconnection_t pConn, Timer::Type type);
    void DeleteTimer(lpconnection_t pConn);
    void ProcessTimers();

    // 工作线程
    static void *ServerSendQueueThread(void *threadData);         ///< 发送消息线程
    static void *ServerRecyConnectionThread(void *threadData);    ///< 回收连接线程
    static void *ServerTimerQueueMonitorThread(void *threadData); ///< 时间队列监控线程

    //------------------------------------------------------------------
    void wait_request_handler_proc_p1(lpconnection_t pConn, bool &isflood);
    // 包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用
    void wait_request_handler_proc_plast(lpconnection_t pConn, bool &isflood);
    // 收到一个完整包后的处理，放到一个函数中，方便调用
    void clearMsgSendQueue(); // 处理发送消息队列

    // 获取对端信息相关
    size_t sock_ntop(struct sockaddr *sa, int port, u_char *text, size_t len); // 根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度

    //void initconnection();                         ///< 初始化连接池
    //void clearconnection();                        ///< 清理连接池
    //lpconnection_t get_connection(int isock);      ///< 获取连接
    //void free_connection(lpconnection_t pConn);    ///< 归还连接
    //void inRecyConnectQueue(lpconnection_t pConn); ///< 将要回收的连接放到一个队列中来

    void AddToTimerQueue(lpconnection_t pConn);           ///< 添加到时间队列
    //time_t GetEarliestTime();                             ///< 获取最早的时间
    //Timer RemoveFirstTimer();                ///< 移除最早的定时器
    Timer GetOverTimeTimer(time_t cur_time); ///< 获取超时的定时器
    void DeleteFromTimerQueue(lpconnection_t pConn);      ///< 删除定时器
    void clearAllFromTimerQueue();                        ///< 清理所有定时器

protected:
    int m_ifTimeOutKick; ///< 是否开启超时踢出功能
    int m_iWaitTime;     ///< 等待时间

private:
    int m_worker_connections; ///< 最大连接数
    int m_ListenPortCount;    ///< 监听端口数量
    int m_epollhandle;        ///< epoll 句柄

    // 连接管理
    std::list<lpconnection_t> m_connectionList;     ///< 连接池
    std::list<lpconnection_t> m_freeconnectionList; ///< 空闲连接池
    std::atomic<int> m_total_connection_n;          ///< 连接池总连接数
    std::atomic<int> m_free_connection_n;           ///< 空闲连接数
    std::mutex m_connectionMutex;                   ///< 连接相关互斥量

    // 连接回收
    std::mutex m_recyconnqueueMutex;                ///< 用于保护连接回收队列的互斥量
    std::list<lpconnection_t> m_recyconnectionList; ///< 存储待释放的连接
    std::atomic<int> m_totol_recyconnection_n;      ///< 待回收连接的数量
    int m_RecyConnectionWaitTime;                   ///< 回收连接的等待时间，单位：秒

    // 监听套接字
    std::vector<std::shared_ptr<listening_t>> m_ListenSocketList; ///< 监听套接字列表
    struct epoll_event m_events[MAX_EVENTS];                      ///< epoll 事件列表

    // 线程管理
    struct ThreadItem
    {
        std::thread _Handle; ///< 线程句柄
        CSocket *_pThis;     ///< 指向 CSocket 的指针
        bool ifrunning;      ///< 线程是否运行

        ThreadItem(CSocket *pthis) : _pThis(pthis), ifrunning(false) {}
    };
    std::vector<std::shared_ptr<ThreadItem>> m_threadVector; ///< 线程池

    // 定时器管理
    std::mutex m_timequeueMutex;               ///< 时间队列互斥量
    std::multimap<time_t, Timer> m_timerQueue; ///< 时间队列

    // HTTP 处理
    std::unordered_map<std::string, std::function<void(http::HttpRequest &, http::HttpResponse &)>> httpHandlers_;
    std::unordered_map<std::string, std::string> mimeTypes_;

    // 安全控制
    std::atomic<int> m_onlineUserCount; ///< 在线用户数
    int m_floodAkEnable;                ///< Flood 攻击检测是否启用
    unsigned int m_floodTimeInterval;   ///< Flood 攻击检测时间间隔
    int m_floodKickCount;               ///< Flood 攻击踢出次数

    // 统计信息
    time_t m_lastprintTime;     ///< 上次打印统计信息的时间
    int m_iDiscardSendPkgCount; ///< 丢弃的发送数据包数量

    //------------------------------------------------------------------
    std::list<char *> m_MsgSendQueue;      ///< 发送消息队列
    std::atomic<int> m_iSendMsgQueueCount; ///< 消息队列大小

    std::mutex m_sendMessageQueueMutex; ///< 发送队列互斥量
    sem_t m_semEventSendQueue;          ///< 发送队列信号量

    int m_ifkickTimeCount; ///< 是否开启踢人时钟

    size_t m_cur_size_;    ///< 时间队列的尺寸
    time_t m_timer_value_; ///< 当前计时队列头部时间值

};
