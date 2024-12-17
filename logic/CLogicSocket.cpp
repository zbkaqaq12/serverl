#include "CLogicSocket.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <mutex>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <functional>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <pthread.h>

#include "macro.h"
#include "global.h"
#include "CMemory.h"
#include "CCRC32.h"

//成员指针函数
using handler = bool (CLogicSocket::*)(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);

//数组定义 成员函数指针 用来处理各种业务
static const handler statusHandler[] =
{
    //前5个元素，保留给一些基本的通讯功能
    &CLogicSocket::_HandlePing,                             //序号0，心跳包实现
    NULL,                                                   //序号1，从0开始
    NULL,                                                   //序号2，从0开始
    NULL,                                                   //序号3，从0开始
    NULL,                                                   //序号4，从0开始

    //开始具体的业务逻辑
    &CLogicSocket::_HandleRegister,                         //序号5，实现具体的注册功能
    &CLogicSocket::_HandleLogIn,                            //序号6，实现具体的登录功能
    //......其他待扩展，待实现功能如购买功能，实现加血功能等等
};
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) //整个命令有多少个，编译时即可知道

//构造函数
CLogicSocket::CLogicSocket()
{
}

//析构函数
CLogicSocket::~CLogicSocket()
{
}

//初始化函数【fork()子进程之前执行】
//成功返回true，失败返回false
bool CLogicSocket::Initialize()
{
    //做一些和本类相关的初始化工作
    //....未来可能要扩展        
    bool bParentInit = CSocket::Initialize();  //调用父类的同名函数
    return bParentInit;
}

void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode)
{
    CMemory* p_memory = CMemory::GetInstance();

    char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader, false);
    char* p_tmpbuf = p_sendbuf;

    memcpy(p_tmpbuf, pMsgHeader, m_iLenMsgHeader);
    p_tmpbuf += m_iLenMsgHeader;

    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf;	  //指向要发送出去的包的包头	
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader);
    pPkgHeader->crc32 = 0;
    msgSend(p_sendbuf);
    return;
}

bool CLogicSocket::_HandleRegister(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    //(1)首先判断包的合法性
    if (pPkgBody == NULL) //客户端发送过来的结构要求跟服务端一致【消息码+包头+包体】，如果没有包体，认为是恶意包，直接丢弃    
    {
        return false;
    }

    int iRecvLen = sizeof(STRUCT_REGISTER);
    if (iRecvLen != iBodyLength) //发送过来的结构大小不对，认为是恶意包，直接丢弃
    {
        return false;
    }

    //(2)对于同一个用户，可能同时发送来多个请求，造成多个线程同时为该 用户服务，比如以网游为例，用户要在商店A买物品，要在商店B买物品，如果用户的钱 只够买A或者B，而不够同时买A和B，
    //那如果用户发送购买命令过来，有一个A请求，有一个B请求，如果是两个线程来执行同一个用户的这两个不同的购买命令，可能造成这个用户的钱同时 A商品购买成功， B
    //所以，对于同一个用户的命令，我们一般都要互斥,所以需要增加互斥代码的变量ngx_connection_s结构中
    std::lock_guard<std::mutex> lock(pConn->logicPorcMutex);

    //(3)取得了整个发送过来的数据
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);          //所有数值型,short,int,long,uint64_t,int64_t这种大家都不要忘记传输之前需要主机序转网络序，收到后要网络序转主机序
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;//这是非常关键的操作，防止客户端发送过来畸形包，导致服务器直接使用这些数据出现错误 
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;//这是非常关键的操作，防止客户端发送过来畸形包，导致服务器直接使用这些数据出现错误 

    //(4)这里可以开始进行 业务逻辑的处理，比如通过接收到的数据，进行数据库的查询和更新等等
    //当前用户的状态是否适合收到这个数据包等等，比如如果用户没登陆，就不适合购买商品等等
    //具体怎么写，你们自己来，这里就是简单打印一些信息
    //。。。。。。。。。。。

    //(5)给客户端发送数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商，这里我们就以给客户端也返回同样的 STRUCT_REGISTER 结构来举例    
    //LPSTRUCT_REGISTER pFromPkgHeader =  (LPSTRUCT_REGISTER)(((char *)pMsgHeader)+m_iLenMsgHeader);	//指向收到的包的包头，其后是收到的包的包体
    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory* p_memory = CMemory::GetInstance();
    CCRC32* p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_REGISTER);
    //a)分配要发送出去的包的内存

    //iSendLen = 65000; //unsigned最大也就是这个值
    char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);//准备发送的格式，这里是 消息头+包头+包体
    //b)填充消息头
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);                   //消息头直接拷贝过来
    //c)填充包头
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);    //指向包头
    pPkgHeader->msgCode = _CMD_REGISTER;	                        //消息代码，统一在ngx_logiccomm.h中定义
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);	            //htons主机序转网络序 
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);        //整个包的尺寸【包头+包体尺寸】 
    //d)填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);	//跳过消息头，跳过包头，就是包体了
    //。。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转换，short类型要使用htons()转换

    //e)包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char*)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);

    //f)发送数据包
    msgSend(p_sendbuf);
    return true;
}

bool CLogicSocket::_HandleLogIn(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    if (pPkgBody == NULL)
    {
        return false;
    }
    int iRecvLen = sizeof(STRUCT_LOGIN);
    if (iRecvLen != iBodyLength)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(pConn->logicPorcMutex);

    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;

    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory* p_memory = CMemory::GetInstance();
    CCRC32* p_crc32 = CCRC32::GetInstance();

    int iSendLen = sizeof(STRUCT_LOGIN);
    char* p_sendbuf = (char*)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen = htons(m_iLenPkgHeader + iSendLen);
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char*)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);
    msgSend(p_sendbuf);
    return true;
}

bool CLogicSocket::_HandlePing(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength)
{
    //心跳包要求没有包体；
    if (iBodyLength != 0)  //有包体认为是 非法包
        return false;

    std::lock_guard<std::mutex> lock(pConn->logicPorcMutex); //凡是和本用户有关的访问都考虑用互斥，以免该用户同时发送过来两个命令达到各种目的
    pConn->lastPingTime = time(NULL);   //更新该变量

    //服务器也发送 一个只有包头的数据包给客户端，作为返回的数据
    SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);

    return true;
}

void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
    CMemory* p_memory = CMemory::GetInstance();

    if (tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence) //此连接没断
    {
        lpconnection_t p_Conn = tmpmsg->pConn;

        if (/*m_ifkickTimeCount == 1 && */m_ifTimeOutKick == 1)  //能调用到这里，第一个条件肯定成立，所以第一个条件加不加无所谓，主要是第二个条件
        {
            //到时间直接踢出去的需要
            zdClosesocketProc(p_Conn);
        }
        else if ((cur_time - p_Conn->lastPingTime) > (m_iWaitTime * 3 + 10)) //超时踢的判断标准就是 每次检查的时间间隔*3，超过这个时间没发送心跳包，就踢【大家可以根据实际情况自由设定】
        {
            //踢出去【如果此时此刻该用户正好发送了心跳包，服务器也同时处理到这里，可能会造成客户端 和 服务器之间产生心跳包通讯上的混乱，这种情况是极少的】            
            zdClosesocketProc(p_Conn);
        }

        p_memory->FreeMemory(tmpmsg);//内存要释放
    }
    else //连接断了
    {
        p_memory->FreeMemory(tmpmsg);//内存要释放
    }
    return;
}

//将收到的消息放入消息队列（线程池中的某个线程会处理）
//pMsgBuf：消息头 + 包头 + 包体 ：自解释；
void CLogicSocket::threadRecvProcFunc(char* pMsgBuf)
{
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                  //消息头
    LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + m_iLenMsgHeader); //包头
    void* pPkgBody;                                                              //指向包体的指针
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);                            //客户端指定的包长度【包头+包体】

    if (m_iLenPkgHeader == pkglen)
    {
        //没有包体，只有包头
        if (pPkgHeader->crc32 != 0) //只有包头的crc值应该为0
        {
            return; //crc错误，直接丢弃
        }
        pPkgBody = NULL;
    }
    else
    {
        //有包体，走到这里
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);		          //针对4字节的数据，网络序转主机序
        pPkgBody = (void*)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader); //跳过消息头 以及 包头 ，指向包体

        //计算crc值判断包的完整性        
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char*)pPkgBody, pkglen - m_iLenPkgHeader); //计算纯包体的crc值
        if (calccrc != pPkgHeader->crc32) //服务器端根据包体计算crc值，和客户端传递过来的包头中的crc32信息比较
        {
            globallogger->clog(LogLevel::ERROR, "CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据包!", calccrc, pPkgHeader->crc32);    //正式代码中可以干掉这个信息
            return; //crc错误，直接丢弃
        }
    }

    //包crc校验OK才能走到这里    	
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); //消息代码拿出来
    lpconnection_t p_Conn = pMsgHeader->pConn;        //消息头中藏着连接池中连接的指针

    //我们要做一些判断
    //(1)如果从收到客户端发送来的包，到服务器释放一个线程池中的线程处理该包的过程中，客户端断开了，那显然，这种收到的包我们就不必处理了    
    if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)   //连接池中连接的序号字段标记，与包头中的序号字段标记必须同时为新，同时为旧，否则认为客户端和服务器连接断了，这种包直接丢弃不理
    {
        return; //丢弃不处理了【客户端断开了】
    }

    //(2)判断消息码是正确的，防止客户端恶意侵害我们服务器，发送一个不在我们服务器处理范围内的消息码
    if (imsgCode >= AUTH_TOTAL_COMMANDS) //无符号数不可能<0
    {
        globallogger->clog(LogLevel::ERROR, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!", imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return; //丢弃不处理了【恶意包或者错误包】
    }

    //走到这里，包没过期，命令码也没问题
    //(3)判断有对应的处理函数
    if (statusHandler[imsgCode] == NULL) //这种用imsgCode的方式可以使查找要执行的成员函数效率特别高
    {
        globallogger->clog(LogLevel::ERROR, "CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!", imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;  //没有相关的处理函数
    }

    //一切正常，可以处理收到的数据了
    //(4)调用消息码对应的成员函数来处理
    (this->*statusHandler[imsgCode])(p_Conn, pMsgHeader, (char*)pPkgBody, pkglen - m_iLenPkgHeader);
    return;
}