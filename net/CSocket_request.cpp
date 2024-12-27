#include"global.h"
#include"CSocket.h"
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h>   //多线程
#include "CMemory.h"
#include "router.h"

/**
 * @brief 处理接收到的数据包
 * @details 该函数用于处理服务器接收到的数据。当有数据可读时，`epoll_process_events()` 会调用此函数处理数据的接收和处理。
 *          此函数会根据连接状态（接收包头、接收包体）不同而执行不同的处理逻辑。
 *          如果启用了Flood攻击检测，会检查是否存在Flood攻击。
 *
 * @param pConn 连接对象，包含当前连接的数据接收缓冲区和接收状态等信息
 */
void CSocket::read_request_handler(lpconnection_t pConn)
{
    auto logger = Logger::GetInstance();
    
    // 从连接到读缓冲区读取数据
    ssize_t n = pConn->readBuffer.writableBytes();
    char* buf = pConn->readBuffer.beginWrite();
    
    // 从套接字读取数据
    ssize_t reco = recvproc(pConn, buf, n);
    
    // 处理接收到的数据
    if (reco <= 0) {
        if (reco == 0) {
            logger->logSystem(LogLevel::INFO, "Client closed connection");
        } else {
            logger->logSystem(LogLevel::ERROR, "Receive error: %s", strerror(errno));
        }
        close_connection(pConn);
        return;
    }
    
    // 更新缓冲区状态
    pConn->readBuffer.hasWritten(reco);
    pConn->httpCtx->lastActivityTime = time(NULL);
    
    // 处理HTTP请求
    bool ok = processHttpRequest(pConn);
    if (!ok)
    {
        // 解析失败，关闭连接
        close_connection(pConn);
        return;
    }
    
    return;
}

/**
 * @brief 处理数据包接收的处理函数
 * @details 该函数用于从客户端接收数据并进行处理。处理各种连接关闭、系统错误等，如果自动关闭连接并释放资源。如果接收成功则返回实际接收到的字节数。
 *
 * @param c 连接对象，包含连接的文件描述符等信息
 * @param buff 存储接收数据的缓冲区
 * @param buflen 缓冲区大小，表示要接收的长度
 * @return 返回接收到的字节数，出错则返回-1并关闭连接
 */
ssize_t CSocket::recvproc(lpconnection_t c, char* buff, ssize_t buflen)
{
    ssize_t n;
    auto logger = Logger::GetInstance();
    n = recv(c->fd, buff, buflen, 0); //recv()系统函数，最后一个参数flag一般为0
    if (n == 0)
    {
        //客户端关闭。应该处理这种非正常的额四路挥手，后续处理就直接回收连接，关闭socket即可 
        //log_stderr(0,"连接被客户正常关闭[4路挥手关闭]！");
        close_connection(c);
        return -1;
    }
    //客户端没断，有数据发送过来 
    if (n < 0) //这被认为有错误发生
    {
        //EAGAIN和EWOULDBLOCK[这个应该常用在hp上]应该等同一个值，表示没收到数据，一般来说在ET模式下会出现这个错误，因为ET模式下是不停的recv肯定有一个时间收到这个errno，但LT模式下一般是来事件才收，所以不该出现这个返回值
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            logger->logSystem(LogLevel::ERROR, "CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }
        //EINTR错误的产生：当阻塞于某个系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于系统调用时由父进程捕获到了一个有效信号时，内核会致使accept回一个EINTR错误(被中断的系统调用)。
        if (errno == EINTR)  //这个不算错误，是我参考官方nginx代码写法
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            logger->logSystem(LogLevel::ERROR, "CSocket::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }

        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        //errno参考：http://dhfapiran1.360drm.com        
        if (errno == ECONNRESET)  //#define ECONNRESET 104 /* Connection reset by peer */
        {
            //如果客户端没有正常关闭socket连接，却关闭了整个运行程序（直接被kill掉了），那么会产生这个错误            
            //10054(WSAECONNRESET)--远程程序正在连接的时候关闭会产生这个错误--远程主机强迫关闭了一个现有的连接
            //算常规错误吧，日志都不用打印，没啥意思，太普通的错误
            //do nothing

            //....一些大家遇到的很普通的错误信息，也可以往这里加各种，但是我觉得我们的日志都还是有限的，能够增加到这里的都是很普遍的错误，所以并不把很多杂七杂八的错误都往这里加
        }
        else
        {
            //能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            logger->logSystem(LogLevel::ERROR, "CSocket::recvproc()中发生错误，我打印出来看看是啥错误！");  //正式运营时可以考虑这些日志打印去掉
        }

        //log_stderr(0,"连接被客户端 非 正常关闭！");

        //这种真正的错误就要，直接关闭套接字，释放连接池中连接了
        close_connection(c);
        return -1;
    }

    //能走到这里的，就认为收到了有效数据
    return n; //返回收到的字节数
}

/**
 * @brief 处理HTTP请求
 * @details 该函数用于处理HTTP请求。解析请求，构造响应，并根据请求类型进行相应的处理。
 *
 * @param pConn 当前连接对象
 * @return 返回处理结果，true表示需要继续处理，false表示请求已处理完毕
 */
bool CSocket::processHttpRequest(lpconnection_t pConn) {
    auto logger = Logger::GetInstance();
    auto& parser = pConn->httpCtx->parser;
    
    // 解析请求
    size_t parsed = parser->parse(pConn->readBuffer.peek(), 
                                pConn->readBuffer.readableBytes());
    
    if (parser->hasError()) {
        // 发送 400 Bad Request
        std::string response = "HTTP/1.1 400 Bad Request\r\n"
                             "Content-Length: 11\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "Bad Request";
        pConn->writeBuffer.append(response.c_str(), response.length());
        
        // 注册写事件
        if (epoll_oper_event(
            pConn->fd,
            EPOLL_CTL_MOD,
            EPOLLOUT | EPOLLET,
            0,
            pConn
        ) == -1) {
            logger->logSystem(LogLevel::ERROR, "Failed to add write event");
            return false;
        }
        
        // 解析错误，关闭连接
        pConn->httpCtx->keepAlive = false;
        return false;
    }
    
    if (!parser->isComplete()) {
        // 需要更多数据
        return true;
    }
    
    // 更新读缓冲区
    pConn->readBuffer.retrieve(parsed);
    
    // 获取解析后的请求信息
    auto method = parser->getMethod();
    auto path = parser->getPath();
    auto version = parser->getVersion();
    auto headers = parser->getHeaders();
    auto body = parser->getBody();
    
    // 检查是否保持连接
    bool keepAlive = false;
    auto it = headers.find("Connection");
    if (it != headers.end()) {
        keepAlive = (it->second == "keep-alive");
    }
    pConn->httpCtx->keepAlive = keepAlive;
    
    // 设置连接状态为处理中
    pConn->httpCtx->state = connection_s::HttpContext::ConnectionState::PROCESSING;
    
    // 将连接指针转换为消息格式
    CMemory* p_memory = CMemory::GetInstance();
    char* msgBuf = (char*)p_memory->AllocMemory(sizeof(lpconnection_t), false);
    memcpy(msgBuf, &pConn, sizeof(lpconnection_t));
    
    // 将请求添加到工作队列，由工作线程处理
    g_threadpool->inMsgRecvQueueAndSignal(msgBuf);
    
    // 如果是长连接，重置解析器
    if (keepAlive) {
        parser->reset();
        return true;
    }
    
    return false;
}

/**
 * @brief 发送数据包并处理状态
 * @details 该函数用于向连接发送数据。处理各种发送结果，包括成功发送、发送缓冲区已满、对端断开连接等情况。如果发送缓冲区已满返回-1，如果对端断开返回0，如果发生其他错误返回-2。
 *
 * @param c 当前连接对象
 * @param buff 要发送的数据缓冲区
 * @param size 发送数据的大小
 * @return 返回成功发送的字节数，或表示错误的各种值：
 *         > 0: 发送成功���字节数
 *         = 0: 对端已关闭连接
 *         -1: 发送缓冲区已满（EAGAIN）
 *         -2: 发生其他错误
 */
ssize_t CSocket::sendproc(lpconnection_t c, char* buff, ssize_t size)  //ssize_t是有符号整型，在32位机器上等同于int，在64位机器上等同于long int，size_t就是无符号型的ssize_t
{
    //这里参考官方nginx函数ngx_unix_send()的写法
    ssize_t   n;
    auto logger = Logger::GetInstance();
    for (;; )
    {
        n = send(c->fd, buff, size, 0); //send()系统函数， 最后一个参数flag，一般为0； 
        if (n > 0) //成功发送了一些数据
        {
            //发送成功一些数据，但发送了多少，我这里不关心，也不需要再次send
            //这里有两种情况
            //(1) n == size也就是想发送多少就发送多少了，这是圆满的，顺利的
            //(2) n < size 没发送完毕，那肯定是发送缓冲区满了，所以也不必要重试发送，直接返回吧
            return n; //返回本次发送的字节数
        }

        if (n == 0)
        {
            //send()返回0？一般recv()返回0表示断开,send()返回0，我这里就直接返回0吧【让调用者处理】，我个人认为send()返回0，要么你发送的字节是0，要么对端可能断开了
            //网上找料：send=0表示超时，对方主动关闭了连接过程
            //我们写代码要遵循一个原则，连接断开，我们并不在send动作里处理诸如关闭socket这种动作，集中到recv那里处理，否则send,recv都处理都处理连接断开关闭socket则会乱套
            //连接断开epoll会通知并且在recv中会处理，所以这里什么也不用干
            return 0;
        }

        if (errno == EAGAIN)  //这东西应该等同于EWOULDBLOCK
        {
            //内核缓冲区满，这个不算错误
            return -1;  //表示发送缓冲区满了
        }

        if (errno == EINTR)
        {
            //这个应该也不算错误 ，收到某个信号导致send产生这个错误？
            //参考官方的写法，打印个���志，其他啥也没干，那就是等下次for循环重新send试一次了
            logger->logSystem(LogLevel::ERROR, "CSocket::sendproc()中send()失败.");  //打印个日志看看啥时候出这个错误
            //其他不需要做什么，等下次for循环吧            
        }
        else
        {
            //走到这里表示是其他错误码，都表示错误，错误我也不断开socket，我也依然等待recv()来统一处理断开，因为我是多线程，send()也会断开，recv()也会断开，如���都处理断开会乱套
            return -2;
        }
    } //end for
}

/**
 * @brief 数据发送完毕后处理函数
 * @details 当数据可写时，epoll通知了该函数。该函数会尝试发送数据，如果发送缓冲区满则返回-1。发送成功则更新余数据。数据发送完毕后，会从epoll中移除写事件。
 *          如果发送过程中发生错误，会记录错误日志。如果数据完全发送完毕，则通知发送者继续执行。
 *
 * @param pConn 当前连接对象，包含待发送数据的缓冲区和数据长度等信息
 */
void CSocket::write_request_handler(lpconnection_t pConn)
{
    auto logger = Logger::GetInstance();
    
    // 获取要发送的数据
    size_t len = pConn->writeBuffer.readableBytes();
    if (len == 0) {
        // 没有数据要发送，移除写事件
        if (epoll_oper_event(
            pConn->fd,
            EPOLL_CTL_MOD,
            EPOLLIN | EPOLLET,
            0,
            pConn
        ) == -1) {
            logger->logSystem(LogLevel::ERROR, "Failed to remove write event");
        }
        return;
    }
    
    // 尝试发送数据
    ssize_t sendsize = send(pConn->fd, 
                           static_cast<const void*>(pConn->writeBuffer.peek()), 
                           len, 0);
    
    if (sendsize > 0) {
        // 更新写缓冲区
        pConn->writeBuffer.retrieve(sendsize);
        
        // 如果还有数据未发送完，保持写事件
        if (pConn->writeBuffer.readableBytes() > 0) {
            return;
        }
        
        // 数据发送完毕，移除写事件
        if (epoll_oper_event(
            pConn->fd,
            EPOLL_CTL_MOD,
            EPOLLIN | EPOLLET,  // 只保留读事件
            0,
            pConn
        ) == -1) {
            logger->logSystem(LogLevel::ERROR, "Failed to remove write event");
            return;
        }
        
        // 如果不是长连接，关闭连接
        if (!pConn->httpCtx->keepAlive) {
            close_connection(pConn);
        }
    }
    else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 发送缓冲区已满，等待下次写事件
        return;
    }
    else {
        // 发送错误，关闭连接
        logger->logSystem(LogLevel::ERROR, "Send error occurred");
        close_connection(pConn);
    }
    
    return;
}