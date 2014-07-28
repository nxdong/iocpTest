/********************************************************************
	created:	2014/07/26
	created:	26:7:2014   16:36
	filename: 	H:\iocpTest\iocpTest\IOCPServer.h
	file path:	H:\iocpTest\iocpTest
	file base:	IOCPServer
	file ext:	h
	author:		princ1ple
	
	purpose:	interface to iocp server
*********************************************************************/
#pragma once
#include "stdafx.h"
#include <MSTcpIP.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
#include "Buffer.h"

#include "zlib.h"

// This define used by users. used in notifyProc.
#define	NC_CLIENT_CONNECT		0x0001
#define	NC_CLIENT_DISCONNECT	0x0002
#define	NC_TRANSMIT				0x0003
#define	NC_RECEIVE				0x0004
#define NC_RECEIVE_COMPLETE		0x0005
// This type used for iocp server
enum IOType 
{
	IOInitialize,
	IORead,
	IOWrite,
	IOIdle
};


// This class used when need ensure one function run successfully. 
class CLock
{
public:
	CLock(CRITICAL_SECTION& cs, const CString& strFunc){
		m_strFunc = strFunc;
		m_pcs = &cs;
		Lock();
	}
	~CLock(){
		Unlock();
	}
	void Unlock(){
		LeaveCriticalSection(m_pcs);
	}
	void Lock(){
		EnterCriticalSection(m_pcs);
	}
protected:
	CRITICAL_SECTION*	m_pcs;
	CString				m_strFunc;
};
//used when need trans information with overlapped struct.
class OVERLAPPEDPLUS 
{
public:
	OVERLAPPED			m_ol;
	IOType				m_ioType;
	OVERLAPPEDPLUS(IOType ioType) {
		ZeroMemory(this, sizeof(OVERLAPPEDPLUS));
		m_ioType = ioType;
	}
};

struct ClientContext
{
	SOCKET				m_Socket;
	CBuffer				m_WriteBuffer;
	CBuffer				m_CompressionBuffer;	// 接收到的压缩的数据
	CBuffer				m_DeCompressionBuffer;	// 解压后的数据
	CBuffer				m_ResendWriteBuffer;	// 上次发送的数据包，接收失败时重发时用
	int					m_Dialog[2]; // 放对话框列表用，第一个int是类型，第二个是CDialog的地址
	int					m_nTransferProgress;
	// Input Elements for Winsock
	WSABUF				m_wsaInBuffer;
	BYTE				m_byInBuffer[8192];
	// Output elements for Winsock
	WSABUF				m_wsaOutBuffer;
	HANDLE				m_hWriteComplete;
	// Message counts... purely for example purposes
	LONG				m_nMsgIn;
	LONG				m_nMsgOut;	
	BOOL				m_bIsMainSocket; // 是不是主socket
};
#include "iocpMapper.h"
// typedef for iocp server class
typedef void (CALLBACK* NOTIFYPROC)(LPVOID, ClientContext*, UINT nCode);
typedef CList<ClientContext*, ClientContext* > ContextList;
class CMainFrame;

class CiocpTestDlg;
class CIOCPServer{
public:
	//in this part declare the variables used in server
	NOTIFYPROC				m_pNotifyProc;				//user NotifyProc pointer
	CiocpTestDlg*           m_pFrame;
	//CMainFrame*				m_pFrame;					//Frame pointer
	static CRITICAL_SECTION	m_cs;						//critical section   
	SOCKET					m_socListen;                //listen socket
	WSAEVENT				m_hEvent;					//event for accept message
	HANDLE					m_hKillEvent;				//event
	HANDLE					m_hThread;					//handle for listen thread
	HANDLE					m_hCompletionPort;          //handle for completionPort
	bool					m_bTimeToKill;				//bool type for close server
	bool					m_bDisconnectAll;			//boll,if disconnect all client
	bool					m_bInit;					//if the server was initialized
	ContextList				m_listContexts;				//list store client info
	ContextList				m_listFreePool;				//client which not use 
	BYTE					m_bPacketFlag[5];			//byte array store flag princ
	LONG					m_nKeepLiveTime;            //heart break time
	LONG					m_nCurrentThreads;			//number of current threads 
	LONG					m_nBusyThreads;				//number of busy threads
	LONG					m_nThreadPoolMin;			//max thread number
	LONG					m_nThreadPoolMax;			//min thread number
	LONG					m_nWorkerCnt;				//current worker thread
	UINT					m_nSendKbps;				//send speed
	UINT					m_nRecvKbps;				//recv speed
	UINT					m_nMaxConnections;			//max connections
public:
	CIOCPServer();
	virtual ~CIOCPServer();
	bool IsRunning();									//if the server is running
	void Shutdown();									//used in close server 
	void Stop();
	bool Initialize(NOTIFYPROC pNotifyProc, /*CMainFrame* pFrame*/CiocpTestDlg* pFrame,
					int nMaxConnections, int nPort);    //init the server
	static unsigned __stdcall ListenThreadProc(LPVOID lpVoid);
	static unsigned __stdcall ThreadPoolFunc(LPVOID WorkContext);
	bool InitializeIOCP(void);                          //initialize iocp
	void OnAccept();
	ClientContext*  AllocateContext();
	bool AssociateSocketWithCompletionPort(SOCKET device, HANDLE hCompletionPort, DWORD dwCompletionKey);
	void PostRecv(ClientContext* pContext);
	void RemoveStaleClient(ClientContext* pContext, BOOL bGraceful);
	
	void Send(ClientContext* pContext, LPBYTE lpData, UINT nSize);
	void CloseCompletionPort();
	void MoveToFreePool(ClientContext *pContext);
	void ResetConnection(ClientContext* pContext);
	void DisconnectAll();

	bool OnClientInitializing	(ClientContext* pContext, DWORD dwSize = 0);
	bool OnClientReading		(ClientContext* pContext, DWORD dwSize = 0);
	bool OnClientWriting		(ClientContext* pContext, DWORD dwSize = 0);
	BEGIN_IO_MSG_MAP()
		IO_MESSAGE_HANDLER(IORead, OnClientReading)
		IO_MESSAGE_HANDLER(IOWrite, OnClientWriting)
		IO_MESSAGE_HANDLER(IOInitialize, OnClientInitializing)
	END_IO_MSG_MAP()
};


