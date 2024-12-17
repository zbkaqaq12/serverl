#pragma once

#include <vector>       //vector
#include <list>         //list
#include <sys/epoll.h>  //epoll
#include <sys/socket.h>
#include <pthread.h>    //多线程
#include <semaphore.h>  //信号量 
#include <atomic>       //c++11里的原子操作
#include <map>          //multimap
#include<mutex>
#include<memory>
#include<thread>

#include"comm.h"

#define LISTEN_BACKLOG 511  //已完成连接的队列
#define MAX_EVENTS     512  //epoll_wait一次最多接收这么多个事件

typedef struct listening_s   listening_t, * lplistening_t;
typedef struct connection_s  connection_t, * lpconnection_t;
typedef class  CSocket           CSocket;

typedef void (CSocket::* event_handler_pt)(lpconnection_t c); //定义成员函数指针

//--------------------------------------------
//一些专用结构定义放在这里，暂时不考虑放global.h里了

/**
 * @struct listening_s
 * @brief 监听端口相关的信息
 *
 * 该结构体用于保存监听套接字相关信息，包括端口号、套接字句柄和连接池中的连接指针。
 */
struct listening_s 
{
	int                       port;        //监听的端口号
	int                       fd;          //套接字句柄socket
	lpconnection_t        connection;  //连接池中的一个连接，注意这是个指针 
};


//以下三个结构是非常重要的三个结构，我们遵从官方nginx的写法；
/**
 * @struct connection_s
 * @brief 代表一个客户端与服务器的TCP连接
 *
 * 该结构体用于描述一个TCP连接的相关信息，包含连接的状态、数据缓冲区、事件处理等。
 */
struct connection_s
{
	connection_s();                                      //构造函数
	virtual ~connection_s();                             //析构函数
	void GetOneToUse();                                      //分配出去的时候初始化一些内容
	void PutOneToFree();                                     //回收回来的时候做一些事情


	int                       fd;                            //套接字句柄socket
	lplistening_t         listening;                     //如果这个链接被分配给了一个监听套接字，那么这个里边就指向监听套接字对应的那个lpngx_listening_t的内存首地址		

	//------------------------------------	
	//unsigned                  instance:1;                    //【位域】失效标志位：0：有效，1：失效【这个是官方nginx提供，到底有什么用，ngx_epoll_process_events()中详解】  
	uint64_t                  iCurrsequence;                 //我引入的一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包，具体怎么用，用到了再说
	struct sockaddr           s_sockaddr;                    //保存对方地址信息用的
	//char                      addr_text[100]; //地址的文本信息，100足够，一般其实如果是ipv4地址，255.255.255.255，其实只需要20字节就够

	//和读有关的标志-----------------------
	//uint8_t                   r_ready;        //读准备好标记【暂时没闹明白官方要怎么用，所以先注释掉】
	//uint8_t                   w_ready;        //写准备好标记

	event_handler_pt      rhandler;                       //读事件的相关处理方法
	event_handler_pt      whandler;                       //写事件的相关处理方法

	//和epoll事件有关
	uint32_t                  events;                         //和epoll事件有关  

	//和收包有关
	unsigned char             curStat;                        //当前收包的状态
	char                      dataHeadInfo[_DATA_BUFSIZE_];   //用于保存收到的数据的包头信息			
	char* precvbuf;                      //接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
	unsigned int              irecvlen;                       //要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码
	char* precvMemPointer;               //new出来的用于收包的内存首地址，释放用的

	std::mutex          logicPorcMutex;                 //逻辑处理相关的互斥量      

	//和发包有关
	std::atomic<int>          iThrowsendCount;                //发送消息，如果发送缓冲区满了，则需要通过epoll事件来驱动消息的继续发送，所以如果发送缓冲区满，则用这个变量标记
	char* psendMemPointer;               //发送完成后释放用的，整个数据的头指针，其实是 消息头 + 包头 + 包体
	char* psendbuf;                      //发送数据的缓冲区的头指针，开始 其实是包头+包体
	unsigned int              isendlen;                       //要发送多少数据

	//和回收有关
	time_t                    inRecyTime;                     //入到资源回收站里去的时间

	//和心跳包有关
	time_t                    lastPingTime;                   //上次ping的时间【上次发送心跳包的事件】

	//和网络安全有关	
	uint64_t                  FloodkickLastTime;              //Flood攻击上次收到包的时间
	int                       FloodAttackCount;               //Flood攻击在该时间内收到包的次数统计
	std::atomic<int>          iSendCount;                     //发送队列中有的数据条目数，若client只发不收，则可能造成此数过大，依据此数做出踢出处理 


	//--------------------------------------------------
	lpconnection_t        next;                           //这是个指针，指向下一个本类型对象，用于把空闲的连接池对象串起来构成一个单向链表，方便取用
};

/**
 * @struct _STRUC_MSG_HEADER
 * @brief 消息头结构体
 *
 * 该结构体用于保存消息的头部信息，并关联到对应的连接。
 */
typedef struct _STRUC_MSG_HEADER
{
	lpconnection_t pConn;         //记录对应的链接，注意这是个指针
	uint64_t           iCurrsequence; //收到数据包时记录对应连接的序号，将来能用于比较是否连接已经作废用
	//......其他以后扩展	
}STRUC_MSG_HEADER, * LPSTRUC_MSG_HEADER;

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
    CSocket();                 ///< 构造函数
    virtual ~CSocket();        ///< 析构函数
    virtual bool Initialize(); ///< 初始化函数
    virtual bool Initialize_subproc(); ///< 子进程初始化
    virtual void Shutdown_subproc(); ///< 子进程资源清理

    void printTDInfo(); ///< 打印线程数据

    virtual void threadRecvProcFunc(char* pMsgBuf); ///< 处理客户端请求的虚函数
    virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time); ///< 心跳包超时检测

    int epoll_init(); ///< 初始化 epoll 功能
    //int epoll_add_event(int fd, int readevent, int writevent, uint32_t otherflag, uint32_t eventtype, std::shared_ptr<connection_t> c); ///< 添加 epoll 事件
    int epoll_process_events(int timer); ///< 处理 epoll 事件
    int epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpconnection_t pConn);///<epoll操作事件

protected:
    void msgSend(char* psendbuf); ///< 发送数据
    void zdClosesocketProc(lpconnection_t p_Conn); ///< 关闭连接

private:
    void ReadConf(); ///< 读取配置
    bool open_listening_sockets(); ///< 打开监听套接字
    void close_listening_sockets(); ///< 关闭监听套接字
    bool setnonblocking(int sockfd); ///< 设置非阻塞模式

    //一些业务处理函数handler
    void event_accept(lpconnection_t oldc);                       //建立新连接
    void read_request_handler(lpconnection_t pConn);              //设置数据来时的读处理函数
    void write_request_handler(lpconnection_t pConn);             //设置数据发送时的写处理函数
    void close_connection(lpconnection_t pConn);                  //通用连接关闭函数，资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】

    ssize_t recvproc(lpconnection_t pConn, char* buff, ssize_t buflen); //接收从客户端来的数据专用函数
    void wait_request_handler_proc_p1(lpconnection_t pConn, bool& isflood);
    //包头收完整后的处理，我们称为包处理阶段1：写成函数，方便复用      
    void wait_request_handler_proc_plast(lpconnection_t pConn, bool& isflood);
    //收到一个完整包后的处理，放到一个函数中，方便调用	
    void clearMsgSendQueue();                                             //处理发送消息队列  

    ssize_t sendproc(lpconnection_t c, char* buff, ssize_t size);       //将数据发送到客户端 

    //获取对端信息相关                                              
    size_t sock_ntop(struct sockaddr* sa, int port, u_char* text, size_t len);  //根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度


    void initconnection(); ///< 初始化连接池
    void clearconnection(); ///< 清理连接池
    lpconnection_t get_connection(int isock); ///< 获取连接
    void free_connection(lpconnection_t pConn); ///< 归还连接
    void inRecyConnectQueue(lpconnection_t pConn);              ///<将要回收的连接放到一个队列中来

    void AddToTimerQueue(lpconnection_t pConn); ///< 添加到时间队列
    time_t GetEarliestTime(); ///< 获取最早的时间
    LPSTRUC_MSG_HEADER RemoveFirstTimer(); ///< 移除最早的定时器
    LPSTRUC_MSG_HEADER GetOverTimeTimer(time_t cur_time); ///< 获取超时的定时器
    void DeleteFromTimerQueue(lpconnection_t pConn); ///< 删除定时器
    void clearAllFromTimerQueue(); ///< 清理所有定时器

    bool TestFlood(lpconnection_t pConn); ///< 测试是否为 Flood 攻击

    static void* ServerSendQueueThread(void* threadData); ///< 发送消息线程
    static void* ServerRecyConnectionThread(void* threadData); ///< 回收连接线程
    static void* ServerTimerQueueMonitorThread(void* threadData); ///< 时间队列监控线程

protected:
    size_t m_iLenPkgHeader; ///< 数据包头长度
    size_t m_iLenMsgHeader; ///< 消息头长度

    int m_ifTimeOutKick; ///< 是否开启超时踢出功能
    int m_iWaitTime; ///< 等待时间

private:
    struct ThreadItem
    {
        std::thread _Handle; ///< 线程句柄
        CSocket* _pThis; ///< 指向 CSocket 的指针
        bool ifrunning; ///< 线程是否运行

        ThreadItem(CSocket* pthis) : _pThis(pthis), ifrunning(false) {}
    };

    int m_worker_connections; ///< 最大连接数
    int m_ListenPortCount; ///< 监听端口数量
    int m_epollhandle; ///< epoll 句柄

    std::list<lpconnection_t> m_connectionList; ///< 连接池
    std::list<lpconnection_t> m_freeconnectionList; ///< 空闲连接池
    std::atomic<int> m_total_connection_n; ///< 连接池总连接数
    std::atomic<int> m_free_connection_n; ///< 空闲连接数
    std::mutex m_connectionMutex; ///< 连接相关互斥量
    std::mutex m_recyconnqueueMutex; ///< 用于保护连接回收队列的互斥量
    std::list<lpconnection_t> m_recyconnectionList; ///< 存储待释放的连接
    std::atomic<int> m_totol_recyconnection_n; ///< 待回收连接的数量
    int m_RecyConnectionWaitTime; ///< 回收连接的等待时间，单位：秒

    
    std::vector<std::shared_ptr<listening_t>> m_ListenSocketList;  ///<监听套接字列表
    struct epoll_event m_events[MAX_EVENTS]; ///< epoll 事件列表

    std::list<char*> m_MsgSendQueue; ///< 发送消息队列
    std::atomic<int> m_iSendMsgQueueCount; ///< 消息队列大小

    std::vector<std::shared_ptr<ThreadItem>> m_threadVector; ///< 线程池
    std::mutex m_sendMessageQueueMutex; ///< 发送队列互斥量
    sem_t m_semEventSendQueue; ///< 发送队列信号量

    int m_ifkickTimeCount; ///< 是否开启踢人时钟
    std::mutex m_timequeueMutex; ///< 时间队列互斥量
    std::multimap<time_t, LPSTRUC_MSG_HEADER> m_timerQueuemap; ///< 时间队列
    size_t m_cur_size_; ///< 时间队列的尺寸
    time_t m_timer_value_; ///< 当前计时队列头部时间值

    std::atomic<int> m_onlineUserCount; ///< 在线用户数

    int m_floodAkEnable; ///< Flood 攻击检测是否启用
    unsigned int m_floodTimeInterval; ///< Flood 攻击检测时间间隔
    int m_floodKickCount; ///< Flood 攻击踢出次数

    time_t m_lastprintTime; ///< 上次打印统计信息的时间
    int m_iDiscardSendPkgCount; ///< 丢弃的发送数据包数量
};

