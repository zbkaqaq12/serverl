#include "CSocket.h"
#include "global.h"
#include"macro.h"
#include"CMemory.h"

#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <iostream>
#include <stdlib.h>
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

/**
 * @brief 构造函数.
 * 
 */
CSocket::CSocket()
{
	// 配置相关
	m_worker_connections = 1;      ///< epoll连接最大项数
	m_ListenPortCount = 1;         ///< 监听一个端口
	m_RecyConnectionWaitTime = 60; ///< 等待这么些秒后才回收连接

	// epoll相关
	m_epollhandle = -1;            ///< epoll句柄

	// 网络通讯相关常用变量
	m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);    ///< 包头长度
	m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);  ///< 消息头长度

	// 多线程相关
	m_iSendMsgQueueCount = 0;      ///< 发消息队列大小
	m_totol_recyconnection_n = 0; ///< 待释放连接队列大小
	m_cur_size_ = 0;               ///< 当前计时队列尺寸
	m_timer_value_ = 0;            ///< 当前计时队列头部时间值
	m_iDiscardSendPkgCount = 0;    ///< 丢弃的发送数据包数量

	// 在线用户相关
	m_onlineUserCount = 0;         ///< 在线用户数量统计
	m_lastprintTime = 0;           ///< 上次打印统计信息的时间
}

CSocket::~CSocket()
{


}

/**
 * @brief 初始化函数.
 * 
 * @return 成功返回true，失败返回false
 */
bool CSocket::Initialize()
{
	ReadConf();  //读配置项
	bool reco = open_listening_sockets();  //打开监听端口
	return reco;
}

/**
 * @brief 子进程初始化函数，用于初始化互斥量、信号量和线程。
 *
 * 该函数在子进程中调用，初始化线程、互斥量、信号量等资源。
 * @return true 如果初始化成功，false 否则
 */
bool CSocket::Initialize_subproc()
{

	if (sem_init(&m_semEventSendQueue, 0, 0) == -1)
	{
		globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize_subproc() 中信号量初始化失败.");
		return false;
	}
	

	// 创建线程（发送数据）
	try {
		auto pSendQueue = std::make_shared<ThreadItem>(this);
		m_threadVector.push_back(pSendQueue);
		pSendQueue->_Handle = std::thread(ServerSendQueueThread, pSendQueue.get());
	}
	catch (...) {
		globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize_subproc() 中创建 ServerSendQueueThread 失败.");
		return false;
	}

	// 创建线程（回收连接）
	try {
		auto pRecyconn = std::make_shared<ThreadItem>(this);
		m_threadVector.push_back(pRecyconn);
		pRecyconn->_Handle = std::thread(ServerRecyConnectionThread, pRecyconn.get());
	}
	catch (...) {
		globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize_subproc() 中创建 ServerRecyConnectionThread 失败.");
		return false;
	}

	// 如果启用踢人时钟，则创建监控线程
	if (m_ifkickTimeCount == 1) {
		try {
			auto pTimemonitor = std::make_shared<ThreadItem>(this);
			m_threadVector.push_back(pTimemonitor);
			pTimemonitor->_Handle = std::thread(ServerTimerQueueMonitorThread, pTimemonitor.get());
		}
		catch (...) {
			globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize_subproc() 中创建 ServerTimerQueueMonitorThread 失败.");
			return false;
		}
	}

	return true;
}

/**
 * @brief 关闭退出函数 (子进程中执行)
 *
 * 该函数用于在子进程中执行关闭操作，包括停止线程、清理队列及销毁信号量等。
 *
 * @note 系统应尝试通过设置 g_stopEvent = 1 来开始停止整个项目。
 * @note 对于使用信号量的部分，可能还需要调用 sem_post 来继续处理队列中的任务。
 */
void CSocket::Shutdown_subproc()
{
	//(1)把干活的线程停止掉，注意 系统应该尝试通过设置 g_stopEvent = 1来 开始让整个项目停止
   //(2)用到信号量的，可能还需要调用一下sem_post
	if (sem_post(&m_semEventSendQueue) == -1)  //让ServerSendQueueThread()流程走下来干活
	{
		globallogger->clog(LogLevel::ERROR, "CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
	}

	for (auto iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		(*iter)->_Handle.join(); //等待一个线程终止
	}

	//(3)队列相关
	clearMsgSendQueue();
	clearconnection();
	clearAllFromTimerQueue();

	//(4)多线程相关    
	sem_destroy(&m_semEventSendQueue);
}

/**
 * @brief 清理TCP发送消息队列
 *
 * 该函数清理TCP消息发送队列，释放队列中所有消息所占用的内存。
 */
void CSocket::clearMsgSendQueue()
{
	char* sTmpMempoint;
	CMemory* p_memory = CMemory::GetInstance();

	while (!m_MsgSendQueue.empty())
	{
		sTmpMempoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front();
		p_memory->FreeMemory(sTmpMempoint);
	}
}

/**
 * @brief 打印线程池和连接池的相关信息
 *
 * 该函数每10秒打印一次当前的在线人数、连接池状态、消息队列情况等信息，
 * 用于监控系统运行状况。若接收队列中的消息条目过大，打印警告日志。
 */
void CSocket::printTDInfo()
{
	time_t currtime = time(NULL);
	if ((currtime - m_lastprintTime) > 10)
	{
		//超过10秒我们打印一次
		int tmprmqc = g_threadpool.getRecvMsgQueueCount(); //收消息队列

		m_lastprintTime = currtime;
		int tmpoLUC = m_onlineUserCount;    //atomic做个中转，直接打印atomic类型报错；
		int tmpsmqc = m_iSendMsgQueueCount; //atomic做个中转，直接打印atomic类型报错；
		std::cout << "------------------------------------begin--------------------------------------" << std::endl;
		std::cout << "当前在线人数/总人数(" << tmpoLUC << "/" << m_worker_connections << ")." << std::endl;
		std::cout << "连接池中空闲连接/总连接/要释放的连接(" << m_freeconnectionList.size() << "/"
			<< m_connectionList.size() << "/" << m_recyconnectionList.size() << ")." << std::endl;
		std::cout << "当前时间队列大小(" << m_timerQueuemap.size() << ")." << std::endl;
		std::cout << "当前收消息队列/发消息队列大小分别为(" << tmprmqc << "/" << tmpsmqc << ")，丢弃的待发送数据包数量为" << m_iDiscardSendPkgCount << "." << std::endl;
		if (tmprmqc > 100000)
		{
			//接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
			globallogger->clog(LogLevel::NOTICE, "接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！", tmprmqc);
		}
		std::cout << ("-------------------------------------end---------------------------------------") << std::endl;
	}
	return;
}

//--------------------------------------------------------------------
/**
 * @brief 初始化 epoll 功能，子进程中进行。
 *
 * 该函数创建 epoll 对象并初始化连接池，遍历所有监听 socket，并为每个监听 socket 添加连接池中的连接。
 * 还会设置监听 socket 的读事件处理方法并将其添加到 epoll 中进行事件监听。
 *
 * @return int 如果成功初始化，返回 1；否则在出错时直接退出程序。
 */
int CSocket::epoll_init()
{
	//(1)很多内核版本不处理epoll_create的参数，只要该参数>0即可
	//创建一个epoll对象，创建了一个红黑树，还创建了一个双向链表
	
	m_epollhandle = epoll_create(m_worker_connections); //直接以epoll连接的最大项数为参数，肯定是>0的；
	if (m_epollhandle == -1)
	{
		globallogger->flog(LogLevel::ERROR, "CSocekt::ngx_epoll_init()中epoll_create()失败.");
		exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
	}

	 //(2)创建连接池【数组】、创建出来，这个东西后续用于处理所有客户端的连接
	initconnection();
	
	//(3)遍历所有监听socket【监听端口】，我们为每个监听socket增加一个 连接池中的连接【说白了就是让一个socket和一个内存绑定，以方便记录该sokcet相关的数据、状态等等】
	for (auto& pos : CSocket::m_ListenSocketList)
	{
		lpconnection_t p_Conn = get_connection(pos->fd);
		if (p_Conn == nullptr)
		{
			//这是致命问题，刚开始怎么可能连接池就为空呢？
			globallogger->flog(LogLevel::ERROR, "CSocekt::epoll_init()中get_connection()失败.");
			exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
		}
		p_Conn->listening = pos.get();//连接对象 和监听对象关联，方便通过连接对象找监听对象
		pos->connection = p_Conn;  //监听对象 和连接对象关联，方便通过监听对象找连接对象

		//对监听端口的读事件设置处理方法，因为监听端口是用来等对方连接的发送三路握手的，所以监听端口关心的就是读事件
		p_Conn->rhandler = &CSocket::event_accept;

		//往监听socket上增加监听事件，从而开始让监听端口履行其职责【如果不加这行，虽然端口能连上，但不会触发epoll_process_events()里边的epoll_wait()往下走】
		//if (epoll_add_event(pos->fd,       //socekt句柄
		//	1, 0,             //读，写【只关心读事件，所以参数2：readevent=1,而参数3：writeevent=0】
		//	0,               //其他补充标记
		//	EPOLL_CTL_ADD,   //事件类型【增加，还有删除/修改】
		//	c                //连接池中的连接 
		//) == -1)
		//{
		//	exit(2); //有问题，直接退出，日志 已经写过了
		//}
		if (epoll_oper_event(
			pos->fd,         //socekt句柄
			EPOLL_CTL_ADD,      //事件类型，这里是增加
			EPOLLIN | EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭
			0,                  //对于事件类型为增加的，不需要这个参数
			p_Conn              //连接池中的连接 
		) == -1)
		{
			exit(2); //有问题，直接退出，日志 已经写过了
		}

	}
	
	return 1;
}


/**
 * @brief 处理 epoll 事件。
 *
 * 该函数用于从 epoll 中获取事件，并根据事件类型进行处理。事件包括读事件和写事件。函数会根据传入的阻塞时间（`timer`）等待事件的到来。如果在指定时间内没有事件发生，函数会返回超时状态。如果有错误发生，会记录日志并返回失败。
 * 本函数在 `process_events_and_timers()` 中被调用，而 `process_events_and_timers()` 则在子进程的死循环中反复调用。
 *
 * @param timer epoll_wait() 阻塞的时长，单位为毫秒。值为 -1 表示无限阻塞，值为 0 表示立即返回。
 *
 * @return int 返回值：
 * - 1：正常返回；
 * - 0：发生错误或异常，应该保持进程继续运行。
 */int CSocket::epoll_process_events(int timer)
{
	//等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件【因为我只提供了这些内存】；
	//如果两次调用epoll_wait()的事件间隔比较长，则可能在epoll的双向链表中，积累了多个事件，所以调用epoll_wait，可能取到多个事件
	//阻塞timer这么长时间除非：a)阻塞时间到达 b)阻塞期间收到事件【比如新用户连入】会立刻返回c)调用时有事件也会立刻返回d)如果来个信号，比如你用kill -1 pid测试
	//如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
	//返回值：有错误发生返回-1，错误在errno中，比如你发个信号过来，就返回-1，错误信息是(4: Interrupted system call)
	//       如果你等待的是一段时间，并且超时了，则返回0；
	//       如果返回>0则表示成功捕获到这么多个事件【返回值里】

	int events = epoll_wait(m_epollhandle, m_events,MAX_EVENTS, timer);
	
	if (events == -1)
	{
		//有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
		//#define EINTR  4，EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
			   //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
		if (errno == EINTR)
		{
			//信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
			globallogger->flog(LogLevel::NOTICE, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
			return 1;  //正常返回
		}
		else
		{
			//这被认为应该是有问题，记录日志
			globallogger->flog(LogLevel::ALERT, "CSocekt::ngx_epoll_process_events()中epoll_wait()失败!");
			return 0;  //非正常返回 
		}
	}

	if (events == 0) //超时，但没事件来
	{
		if (timer != -1)
		{
			//要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
			return 1;
		}
		//无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
		globallogger->flog(LogLevel::ALERT, "CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!");
		return 0; //非正常返回 
	}

	//会惊群，一个telnet上来，4个worker进程都会被惊动，都执行下边这个
	//ngx_log_stderr(errno,"惊群测试1:%d",events); 

	//走到这里说明事件收到了
	lpconnection_t p_Conn;
	//uintptr_t          instance;
	uint32_t           revents;
	for (int i = 0; i < events; ++i)    //遍历本次epoll_wait返回的所有事件，注意events才是返回的实际事件数量
	{
		p_Conn = (lpconnection_t)(m_events[i].data.ptr);           //ngx_epoll_add_event()给进去的，这里能取出来

		/*
		instance = (uintptr_t) c & 1;                             //将地址的最后一位取出来，用instance变量标识, 见ngx_epoll_add_event，该值是当时随着连接池中的连接一起给进来的
																  //取得的是你当时调用ngx_epoll_add_event()的时候，这个连接里边的instance变量的值；
		p_Conn = (lpngx_connection_t) ((uintptr_t)p_Conn & (uintptr_t) ~1); //最后1位干掉，得到真正的c地址

		//仔细分析一下官方nginx的这个判断
		//过滤过期事件的；
		if(c->fd == -1)  //一个套接字，当关联一个 连接池中的连接【对象】时，这个套接字值是要给到c->fd的，
						   //那什么时候这个c->fd会变成-1呢？关闭连接时这个fd会被设置为-1，哪行代码设置的-1再研究，但应该不是ngx_free_connection()函数设置的-1
		{
			//比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭，那我们应该会把c->fd设置为-1；
			//第二个事件照常处理
			//第三个事件，假如这第三个事件，也跟第一个事件对应的是同一个连接，那这个条件就会成立；那么这种事件，属于过期事件，不该处理

			//这里可以增加个日志，也可以不增加日志
			ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了fd=-1的过期事件:%p.",c);
			continue; //这种事件就不处理即可
		}

		//过滤过期事件的；
		if(c->instance != instance)
		{
			//--------------------以下这些说法来自于资料--------------------------------------
			//什么时候这个条件成立呢？【换种问法：instance标志为什么可以判断事件是否过期呢？】
			//比如我们用epoll_wait取得三个事件，处理第一个事件时，因为业务需要，我们把这个连接关闭【麻烦就麻烦在这个连接被服务器关闭上了】，但是恰好第三个事件也跟这个连接有关；
			//因为第一个事件就把socket连接关闭了，显然第三个事件我们是不应该处理的【因为这是个过期事件】，若处理肯定会导致错误；
			//那我们上述把c->fd设置为-1，可以解决这个问题吗？ 能解决一部分问题，但另外一部分不能解决，不能解决的问题描述如下【这么离奇的情况应该极少遇到】：

			//a)处理第一个事件时，因为业务需要，我们把这个连接【假设套接字为50】关闭，同时设置c->fd = -1;并且调用ngx_free_connection将该连接归还给连接池；
			//b)处理第二个事件，恰好第二个事件是建立新连接事件，调用ngx_get_connection从连接池中取出的连接非常可能就是刚刚释放的第一个事件对应的连接池中的连接；
			//c)又因为a中套接字50被释放了，所以会被操作系统拿来复用，复用给了b)【一般这么快就被复用也是醉了】；
			//d)当处理第三个事件时，第三个事件其实是已经过期的，应该不处理，那怎么判断这第三个事件是过期的呢？ 【假设现在处理的是第三个事件，此时这个 连接池中的该连接 实际上已经被用作第二个事件对应的socket上了】；
				//依靠instance标志位能够解决这个问题，当调用ngx_get_connection从连接池中获取一个新连接时，我们把instance标志位置反，所以这个条件如果不成立，说明这个连接已经被挪作他用了；

			//--------------------我的个人思考--------------------------------------
			//如果收到了若干个事件，其中连接关闭也搞了多次，导致这个instance标志位被取反2次，那么，造成的结果就是：还是有可能遇到某些过期事件没有被发现【这里也就没有被continue】，照旧被当做没过期事件处理了；
				  //如果是这样，那就只能被照旧处理了。可能会造成偶尔某个连接被误关闭？但是整体服务器程序运行应该是平稳，问题不大的，这种漏网而被当成没过期来处理的的过期事件应该是极少发生的

			ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了instance值改变的过期事件:%p.",c);
			continue; //这种事件就不处理即可
		}
		//存在一种可能性，过期事件没被过滤完整【非常极端】，走下来的；
		*/

		//能走到这里，我们认为这些事件都没过期，就正常开始处理
		revents = m_events[i].events;//取出事件类型

		/*
		if(revents & (EPOLLERR|EPOLLHUP)) //例如对方close掉套接字，这里会感应到【换句话说：如果发生了错误或者客户端断连】
		{
			//这加上读写标记，方便后续代码处理，至于怎么处理，后续再说，这里也是参照nginx官方代码引入的这段代码；
			//官方说法：if the error events were returned, add EPOLLIN and EPOLLOUT，to handle the events at least in one active handler
			//我认为官方也是经过反复思考才加上着东西的，先放这里放着吧；
			revents |= EPOLLIN|EPOLLOUT;   //EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器小处理发送来的FIN包）
										   //EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
		} */

		if (revents & EPOLLIN)  //如果是读事件
		{
			//ngx_log_stderr(errno,"数据来了来了来了 ~~~~~~~~~~~~~.");
			//一个客户端新连入，这个会成立，
			//已连接发送数据来，这个也成立；
			//c->r_ready = 1;                         //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
			(this->* (p_Conn->rhandler))(p_Conn);    //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
			//如果新连接进入，这里执行的应该是CSocekt::ngx_event_accept(c)】            
			//如果是已经连入，发送数据到这里，则这里执行的应该是 CSocekt::ngx_read_request_handler()     

		}

		if (revents & EPOLLOUT) //如果是写事件【对方关闭连接也触发这个，再研究。。。。。。】，注意上边的 if(revents & (EPOLLERR|EPOLLHUP))  revents |= EPOLLIN|EPOLLOUT; 读写标记都给加上了
		{
			//ngx_log_stderr(errno,"22222222222222222222.");
			if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) //客户端关闭，如果服务器端挂着一个写通知事件，则这里个条件是可能成立的
			{
				//EPOLLERR：对应的连接发生错误                     8     = 1000 
				//EPOLLHUP：对应的连接被挂起                       16    = 0001 0000
				//EPOLLRDHUP：表示TCP连接的远端关闭或者半关闭连接   8192   = 0010  0000   0000   0000
				//我想打印一下日志看一下是否会出现这种情况
				//8221 = ‭0010 0000 0001 1101‬  ：包括 EPOLLRDHUP ，EPOLLHUP， EPOLLERR
				//ngx_log_stderr(errno,"CSocekt::ngx_epoll_process_events()中revents&EPOLLOUT成立并且revents & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)成立,event=%ud。",revents); 

				//我们只有投递了 写事件，但对端断开时，程序流程才走到这里，投递了写事件意味着 iThrowsendCount标记肯定被+1了，这里我们减回
				--p_Conn->iThrowsendCount;
			}
			else
			{
				(this->* (p_Conn->whandler))(p_Conn);   //如果有数据没有发送完毕，由系统驱动来发送，则这里执行的应该是 CSocekt::ngx_write_request_handler()
			}
		}
	}

	return 0;
}

/**
 * @brief 执行 epoll 操作（增加、修改或删除事件）。
 *
 * 该函数用于对 epoll 红黑树中的节点进行操作，包括增加、修改或删除事件。具体操作取决于传入的 `eventtype` 参数：
 * - `EPOLL_CTL_ADD`：将一个新的事件添加到红黑树中。
 * - `EPOLL_CTL_MOD`：修改已存在事件的标志。
 * - `EPOLL_CTL_DEL`：删除事件（目前未实现该功能，直接返回成功）。
 *
 * @param fd 事件关联的文件描述符（如 socket 的文件描述符）。
 * @param eventtype 操作类型，指定是增加、修改还是删除事件。
 * @param flag 事件的标志，指定感兴趣的事件类型（如 `EPOLLIN`，`EPOLLOUT` 等）。
 * @param bcaction 修改事件标志时的行为：
 * - 0：增加某个标志；
 * - 1：去掉某个标志；
 * - 其他：完全覆盖当前标志。
 * @param pConn 连接池中的连接对象，用于存储和传递事件相关信息。
 *
 * @return int 返回值：
 * - 1：操作成功；
 * - -1：操作失败，日志中记录错误信息。
 */
int CSocket::epoll_oper_event(int fd, uint32_t eventtype, uint32_t flag, int bcaction, lpconnection_t pConn)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

	if (eventtype == EPOLL_CTL_ADD) //往红黑树中增加节点；
	{
		//红黑树从无到有增加节点
		//ev.data.ptr = (void *)pConn;
		ev.events = flag;      //既然是增加节点，则不管原来是啥标记
		pConn->events = flag;  //这个连接本身也记录这个标记
	}
	else if (eventtype == EPOLL_CTL_MOD)
	{
		//节点已经在红黑树中，修改节点的事件信息
		ev.events = pConn->events;  //先把标记恢复回来
		if (bcaction == 0)
		{
			//增加某个标记            
			ev.events |= flag;
		}
		else if (bcaction == 1)
		{
			//去掉某个标记
			ev.events &= ~flag;
		}
		else
		{
			//完全覆盖某个标记            
			ev.events = flag;      //完全覆盖            
		}
		pConn->events = ev.events; //记录该标记
	}
	else
	{
		//删除红黑树中节点，目前没这个需求【socket关闭这项会自动从红黑树移除】，所以将来再扩展
		return  1;  //先直接返回1表示成功
	}

	//原来的理解中，绑定ptr这个事，只在EPOLL_CTL_ADD的时候做一次即可，但是发现EPOLL_CTL_MOD似乎会破坏掉.data.ptr，因此不管是EPOLL_CTL_ADD，还是EPOLL_CTL_MOD，都给进去
	//找了下内核源码SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,		struct epoll_event __user *, event)，感觉真的会覆盖掉：
	   //copy_from_user(&epds, event, sizeof(struct epoll_event)))，感觉这个内核处理这个事情太粗暴了
	ev.data.ptr = (void*)pConn;

	if (epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1)
	{
		globallogger->flog(LogLevel::ERROR, "CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.", fd, eventtype, flag, bcaction);
		return -1;
	}
	return 1;
}

/**
 * @brief 将一个待发送消息入到发送消息队列中，并处理相关的安全检查。
 *
 * 该函数负责将消息放入发送队列。如果队列过大或者消息发送过慢，则采取相应的安全措施（例如丢弃消息或关闭连接）。
 *
 * @param psendbuf 待发送的消息缓冲区。
 */
void CSocket::msgSend(char* psendbuf)
{
	// 获取内存管理单例对象
	CMemory* p_memory = CMemory::GetInstance();

	// 使用互斥量保护发送队列
	std::lock_guard<std::mutex> lock(m_sendMessageQueueMutex); // 使用C++的锁自动管理

	// 检查发送队列是否过大，避免内存溢出等问题
	if (m_iSendMsgQueueCount > 50000)
	{
		// 发送队列过大，可能是由于客户端不接收数据导致队列不断积压
		// 为了防止服务器不稳定，丢弃当前消息
		m_iDiscardSendPkgCount++;
		p_memory->FreeMemory(psendbuf);
		return;
	}

	// 提取消息头并检查该用户的消息发送队列状态
	LPSTRUC_MSG_HEADER pMsgHeader = reinterpret_cast<LPSTRUC_MSG_HEADER>(psendbuf);
	lpconnection_t p_Conn = pMsgHeader->pConn;

	if (p_Conn->iSendCount > 400)
	{
		// 如果某个用户的发送队列条目数过多，认为该用户可能是恶意用户
		// 直接关闭与该用户的连接，丢弃消息
		std::cerr << "CSocekt::msgSend() 中发现某用户 " << p_Conn->fd
			<< " 积压了大量待发送数据包，切断与他的连接！" << std::endl;
		m_iDiscardSendPkgCount++;
		p_memory->FreeMemory(psendbuf);
		zdClosesocketProc(p_Conn); // 关闭连接
		return;
	}

	// 增加发送队列中该用户的条目计数
	++p_Conn->iSendCount;

	// 将消息缓冲区放入发送队列
	m_MsgSendQueue.push_back(psendbuf);
	++m_iSendMsgQueueCount; // 原子操作增加队列大小

	//将信号量的值+1,这样其他卡在sem_wait的就可以走下去
	if (sem_post(&m_semEventSendQueue) == -1)  //让ServerSendQueueThread()流程走下来干活
	{
		globallogger->clog(LogLevel::ERROR, "CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");
	}
	return;
}

/**
 * @brief 主动关闭一个连接时的善后处理函数。
 *
 * 该函数会执行关闭连接后的清理工作，包括从时间队列中移除连接、关闭 socket 描述符并回收连接。
 * 注意，该函数是线程安全的，即使被多个线程调用也不会影响服务器的稳定性和正确性。
 *
 * @param p_Conn 指向要关闭的连接的指针。
 */
void CSocket::zdClosesocketProc(lpconnection_t p_Conn)
{
	if (m_ifkickTimeCount == 1)
	{
		DeleteFromTimerQueue(p_Conn); //从时间队列中把连接干掉
	}
	if (p_Conn->fd != -1)
	{
		close(p_Conn->fd); //这个socket关闭，关闭后epoll就会被从红黑树中删除，所以这之后无法收到任何epoll事件
		p_Conn->fd = -1;
	}

	if (p_Conn->iThrowsendCount > 0)
		--p_Conn->iThrowsendCount;   //归0

	inRecyConnectQueue(p_Conn);
	return;
}

/**
 * @brief 读取各种配置项。
 *
 * 该函数用于从配置文件中读取服务器的各项设置，如最大连接数、监听端口数、连接回收时间等。
 */
void CSocket::ReadConf()
{
	m_worker_connections = globalconfig->GetIntDefault("worker_connections", m_worker_connections); //epoll连接的最大项数
	m_ListenPortCount = globalconfig->GetIntDefault("ListenPortCount", m_ListenPortCount);       //取得要监听的端口数量
	m_RecyConnectionWaitTime = globalconfig->GetIntDefault("Sock_RecyConnectionWaitTime", m_RecyConnectionWaitTime); //等待这么些秒后才回收连接

	m_ifkickTimeCount = globalconfig->GetIntDefault("Sock_WaitTimeEnable", 0);                                //是否开启踢人时钟，1：开启   0：不开启
	m_iWaitTime = globalconfig->GetIntDefault("Sock_MaxWaitTime", m_iWaitTime);                         //多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用	
	m_iWaitTime = (m_iWaitTime > 5) ? m_iWaitTime : 5;                                                 //不建议低于5秒钟，因为无需太频繁
	m_ifTimeOutKick = globalconfig->GetIntDefault("Sock_TimeOutKick", 0);                                   //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用 

	m_floodAkEnable = globalconfig->GetIntDefault("Sock_FloodAttackKickEnable", 0);                          //Flood攻击检测是否开启,1：开启   0：不开启
	m_floodTimeInterval = globalconfig->GetIntDefault("Sock_FloodTimeInterval", 100);                            //表示每次收到数据包的时间间隔是100(毫秒)
	m_floodKickCount = globalconfig->GetIntDefault("Sock_FloodKickCounter", 10);                              //累积多少次踢出此人

	return;
}

/**
 * @brief 打开并绑定监听端口。
 *
 * 该函数用于为每个监听端口创建 socket，并将其绑定到指定的端口。服务器可以监听多个端口。
 *
 * @return bool 如果所有端口都成功绑定并开始监听，返回 `true`；否则返回 `false`。
 */
bool CSocket::open_listening_sockets()
{
	int isock;  //socket
	struct sockaddr_in serv_addr;  //服务器的地址结构结构体
	int iport;	//端口
	char strinfo[100];  //临时字符串

	//初始化相关
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;			//选择协议族为IPV4
	serv_addr.sin_addr.s_addr = inet_addr("192.168.72.130"); //监听本地所有的IP地址INADDR_ANY

	//中途用到的一些配置信息
	for (int i = 0; i < m_ListenPortCount; i++) //要监听这么多个端口
	{
		//参数1：AF_INET：使用ipv4协议，一般就这么写
	   //参数2：SOCK_STREAM：使用TCP，表示可靠连接【相对还有一个UDP套接字，表示不可靠连接】
	   //参数3：给0，固定用法，就这么记
		isock = socket(AF_INET, SOCK_STREAM, 0);
		if (isock == -1)
		{
			globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize()中socket()失败,i=%d.", i);
			return false;
		}

		//setsockopt（）:设置一些套接字参数选项；
		//参数2：是表示级别，和参数3配套使用，也就是说，参数3如果确定了，参数2就确定了;
		//参数3：允许重用本地地址
		//设置 SO_REUSEADDR
		int reuseaddr = 1;	//打开对应的设置项
		if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*)&reuseaddr, sizeof(reuseaddr)) == -1)
		{
			globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.", i);
			close(isock); //无需理会是否正常执行了                                                  
			return false;
		}

		//设置该socket为非阻塞
		if (setnonblocking(isock) == false)
		{
			globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize()中setnonblocking()失败,i=%d.", i);
			close(isock);
			return false;
		}

		//设置本服务器要监听的地址和端口，这样客户端才能连接到该地址和端口并发送数据        
		strinfo[0] = 0;
		sprintf(strinfo, "ListenPort%d", i);
		iport = globalconfig->GetIntDefault(strinfo, 10000);
		serv_addr.sin_port = htons((in_port_t)iport);   //in_port_t其实就是uint16_t

		//绑定服务器地址结构体
		if (bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		{
			globallogger->clog(LogLevel::ERROR, "CSocekt::Initialize()中bind()失败,i=%d.成为原因%s", i, strerror(errno));
			close(isock);
			return false;
		}

		//开始监听
		if (listen(isock, LISTEN_BACKLOG) == -1)
		{
			globallogger->flog(LogLevel::ERROR, "CSocekt::Initialize()中listen()失败,i=%d.", i);
			close(isock);
			return false;
		}

		auto p_listensocketitem = std::make_shared<listening_t>();
		//lplistening_t p_listensocketitem = new listening_t;
		memset(p_listensocketitem.get(), 0, sizeof(listening_t));      //注意后边用的是 ngx_listening_t而不是lpngx_listening_t
		p_listensocketitem->port = iport;                          //记录下所监听的端口号
		p_listensocketitem->fd = isock;                          //套接字木柄保存下来   
		globallogger->clog(LogLevel::NOTICE, "监听%d端口成功!", iport); //显示一些信息到日志中
		m_ListenSocketList.push_back(p_listensocketitem);          //加入到队列中
	}

	if (m_ListenSocketList.size() <= 0)  //不可能一个端口都不监听吧
		return false;
	return true;
}

/**
 * @brief 关闭所有监听端口。
 *
 * 该函数关闭所有正在监听的端口，并释放相关资源。
 */
void CSocket::close_listening_sockets()
{
	for (int i = 0; i < m_ListenPortCount; i++) //要关闭这么多个监听端口
	{
		//ngx_log_stderr(0,"端口是%d,socketid是%d.",m_ListenSocketList[i]->port,m_ListenSocketList[i]->fd);
		close(m_ListenSocketList[i]->fd);
		globallogger->flog(LogLevel::NOTICE, "关闭监听端口%d!", m_ListenSocketList[i]->port); //显示一些信息到日志中
	}//end for(int i = 0; i < m_ListenPortCount; i++)
	return;
}

/**
 * @brief 设置 socket 为非阻塞模式。
 *
 * 该函数通过 `ioctl` 系统调用将指定的 socket 设置为非阻塞模式。
 *
 * @param sockfd 要设置的 socket 描述符。
 * @return bool 如果设置成功返回 `true`，否则返回 `false`。
 */
bool CSocket::setnonblocking(int sockfd)
{

	int nb = 1; //0：清除，1：设置  
	if (ioctl(sockfd, FIONBIO, &nb) == -1) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
	{
		return false;
	}
	return true;
}


/**
 * @brief 测试是否为Flood攻击
 *
 * 判断当前连接是否存在Flood攻击，如果两次收到包的时间间隔过短（小于设置的Flood时间间隔），则认为是Flood攻击，返回true，反之返回false。
 *
 * @param pConn 连接信息指针
 * @return true 如果发生Flood攻击
 * @return false 如果未发生Flood攻击
 */
bool CSocket::TestFlood(lpconnection_t pConn)
{
	struct  timeval sCurrTime;   //当前时间结构
	uint64_t        iCurrTime;   //当前时间（单位：毫秒）
	bool  reco = false;

	gettimeofday(&sCurrTime, NULL); //取得当前时间
	iCurrTime = (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);  //毫秒
	if ((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)   //两次收到包的时间 < 100毫秒
	{
		//发包太频繁记录
		pConn->FloodAttackCount++;
		pConn->FloodkickLastTime = iCurrTime;
	}
	else
	{
		//既然发布不这么频繁，则恢复计数值
		pConn->FloodAttackCount = 0;
		pConn->FloodkickLastTime = iCurrTime;
	}

	//ngx_log_stderr(0,"pConn->FloodAttackCount=%d,m_floodKickCount=%d.",pConn->FloodAttackCount,m_floodKickCount);

	if (pConn->FloodAttackCount >= m_floodKickCount)
	{
		//可以踢此人的标志
		reco = true;
	}
	return reco;
}

//--------------------------------------------------------------------
/**
 * @brief 处理发送消息队列的线程
 *
 * 该线程负责从消息队列中取出消息并将其发送到连接池中的相应连接。
 * 该函数使用信号量机制来同步消息的处理，确保不会在没有数据的情况下阻塞。
 *
 * @param threadData 线程数据，包含当前线程的相关信息
 * @return void* 返回线程执行结果（通常为NULL）
 */
void* CSocket::ServerSendQueueThread(void* threadData)
{
	ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
	CSocket* pSocketObj = pThread->_pThis;
	
	std::list <char*>::iterator pos, pos2, posend;

	char* pMsgBuf;
	LPSTRUC_MSG_HEADER	pMsgHeader;
	LPCOMM_PKG_HEADER   pPkgHeader;
	lpconnection_t  p_Conn;
	unsigned short      itmp;
	ssize_t             sendsize;

	CMemory* p_memory = CMemory::GetInstance();

	while (g_stopEvent == 0) //不退出
	{
		//如果信号量值>0，则 -1(减1) 并走下去，否则卡这里卡着【为了让信号量值+1，可以在其他线程调用sem_post达到，实际上在CSocekt::msgSend()调用sem_post就达到了让这里sem_wait走下去的目的】
		//******如果被某个信号中断，sem_wait也可能过早的返回，错误为EINTR；
		//整个程序退出之前，也要sem_post()一下，确保如果本线程卡在sem_wait()，也能走下去从而让本线程成功返回
		if (sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
		{
			//失败？及时报告，其他的也不好干啥
			if (errno != EINTR) //这个我就不算个错误了【当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。】
				globallogger->flog(LogLevel::ERROR, "CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");
		}

		//一般走到这里都表示需要处理数据收发了
		if (g_stopEvent != 0)  //要求整个进程退出
			break;

		if (pSocketObj->m_iSendMsgQueueCount > 0) //原子的 
		{
			try {
				std::lock_guard<std::mutex> lock(pSocketObj->m_sendMessageQueueMutex); // 自动加锁，作用域结束时自动解锁
				// 这里是操作发送消息队列的代码
			}
			catch (const std::system_error& e) {
				globallogger->clog(LogLevel::NOTICE,
					"CSocekt::ServerSendQueueThread()中mutex lock失败，错误信息: %s", e.what());
			}

			pos = pSocketObj->m_MsgSendQueue.begin();
			posend = pSocketObj->m_MsgSendQueue.end();

			while (pos != posend)
			{
				pMsgBuf = (*pos);                          //拿到的每个消息都是 消息头+包头+包体【但要注意，我们是不发送消息头给客户端的】
				pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;  //指向消息头
				pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + pSocketObj->m_iLenMsgHeader);	//指向包头
				p_Conn = pMsgHeader->pConn;

				//包过期，因为如果 这个连接被回收，比如在close_connection(),inRecyConnectQueue()中都会自增iCurrsequence
				//而且这里有没必要针对 本连接 来用m_connectionMutex临界 ,只要下面条件成立，肯定是客户端连接已断，要发送的数据肯定不需要发送了
				if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
				{
					//本包中保存的序列号与p_Conn【连接池中连接】中实际的序列号已经不同，丢弃此消息，小心处理该消息的删除
					pos2 = pos;
					pos++;
					pSocketObj->m_MsgSendQueue.erase(pos2);
					--pSocketObj->m_iSendMsgQueueCount; //发送消息队列容量少1		
					p_memory->FreeMemory(pMsgBuf);
					continue;
				} //end if

				if (p_Conn->iThrowsendCount > 0)
				{
					//靠系统驱动来发送消息，所以这里不能再发送
					pos++;
					continue;
				}

				--p_Conn->iSendCount;   //发送队列中有的数据条目数-1；

				//走到这里，可以发送消息，一些必须的信息记录，要发送的东西也要从发送队列里干掉
				p_Conn->psendMemPointer = pMsgBuf;      //发送后释放用的，因为这段内存是new出来的
				pos2 = pos;
				pos++;
				pSocketObj->m_MsgSendQueue.erase(pos2);
				--pSocketObj->m_iSendMsgQueueCount;      //发送消息队列容量少1	
				p_Conn->psendbuf = (char*)pPkgHeader;   //要发送的数据的缓冲区指针，因为发送数据不一定全部都能发送出去，我们要记录数据发送到了哪里，需要知道下次数据从哪里开始发送
				itmp = ntohs(pPkgHeader->pkgLen);        //包头+包体 长度 ，打包时用了htons【本机序转网络序】，所以这里为了得到该数值，用了个ntohs【网络序转本机序】；
				p_Conn->isendlen = itmp;                 //要发送多少数据，因为发送数据不一定全部都能发送出去，我们需要知道剩余有多少数据还没发送

				//这里是重点，我们采用 epoll水平触发的策略，能走到这里的，都应该是还没有投递 写事件 到epoll中
					//epoll水平触发发送数据的改进方案：
					//开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
					//如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
					//此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
					//优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；                         
				//(1)直接调用write或者send发送数据
				//ngx_log_stderr(errno,"即将发送数据%ud。",p_Conn->isendlen);

				sendsize = pSocketObj->sendproc(p_Conn, p_Conn->psendbuf, p_Conn->isendlen); //注意参数
				if (sendsize > 0)
				{
					if (sendsize == p_Conn->isendlen) //成功发送出去了数据，一下就发送出去这很顺利
					{
						//成功发送的和要求发送的数据相等，说明全部发送成功了 发送缓冲区去了【数据全部发完】
						p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
						p_Conn->psendMemPointer = NULL;
						p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的                        
						//ngx_log_stderr(0,"CSocekt::ServerSendQueueThread()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉
					}
					else  //没有全部发送完毕(EAGAIN)，数据只发出去了一部分，但肯定是因为 发送缓冲区满了,那么
					{
						//发送到了哪里，剩余多少，记录下来，方便下次sendproc()时使用
						p_Conn->psendbuf = p_Conn->psendbuf + sendsize;
						p_Conn->isendlen = p_Conn->isendlen - sendsize;
						//因为发送缓冲区慢了，所以 现在我要依赖系统通知来发送数据了
						++p_Conn->iThrowsendCount;             //标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送【原子+1，且不可写成p_Conn->iThrowsendCount = p_Conn->iThrowsendCount +1 ，这种写法不是原子+1】
						//投递此事件后，我们将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
						if (pSocketObj->epoll_oper_event(
							p_Conn->fd,         //socket句柄
							EPOLL_CTL_MOD,      //事件类型，这里是增加【因为我们准备增加个写通知】
							EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
							0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
							p_Conn              //连接池中的连接
						) == -1)
						{
							//有这情况发生？这可比较麻烦，不过先do nothing
							globallogger->clog(LogLevel::ERROR, "CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
						}

						//ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中数据没发送完毕【发送缓冲区满】，整个要发送%d，实际发送了%d。",p_Conn->isendlen,sendsize);

					} //end if(sendsize > 0)
					continue;  //继续处理其他消息                    
				}  //end if(sendsize > 0)

				//能走到这里，应该是有点问题的
				else if (sendsize == 0)
				{
					//发送0个字节，首先因为我发送的内容不是0个字节的；
					//然后如果发送 缓冲区满则返回的应该是-1，而错误码应该是EAGAIN，所以我综合认为，这种情况我就把这个发送的包丢弃了【按对端关闭了socket处理】
					//这个打印下日志，我还真想观察观察是否真有这种现象发生
					//ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sendproc()居然返回0？"); //如果对方关闭连接出现send=0，那么这个日志可能会常出现，商用时就 应该干掉
					//然后这个包干掉，不发送了
					p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
					p_Conn->psendMemPointer = NULL;
					p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的    
					continue;
				}

				//能走到这里，继续处理问题
				else if (sendsize == -1)
				{
					//发送缓冲区已经满了【一个字节都没发出去，说明发送 缓冲区当前正好是满的】
					++p_Conn->iThrowsendCount; //标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
					//投递此事件后，我们将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
					if (pSocketObj->epoll_oper_event(
						p_Conn->fd,         //socket句柄
						EPOLL_CTL_MOD,      //事件类型，这里是增加【因为我们准备增加个写通知】
						EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
						0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
						p_Conn              //连接池中的连接
					) == -1)
					{
						//有这情况发生？这可比较麻烦，不过先do nothing
						globallogger->clog(LogLevel::ERROR, "CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");
					}
					continue;
				}

				else
				{
					//能走到这里的，应该就是返回值-2了，一般就认为对端断开了，等待recv()来做断开socket以及回收资源
					p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
					p_Conn->psendMemPointer = NULL;
					p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的  
					continue;
				}

			} //end while(pos != posend)

			/*err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex);
			if (err != 0)  globallogger->clog(LogLevel::ERROR, "CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);*/

		} //if(pSocketObj->m_iSendMsgQueueCount > 0)
	} //end while

	return (void*)0;
}
