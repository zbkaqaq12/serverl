#include"global.h"
#include"CSocket.h"
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h>   //多线程
#include "CMemory.h"

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
    bool isflood = false; //是否flood攻击成立

    //收包，注意我们用的第二个和第三个参数，我们用的始终是这两个参数，因为我们要保证 c->precvbuf指向正确的收包位置，保证c->irecvlen指向正确的收包长度
    ssize_t reco = recvproc(pConn, pConn->precvbuf, pConn->irecvlen);
    if (reco <= 0)  
    {
        return;//该处理在recvproc()中已经处理过了，这里<=0就直接return        
    }

    //走到这里，说明成功收到了一些字节（>0），我们要开始判断收到了多少数据     
    if (pConn->curStat == _PKG_HD_INIT) //连接建立起来时肯定是这个状态，因为在get_connection()中已经把curStat成员赋值成_PKG_HD_INIT了
    {
        if (reco == static_cast<ssize_t>(m_iLenPkgHeader))//正好收到完整包头，这是我们希望的
        {
            wait_request_handler_proc_p1(pConn, isflood); //那就调用专门处理包头的函数去处理把。
        }
        else
        {
            //收到的包头不完整--我们不预期每个包的长度，也不预期收到包的内容是什么，我们只是收到包头时，包头的长度必须正确
            pConn->curStat = _PKG_HD_RECVING;                 //接收包头中，包头不完整，继续接收包头
            pConn->precvbuf = pConn->precvbuf + reco;              //注意收后续包的内存往后走
            pConn->irecvlen = pConn->irecvlen - reco;              //要收的内容当然要减少，以确保只收到完整的包头
        } //end  if(reco == m_iLenPkgHeader)
    }
    else if (pConn->curStat == _PKG_HD_RECVING) //接收包头中，包头不完整，继续接收中，这个条件才会成立
    {
        if (pConn->irecvlen == reco) //要求收到的长度和我实际收到的长度相等
        {
            //包头收完整了
            wait_request_handler_proc_p1(pConn, isflood); //那就调用专门处理包头的函数去处理把。
        }
        else
        {
            //包头还是没收完整，继续收包头
            //pConn->curStat        = _PKG_HD_RECVING;                 //没必要
            pConn->precvbuf = pConn->precvbuf + reco;              //注意收后续包的内存往后走
            pConn->irecvlen = pConn->irecvlen - reco;              //要收的内容当然要减少，以确保只收到完整的包头
        }
    }
    else if (pConn->curStat == _PKG_BD_INIT)
    {
        //包头刚好收完，准备接收包体
        if (reco == pConn->irecvlen)
        {
            //收到的宽度等于要收的宽度，包体也收完整了
            if (m_floodAkEnable == 1)
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            //收到的宽度小于要收的宽度
            pConn->curStat = _PKG_BD_RECVING;
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if (pConn->curStat == _PKG_BD_RECVING)
    {
        //接收包体中，包体不完整，继续接收中
        if (pConn->irecvlen == reco)
        {
            //包体收完整了
            if (m_floodAkEnable == 1)
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            //包体没收完整，继续收
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }  //end if(c->curStat == _PKG_HD_INIT)

    if (isflood == true)
    {
        //客户端flood服务器，直接把客户端踢掉
        //log_stderr(errno,"发现客户端flood，干掉该客户端!");
        zdClosesocketProc(pConn);
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

    n = recv(c->fd, buff, buflen, 0); //recv()系统函数，最后一个参数flag一般为0
    if (n == 0)
    {
        //客户端关闭。应该处理这种非正常的额四路挥手，后续处理就直接回收连接，关闭socket即可 
        //log_stderr(0,"连接被客户���正常关闭[4路挥手关闭]！");
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
            globallogger->flog(LogLevel::ERROR, "CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }
        //EINTR错误的产生：当阻塞于某个系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于系统调用时由父进程捕获到了一个有效信号时，内核会致使accept���回一个EINTR错误(被中断的系统调用)。
        if (errno == EINTR)  //这个不算错误，是我参考官方nginx代码写法
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            globallogger->flog(LogLevel::ERROR, "CSocket::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
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

            //....一些大家遇到的很普通的错误信息，也可以往这里��加各种，但是我觉得我们的日志都还是有限的，能够增加到这里的都是很普遍的错误，所以并不把很多杂七杂八的错误都往这里加
        }
        else
        {
            //能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            globallogger->flog(LogLevel::ERROR, "CSocket::recvproc()中发生错误，我打印出来看看是啥错误！");  //正式运营时可以考虑这些日志打印去掉
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
 * @brief 包头收完整后的处理函数
 * @details 在接收到完整的包头后，会判断包的合法性，根据数据包的长度分配内存来包体接收。如果包头有效则继续接收包体；如果包头不合法则重置连接并保持状态。
 *
 * @param pConn 当前连接
 * @param isflood 输出参数，表示Flood攻击检测
 */
void CSocket::wait_request_handler_proc_p1(lpconnection_t pConn, bool& isflood)
{
    CMemory* p_memory = CMemory::GetInstance();

    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo; //正好收到包头时，包头信息肯定是在dataHeadInfo里；

    unsigned short e_pkgLen;
    e_pkgLen = ntohs(pPkgHeader->pkgLen);  //注意这里网络序转本机序，所有传输到网络上的2字节数据，都要用htons()转成网络序，所有从网络上收到的2字节数据，都要用ntohs()转成本机序
    //ntohs/htons的目的就是保证不同操作系统之间收发数据的正确性，操作系统不同，可能整数在内存中存储的顺序不同，收到的就不对了。
    //不理解的同学，直接百度"网络字节序" "主机字节序" "c++ 大端" "c++ 小端"
    //针对二进制数据的处理判断
    if (e_pkgLen < m_iLenPkgHeader)
    {
        //伪造包/或者包错误，否则整个包长怎么可能比包头还小？（整个包长是包头+包体，就算包体为0字节，那么至少e_pkgLen == m_iLenPkgHeader）
        //状态和接收位置都要还原，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数；
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else if (e_pkgLen > (_PKG_MAX_LENGTH - 1000))   //客户端发来包居然说包长度 > 29000?肯定是恶意包
    {
        //包太大，认定非法用户，废包
        //状态和接收位置都要还原，因为有可能在其他状态比如_PKG_HD_RECVING状态调用这个函数        
        pConn->curStat = _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        //合法的包头，继续处理
        //我现在要分配内存开始收包体，因为包体长度并不是固定的，所以内存肯定要new出来；
        char* pTmpBuffer = (char*)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false); //分配内存【消息头 + 包头 + 包体】，最后参数先给false，表示内存不需要memset；        
        pConn->precvMemPointer = pTmpBuffer;  //内存开始指针

        //a)先填写消息头内容
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = pConn;
        ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence; //收到包时的连接池中连接序号记录到消息头里来，以备将来用；
        //b)再填写包头内容
        pTmpBuffer += m_iLenMsgHeader;                 //往后跳，跳过消息头，指向包头
        memcpy(pTmpBuffer, pPkgHeader, m_iLenPkgHeader); //直接把收到的包头内容原封不动的拷贝进来
        if (e_pkgLen == m_iLenPkgHeader)
        {
            //该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            //这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            if (m_floodAkEnable == 1)
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            wait_request_handler_proc_plast(pConn, isflood);
        }
        else
        {
            //开始收包体，注意我的写法
            pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }
    }  //end if(e_pkgLen < m_iLenPkgHeader) 

    return;
}

/**
 * @brief 接收到一个完整包后的处理函数
 * @details 该函数在接收到完整包后，将数据包消息放入处理线程池处理。如果检测到Flood攻击，则释放内存。处理完后，恢复连接的状态以便接收下一个包。
 *
 * @param pConn 当前连接对象
 * @param isflood 输出参数，用于指示是否检测到Flood攻击
 */
void CSocket::wait_request_handler_proc_plast(lpconnection_t pConn, bool& isflood)
{
    //把这段内存放到消息队列中来
    //int irmqc = 0;  //消息队列当前消息数量
    //inMsgRecvQueue(c->precvMemPointer,irmqc); //返回消息队列当前消息数量irmqc，是调用本函数后的消息数量
    //激发线程池中的某个线程来处理业务逻辑
    //g_threadpool.Call(irmqc);

    if (isflood == false)
    {
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); //入消息队列并触发线程处理消息
    }
    else
    {
        //对于有攻击倾向的恶人，先把他的包丢掉
        CMemory* p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn->precvMemPointer); //直接释放掉内存，根本不往消息队列入
    }

    pConn->precvMemPointer = NULL;
    pConn->curStat = _PKG_HD_INIT;     //收包状态机的状态恢复为原始态，为收下一个包做准备                    
    pConn->precvbuf = pConn->dataHeadInfo;  //设置好收包的位置
    pConn->irecvlen = m_iLenPkgHeader;  //设置好要接收数据的大小
    return;
}

/**
 * @brief 发送数据包并处理状态
 * @details 该函数用于向连接发送数据。处理各种发送结果，包括成功发送、发送缓冲区已满、对端断开连接等情况。如果发送缓冲区已满返回-1，如果对端断开返回0，如果发生其他错误返回-2。
 *
 * @param c 当前连接对象
 * @param buff 要发送的数据缓冲区
 * @param size 发送数据的大小
 * @return 返回成功发送的字节数，或表示错误的各种值：
 *         > 0: 发送成功的字节数
 *         = 0: 对端已关闭连接
 *         -1: 发送缓冲区已满（EAGAIN）
 *         -2: 发生其他错误
 */
ssize_t CSocket::sendproc(lpconnection_t c, char* buff, ssize_t size)  //ssize_t是有符号整型，在32位机器上等同于int，在64位机器上等同于long int，size_t就是无符号型的ssize_t
{
    //这里参考官方nginx函数ngx_unix_send()的写法
    ssize_t   n;

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
            //网上找资料：send=0表示超时，对方主动关闭了连接过程
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
            //参考官方的写法，打印个日志，其他啥也没干，那就是等下次for循环重新send试一次了
            globallogger->clog(LogLevel::ERROR, "CSocket::sendproc()中send()失败.");  //打印个日志看看啥时候出这个错误
            //其他不需要做什么，等下次for循环吧            
        }
        else
        {
            //走到这里表示是其他错误码，都表示错误，错误我也不断开socket，我也依然等待recv()来统一处理断开，因为我是多线程，send()也会断开，recv()也会断开，如果都处理断开会乱套
            return -2;
        }
    } //end for
}

/**
 * @brief 数据发送完毕后处理函数
 * @details 当数据可写时，epoll通知了该函数。该函数会尝试发送数据，如果发送缓冲区满则返回-1。发送成功则更新���余数据。数据发送完毕后，会从epoll中移除写事件。
 *          如果发送过程中发生错误，会记录错误日志。如果数据完全发送完毕，则通知发送者继续执行。
 *
 * @param pConn 当前连接对象，包含待发送数据的缓冲区和数据长度等信息
 */
void CSocket::write_request_handler(lpconnection_t pConn)
{
    CMemory* p_memory = CMemory::GetInstance();

    //这些代码的书写可以参考 void* CSocket::ServerSendQueueThread(void* threadData)
    ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);

    if (sendsize > 0 && sendsize != pConn->isendlen)
    {
        //没有全部发送完毕，数据只发出去了一部分，那么发送到了哪里，剩余多少，要记录下来，下次再发送
        pConn->psendbuf = pConn->psendbuf + sendsize;
        pConn->isendlen = pConn->isendlen - sendsize;
        return;
    }
    else if (sendsize == -1)
    {
        //这不太可能，可能发生了某些错误吧，打印个日志记录一下看看
        globallogger->clog(LogLevel::ERROR, "CSocket::write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志，别的先不干啥
        return;
    }

    if (sendsize > 0 && sendsize == pConn->isendlen) //成功发送完毕，这种情况是我们喜欢的
    {
        //如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；
        if (epoll_oper_event(
            pConn->fd,          //socket句柄
            EPOLL_CTL_MOD,      //事件类型，这里是修改【因为我们准备减去写通知】
            EPOLLOUT,           //标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
            1,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
            pConn               //连接池中的连接
        ) == -1)
        {
            //如果有错误，打印出来看看是啥错误，先不要想办法解决
            globallogger->clog(LogLevel::ERROR, "CSocket::write_request_handler()中epoll_oper_event()失败。");
        }

        //log_stderr(0,"CSocket::write_request_handler()中数据发送完毕，很好。"); //做个提示吧，商用时可以干掉

    }

    //能走下来的，要么数据发送完毕了，要么对端断开了，那么执行收尾工作吧；

    /* 2019.4.2注释掉，调整顺序，感觉这个顺序不太好
    //数据发送完毕，或者把需要发送的数据干掉，都说明发送完毕了��要执行一些收尾工作了
    if(sem_post(&m_semEventSendQueue)==-1)
        log_stderr(0,"CSocket::write_request_handler()中sem_post(&m_semEventSendQueue)失败.");


    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;
    --pConn->iThrowsendCount;  //这个值恢复了
    */
    //2019.4.2调整的顺序
    p_memory->FreeMemory(pConn->psendMemPointer);  //释放内存
    pConn->psendMemPointer = NULL;
    --pConn->iThrowsendCount;//这个值减减，表示出了一个发送消息队列
    if (sem_post(&m_semEventSendQueue) == -1)
        globallogger->clog(LogLevel::ERROR, "CSocket::write_request_handler()中sem_post(&m_semEventSendQueue)失败.");

    return;
}

/**
 * @brief 处理接收到的TCP消息
 * @details 该函数专门用于处理接收到的TCP消息。消息格式对讲用，通过包头以及包体内容的长度。该函数目前是一个占位函数，具体消息处理逻辑未实现。
 *
 * @param pMsgBuf 接收到的消息缓冲区
 */
void CSocket::threadRecvProcFunc(char* pMsgBuf)
{
    return;
}