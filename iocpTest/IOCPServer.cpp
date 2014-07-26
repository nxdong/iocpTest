#include "stdafx.h"
#include "IOCPServer.h"
// 'P' 'r' 'i' 'n' 'c' | PacketLen | UnZipLen
#define HDR_SIZE	13             //packet header size
#define FLAG_SIZE	5			   //Flag size . p r i n c
#define HUERISTIC_VALUE 2		   // process * HUERISTIC_VALUE

// initialize the critical section of CIOCPServer
CRITICAL_SECTION CIOCPServer::m_cs;

//************************************
// Method:    CIOCPServer
// FullName:  CIOCPServer::CIOCPServer
// Access:    public 
// Returns:   
// Qualifier: 
//************************************
CIOCPServer::CIOCPServer()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,2), &wsaData);

	InitializeCriticalSection(&m_cs);

	m_hThread			= NULL;
	m_hKillEvent		= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_socListen			= NULL;

	m_bTimeToKill		= false;
	m_bDisconnectAll	= false;

	m_hEvent			= NULL;
	m_hCompletionPort	= NULL;

	m_bInit				=false;
	m_nCurrentThreads	= 0;
	m_nBusyThreads		= 0;

	m_nSendKbps			= 0;
	m_nRecvKbps			= 0;

	m_nMaxConnections	= 10000;
	m_nKeepLiveTime		= 1000 * 60 * 3; 
	BYTE bPacketFlag[]	= {'p', 'r', 'i', 'n', 'c'};
	memcpy(m_bPacketFlag, bPacketFlag, sizeof(bPacketFlag));
}
//************************************
// Method:    ~CIOCPServer
// FullName:  CIOCPServer::~CIOCPServer
// Access:    virtual public 
// Returns:   
// Qualifier:
//************************************
CIOCPServer::~CIOCPServer()
{
	try
	{
		Shutdown();
		WSACleanup();
	}catch(...){}
}
//************************************
// Method:    Initialize
// FullName:  CIOCPServer::Initialize
// Access:    public 
// Returns:   bool
// Qualifier: users can use this function to init iocp server
// Parameter: NOTIFYPROC pNotifyProc
// Parameter: CMainFrame * pFrame
// Parameter: int nMaxConnections
// Parameter: int nPort
//************************************
bool CIOCPServer::Initialize(NOTIFYPROC pNotifyProc, CMainFrame* pFrame, int nMaxConnections, int nPort)
{
	m_pNotifyProc		= pNotifyProc;
	m_pFrame			= pFrame;
	m_nMaxConnections	= nMaxConnections;

	m_socListen = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (m_socListen == INVALID_SOCKET)		
		return false;

	m_hEvent = WSACreateEvent();			// Event for handling Network IO

	if (m_hEvent == WSA_INVALID_EVENT)
	{
		closesocket(m_socListen);
		return false;
	}
	// The listener is ONLY interested in FD_ACCEPT
	// That is when a client connects to or IP/Port
	// Request asynchronous notification
	int nRet = WSAEventSelect(m_socListen,
								m_hEvent,
								FD_ACCEPT);
	if (nRet == SOCKET_ERROR)
	{
		closesocket(m_socListen);
		return false;
	}
	// bind our name to the socket
	SOCKADDR_IN		saServer;		
	saServer.sin_port = htons(nPort);
	saServer.sin_family = AF_INET;
	saServer.sin_addr.s_addr = INADDR_ANY;
	nRet = bind(m_socListen, 
		(LPSOCKADDR)&saServer, 
		sizeof(struct sockaddr));
	if (nRet == SOCKET_ERROR)
	{
		closesocket(m_socListen);
		return false;
	}
	// Set the socket to listen
	nRet = listen(m_socListen, SOMAXCONN);
	if (nRet == SOCKET_ERROR)
	{
		closesocket(m_socListen);
		return false;
	}
	
	// Start a thread processing listen event
	UINT	dwThreadId = 0;
	m_hThread =
		(HANDLE)_beginthreadex(NULL,		// security
		0,									// default stack size
		ListenThreadProc,					// ThreadProc entry point
		(void*) this,						// parameter
		0,									// Init flag
		&dwThreadId);						// thread address
	if (m_hThread != INVALID_HANDLE_VALUE)
	{
		InitializeIOCP();
		m_bInit = true;
		return true;
	}
	return false;
}

