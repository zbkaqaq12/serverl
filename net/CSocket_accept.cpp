#include"CSocket.h"
#include"global.h"
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

/**
 * @brief 建立新连接的处理函数
 * @details 该函数在新连接到来时被 `ngx_epoll_process_events()` 调用。根据边缘触发（ET）还是水平触发（LT）模式，确保只调用一次 `accept()`。
 *          函数通过 `accept()` 或 `accept4()` 接受新的连接并分配连接池，同时添加新连接到 `epoll` 中。
 *
 * @param oldc 监听连接对象，用于获取监听套接字
 */
void CSocket::event_accept(lpconnection_t oldc)
{
	//因为listen套接字上用的不是ET【边缘触发】，而是LT【水平触发】，意味着客户端连入如果要不处理，这个函数会被多次调用，所以，这里这里可以不必多次accept()，可以只执行一次accept()
	//这里可以根据实际情况，进行多次accept()，但是我们这里就只执行一次accept()
	struct sockaddr mysockaddr;        //远端服务器的地址
	socklen_t socklen;
	int err;
	LogLevel level;
	int s;
	static int use_accept4 = 1;
	lpconnection_t newc;    //代表连接池中的一个连接

	socklen = sizeof(mysockaddr);
	do
	{
		if (use_accept4)
		{
			//因为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept4()也不会卡在这里；
			s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);
		}
		else
		{
			//因为listen套接字是非阻塞的，所以即便已完成连接队列为空，accept()也不会卡在这里；
			s = accept(oldc->fd, &mysockaddr, &socklen);
		}

		//惊群，有时候不一定完全惯性，可能也会惊动其他的worker进程，待验证
		if (s == -1)
		{
			err = errno;

			//对于accept()，send()，recv()这些函数，如果事件未发生时errno通常被设置成EAGAIN（意为"再来一次"）或者EWOULDBLOCK（意为"期待阻塞"）
			if (err == EAGAIN) //accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
			{
				//除非你用一个循环不断的accept()取走所有的连接，不然一般不会有这个错误【我们这里只取一个连接】
				return;
			}
			level = LogLevel::ALERT;
			if (err == ECONNABORTED)  //ECONNRESET错误则发生在对方意外关闭套接字后，这里收到该消息，而我们继续处理--这里可能是对方断开了
			{
				//该错误被描述为"software caused connection abort"，即"软件引起的连接中止"。原因在于客户端在服务器处理完成前就关闭了连接，
				//会导致服务器端收到一个 RST 包，即复位包，告诉服务器这个连接已经被重置了。
				//在 POSIX 系统中，这种情况下 errno 会被设置为 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理终止的连接，
				//服务器进程将不会知道该中止的发生。
				//我们这里不认为这个是错误，只是简单的记录一下日志信息
				level = LogLevel::ERROR;
			}
			else if (err == EMFILE || err == ENFILE) //EMFILE:进程的fd已用尽了  ENFILE:系统的fd已用尽了，这个限制可以参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
				//ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
			//ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定不会超过system-wide的resource limits。
			{
				level = LogLevel::CRIT;
			}
			globallogger->flog(level, "CSocekt::ngx_event_accept()中accept4()失败!");

			if (use_accept4 && err == ENOSYS) //accept4()函数没实现，坑爹
			{
				use_accept4 = 0;  //标记不使用accept4()函数，改用accept()函数
				continue;         //回去重试
			}

			if (err == ECONNABORTED)  //对方关闭套接字
			{
				//这个错误因为可以忽略，所以不用干啥
				//do nothing
			}

			if (err == EMFILE || err == ENFILE)
			{
				//do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
				//我这里目前先不处理吧【因为上边已经写日志了】；
			}
			return;
		}  //end if(s == -1)

		//走到这里的，表示accept4()/accept()成功了  
		newc = get_connection(s); //这是针对新连接的，所以这个socket上从默认是空的，什么事件都没有，直接从连接池中取一个连接来
		if (newc == NULL)
		{
			//连接池中连接不够用，那么就得把这个socket直接关闭并返回了，因为在ngx_get_connection()中已经写日志了，所以这里不需要写日志了
			if (close(s) == -1)
			{
				globallogger->flog(LogLevel::ALERT, "CSocekt::ngx_event_accept()中close(%d)失败!", s);
			}
			return;
		}
		//...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

		//成功的拿到了连接池中的一个连接
		memcpy(&newc->s_sockaddr, &mysockaddr, socklen);  //拷贝客户端地址到连接对象【要转换字符串ip地址参考函数ngx_sock_ntop()】

		if (!use_accept4)
		{
			//如果不是用accept4()取得的socket，那么就要设置为非阻塞【因为用accept4()的已经被accept4()设置为非阻塞了】
			if (setnonblocking(s) == false)
			{
				//设置非阻塞居然失败
				close_connection(newc); //关闭套接字，并且将连接放回到连接池中
				return; //直接返回
			}
		}

		newc->listening = oldc->listening;                    //连接对象 和监听对象关联，方便通过连接对象找监听对象
		//newc->w_ready = 1;                                    //标记可以写，新连接写事件肯定是ready的，这是从连接池拿出一个连接时就要初始化好的属性            

		newc->rhandler = &CSocket::read_request_handler;  //设置数据来时的读处理函数，其实官方nginx中是ngx_http_wait_request_handler()
		newc->whandler = &CSocket::write_request_handler; //设置数据发送时的写处理函数。
		//客户端应该主动发送第一条数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发ngx_wait_request_handler()被ngx_epoll_process_events()调用        
		if (epoll_oper_event(
			s,                  //socket句柄
			EPOLL_CTL_ADD,      //事件类型，这里是增加
			EPOLLIN | EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
			0,                  //对于事件类型为增加的，不需要这个参数
			newc                //连接池中的连接
		) == -1)
		{
			//增加事件失败，失败日志在ngx_epoll_add_event中写过了，这里不多写啥；
			close_connection(newc);//关闭socket,这种可以立即回收这个连接，无需延迟，因为其上还没有数据收发，谈不到业务逻辑因此无需延迟；
			return; //直接返回
		}

		if (m_ifkickTimeCount == 1)
		{
			AddToTimerQueue(newc);
		}
		++m_onlineUserCount;  //连入用户数量+1 
		break;  //一般就是循环一次就跳出去

	} while (1);

	return;
}
