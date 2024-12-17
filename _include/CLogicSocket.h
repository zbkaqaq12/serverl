#pragma once
#include"CSocket.h"
#include"logiccomm.h"

class CLogicSocket :public CSocket
{
public:
	CLogicSocket();                                                         //构造函数
	virtual ~CLogicSocket();                                                //析构函数
	virtual bool Initialize();                                              //初始化函数

public:

	//通信相关函数
	void  SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned short iMsgCode);

	//业务逻辑相关函数
	bool _HandleRegister(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);
	bool _HandleLogIn(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);
	bool _HandlePing(lpconnection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char* pPkgBody, unsigned short iBodyLength);

	virtual void procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time);      //心跳包检测

public:
	virtual void threadRecvProcFunc(char* pMsgBuf);
};

