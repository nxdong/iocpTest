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
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
#include "Buffer.h"

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
// typedef for iocp server class
typedef void (CALLBACK* NOTIFYPROC)(LPVOID, ClientContext*, UINT nCode);
typedef CList<ClientContext*, ClientContext* > ContextList;
class CMainFrame;

class CIOCPServer{
public:
	CIOCPServer();
	virtual ~CIOCPServer();
};