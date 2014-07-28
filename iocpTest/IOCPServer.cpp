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
bool CIOCPServer::Initialize(NOTIFYPROC pNotifyProc,/* CMainFrame*/CiocpTestDlg* pFrame, int nMaxConnections, int nPort)
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

//************************************
// Method:    ListenThreadProc
// FullName:  CIOCPServer::ListenThreadProc
// Access:    public static 
// Returns:   unsigned
// Qualifier: ListenThreadProc
// Parameter: LPVOID lParam
//************************************
unsigned CIOCPServer::ListenThreadProc(LPVOID lParam)
{
	CIOCPServer* pThis = reinterpret_cast<CIOCPServer*>(lParam);
	WSANETWORKEVENTS events;
	while(1)
	{
		// Wait for something to happen
		if (WaitForSingleObject(pThis->m_hKillEvent, 100) == WAIT_OBJECT_0)
			break;
		DWORD dwRet;
		dwRet = WSAWaitForMultipleEvents(1,
			&pThis->m_hEvent,
			FALSE,
			100,
			FALSE);
		if (dwRet == WSA_WAIT_TIMEOUT)
			continue;
		// Figure out what happened
		int nRet = WSAEnumNetworkEvents(pThis->m_socListen,
			pThis->m_hEvent,
			&events);
		if (nRet == SOCKET_ERROR)
			break;
		// Handle Network events //
		// ACCEPT
		if (events.lNetworkEvents & FD_ACCEPT)
		{
			if (events.iErrorCode[FD_ACCEPT_BIT] == 0)
				pThis->OnAccept();
			else
				break;
		}

	} // while....
	return 0; 
}
//************************************
// Method:    OnAccept
// FullName:  CIOCPServer::OnAccept
// Access:    public 
// Returns:   void
// Qualifier:
//************************************
void CIOCPServer::OnAccept()
{
	SOCKADDR_IN	SockAddr;
	SOCKET		clientSocket;
	int			nRet;
	int			nLen;
	if (m_bTimeToKill || m_bDisconnectAll)
		return;
	// accept the new socket descriptor
	nLen = sizeof(SOCKADDR_IN);
	clientSocket = accept(m_socListen,
		(LPSOCKADDR)&SockAddr,
		&nLen); 
	if (clientSocket == SOCKET_ERROR)
	{
		nRet = WSAGetLastError();
		if (nRet != WSAEWOULDBLOCK)
			return;
	}
	// Create the Client context to be associated with the completion port
	ClientContext* pContext = AllocateContext();
	if (pContext == NULL)
		return;
	pContext->m_Socket = clientSocket;
	// Fix up In Buffer
	pContext->m_wsaInBuffer.buf = (char*)pContext->m_byInBuffer;
	pContext->m_wsaInBuffer.len = sizeof(pContext->m_byInBuffer);
	// Associate the new socket with a completion port.
	if (!AssociateSocketWithCompletionPort(clientSocket, m_hCompletionPort, (DWORD) pContext))
	{
		delete pContext;
		pContext = NULL;
		closesocket( clientSocket );
		closesocket( m_socListen );
		return;
	}
	const char chOpt = 1;
	// Set KeepAlive 开启保活机制
	if (setsockopt(pContext->m_Socket, SOL_SOCKET, SO_KEEPALIVE, (char *)&chOpt, sizeof(chOpt)) != 0)
		return;
	tcp_keepalive	klive;
	klive.onoff = 1; // 启用保活
	klive.keepalivetime = m_nKeepLiveTime;
	klive.keepaliveinterval = 1000 * 10; // 重试间隔为10秒 Resend if No-Reply
	WSAIoctl(
		pContext->m_Socket, 
		SIO_KEEPALIVE_VALS,
		&klive,
		sizeof(tcp_keepalive),
		NULL,0,
		(unsigned long *)&chOpt,
		0,NULL);
	CLock cs(m_cs,_T("OnAccept"));
	// Hold a reference to the context
	m_listContexts.AddTail(pContext);
	// IO_MESSAGE_HANDLER
	OVERLAPPEDPLUS	*pOverlap = new OVERLAPPEDPLUS(IOInitialize);
	BOOL bSuccess = PostQueuedCompletionStatus(m_hCompletionPort, 0, (DWORD) pContext, &pOverlap->m_ol);
	if ( (!bSuccess && GetLastError( ) != ERROR_IO_PENDING))
	{
		RemoveStaleClient(pContext,TRUE);
		return;
	}
	m_pNotifyProc((LPVOID) m_pFrame, pContext, NC_CLIENT_CONNECT);
	// Post to WSARecv Next
	PostRecv(pContext);
}
//************************************
// Method:    InitializeIOCP
// FullName:  CIOCPServer::InitializeIOCP
// Access:    public 
// Returns:   bool
// Qualifier: Create a completion port. start thread .
// Parameter: void
//************************************
bool CIOCPServer::InitializeIOCP(void)
{
	DWORD i;
	UINT  nThreadID;
	SYSTEM_INFO systemInfo;
	// Create the completion port
	m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0 );
	if ( m_hCompletionPort == NULL ) 
		return false;
	GetSystemInfo( &systemInfo );
	m_nThreadPoolMin  = systemInfo.dwNumberOfProcessors * HUERISTIC_VALUE;
	m_nThreadPoolMax  = m_nThreadPoolMin;
	UINT nWorkerCnt = systemInfo.dwNumberOfProcessors * HUERISTIC_VALUE;
	HANDLE hWorker;					//save the Handles for Later Termination...
	m_nWorkerCnt = 0;
	for ( i = 0; i < nWorkerCnt; i++ ) 
	{
		hWorker = (HANDLE)_beginthreadex(NULL,					// Security
			0,						// Stack size - use default
			ThreadPoolFunc,     	// Thread function entry point
			(void*) this,			// Param for thread
			0,						// Init flag
			&nThreadID);			// Thread address
		if (hWorker == NULL ) 
		{
			CloseHandle( m_hCompletionPort );
			return false;
		}
		m_nWorkerCnt++;
		CloseHandle(hWorker);
	}
	return true;
} 
//************************************
// Method:    ThreadPoolFunc
// FullName:  CIOCPServer::ThreadPoolFunc
// Access:    public static 
// Returns:   unsigned
// Qualifier: main worker routine for the worker threads
// Parameter: LPVOID thisContext
//************************************
unsigned CIOCPServer::ThreadPoolFunc (LPVOID thisContext)    
{
	ULONG ulFlags = MSG_PARTIAL;
	CIOCPServer* pThis = reinterpret_cast<CIOCPServer*>(thisContext);
	ASSERT(pThis);
	HANDLE hCompletionPort = pThis->m_hCompletionPort;
	DWORD dwIoSize;
	LPOVERLAPPED lpOverlapped;
	ClientContext* lpClientContext;
	OVERLAPPEDPLUS*	pOverlapPlus;
	bool			bError;
	bool			bEnterRead;

	InterlockedIncrement(&pThis->m_nCurrentThreads);
	InterlockedIncrement(&pThis->m_nBusyThreads);

	// Loop round and round servicing I/O completions.
	for (BOOL bStayInPool = TRUE; bStayInPool && pThis->m_bTimeToKill == false; ) 
	{
		pOverlapPlus	= NULL;
		lpClientContext = NULL;
		bError			= false;
		bEnterRead		= false;
	
		InterlockedDecrement(&pThis->m_nBusyThreads);
		// Get a completed IO request.
		BOOL bIORet = GetQueuedCompletionStatus(
			hCompletionPort,
			&dwIoSize,
			(LPDWORD) &lpClientContext,
			&lpOverlapped, INFINITE);

		DWORD dwIOError = GetLastError();
		pOverlapPlus = CONTAINING_RECORD(lpOverlapped, OVERLAPPEDPLUS, m_ol);

		int nBusyThreads = InterlockedIncrement(&pThis->m_nBusyThreads);

		if (!bIORet && dwIOError != WAIT_TIMEOUT )
		{
			if (lpClientContext && pThis->m_bTimeToKill == false)
			{
				pThis->RemoveStaleClient(lpClientContext, FALSE);
			}
			continue;
			// anyway, this was an error and we should exit
			bError = true;
		}

		if (!bError) 
		{
			// Thread timed out - IDLE?
			if (!bIORet && dwIOError == WAIT_TIMEOUT)
			{
				if (lpClientContext == NULL)
				{
					// Thread has no outstanding IO - Server hasn't much to do so die
					if (pThis->m_nCurrentThreads > pThis->m_nThreadPoolMin)
						bStayInPool =  FALSE;
					bError = true;
				}
			}
		}

		if (!bError)
		{
			if(bIORet && NULL != pOverlapPlus && NULL != lpClientContext) 
			{
				try
				{
					pThis->ProcessIOMessage(pOverlapPlus->m_ioType, lpClientContext, dwIoSize);
				}
				catch (...) {}
			}
		}
		if(pOverlapPlus)
			delete pOverlapPlus; // from previous call
	}
	InterlockedDecrement(&pThis->m_nWorkerCnt);
	InterlockedDecrement(&pThis->m_nCurrentThreads);
	InterlockedDecrement(&pThis->m_nBusyThreads);
	return 0;
} 
//************************************
// Method:    Stop
// FullName:  CIOCPServer::Stop
// Access:    public 
// Returns:   void
// Qualifier: Signal the listener to quit his thread
//************************************
void CIOCPServer::Stop()
{
	::SetEvent(m_hKillEvent);
	WaitForSingleObject(m_hThread, INFINITE);
	CloseHandle(m_hThread);
	CloseHandle(m_hKillEvent);
}
//************************************
// Method:    PostRecv
// FullName:  CIOCPServer::PostRecv
// Access:    public 
// Returns:   void
// Qualifier: recv message 
// Parameter: ClientContext * pContext
//************************************
void CIOCPServer::PostRecv(ClientContext* pContext)
{
	// issue a read request 
	OVERLAPPEDPLUS * pOverlap = new OVERLAPPEDPLUS(IORead);
	ULONG			ulFlags = MSG_PARTIAL;
	DWORD			dwNumberOfBytesRecvd;
	UINT nRetVal = WSARecv(pContext->m_Socket, 
		&pContext->m_wsaInBuffer,
		1,
		&dwNumberOfBytesRecvd, 
		&ulFlags,
		&pOverlap->m_ol, 
		NULL);
	if ( nRetVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) 
	{
		RemoveStaleClient(pContext, FALSE);
	}
}

//************************************
// Method:    Send
// FullName:  CIOCPServer::Send
// Access:    public 
// Returns:   void
// Qualifier: send message
// Parameter: ClientContext * pContext
// Parameter: LPBYTE lpData
// Parameter: UINT nSize
//************************************
void CIOCPServer::Send(ClientContext* pContext, LPBYTE lpData, UINT nSize)
{
	if (pContext == NULL)
		return;
	try
	{
		if (nSize > 0)
		{
			// Compress data
			unsigned long	destLen = (double)nSize * 1.001  + 12;
			LPBYTE			pDest = new BYTE[destLen];
			int	nRet = compress(pDest, &destLen, lpData, nSize);
			if (nRet != Z_OK)
			{
				delete [] pDest;
				return;
			}
			LONG nBufLen = destLen + HDR_SIZE;
			// 5 bytes packet flag
			pContext->m_WriteBuffer.Write(m_bPacketFlag, sizeof(m_bPacketFlag));
			// 4 byte header [Size of Entire Packet]
			pContext->m_WriteBuffer.Write((PBYTE) &nBufLen, sizeof(nBufLen));
			// 4 byte header [Size of UnCompress Entire Packet]
			pContext->m_WriteBuffer.Write((PBYTE) &nSize, sizeof(nSize));
			// Write Data
			pContext->m_WriteBuffer.Write(pDest, destLen);
			delete [] pDest;
			// 发送完后，再备份数据, 因为有可能是m_ResendWriteBuffer本身在发送,所以不直接写入
			LPBYTE lpResendWriteBuffer = new BYTE[nSize];
			CopyMemory(lpResendWriteBuffer, lpData, nSize);
			pContext->m_ResendWriteBuffer.ClearBuffer();
			pContext->m_ResendWriteBuffer.Write(lpResendWriteBuffer, nSize);	// 备份发送的数据
			delete [] lpResendWriteBuffer;
		}
		else // 要求重发
		{
			pContext->m_WriteBuffer.Write(m_bPacketFlag, sizeof(m_bPacketFlag));
			pContext->m_ResendWriteBuffer.ClearBuffer();
			pContext->m_ResendWriteBuffer.Write(m_bPacketFlag, sizeof(m_bPacketFlag));	// 备份发送的数据	
		}
		// Wait for Data Ready signal to become available
		WaitForSingleObject(pContext->m_hWriteComplete, INFINITE);
		//  Prepare Packet
		//	pContext->m_wsaOutBuffer.buf = (CHAR*) new BYTE[nSize];
		//	pContext->m_wsaOutBuffer.len = pContext->m_WriteBuffer.GetBufferLen();
		OVERLAPPEDPLUS * pOverlap = new OVERLAPPEDPLUS(IOWrite);
		PostQueuedCompletionStatus(m_hCompletionPort, 0, (DWORD) pContext, &pOverlap->m_ol);
		pContext->m_nMsgOut++;
	}catch(...){}
}
//************************************
// Method:    OnClientInitializing
// FullName:  CIOCPServer::OnClientInitializing
// Access:    public 
// Returns:   bool
// Qualifier: sign for reuse
// Parameter: ClientContext * pContext
// Parameter: DWORD dwIoSize
//************************************
bool CIOCPServer::OnClientInitializing(ClientContext* pContext, DWORD dwIoSize)
{
	return true;		
}
//************************************
// Method:    OnClientReading
// FullName:  CIOCPServer::OnClientReading
// Access:    public 
// Returns:   bool
// Qualifier: handle client reading
// Parameter: ClientContext * pContext
// Parameter: DWORD dwIoSize
//************************************
bool CIOCPServer::OnClientReading(ClientContext* pContext, DWORD dwIoSize)
{
	CLock cs(CIOCPServer::m_cs, _T("OnClientReading"));
	try
	{
		//////////////////////////////////////////////////////////////////////////
		static DWORD nLastTick = GetTickCount();
		static DWORD nBytes = 0;
		nBytes += dwIoSize;
		if (GetTickCount() - nLastTick >= 1000)
		{
			nLastTick = GetTickCount();
			InterlockedExchange((LPLONG)&(m_nRecvKbps), nBytes);
			nBytes = 0;
		}
		if (dwIoSize == 0)
		{
			RemoveStaleClient(pContext, FALSE);
			return false;
		}
		if (dwIoSize == FLAG_SIZE && memcmp(pContext->m_byInBuffer, m_bPacketFlag, FLAG_SIZE) == 0)
		{
			// 重新发送
			Send(pContext, pContext->m_ResendWriteBuffer.GetBuffer(), pContext->m_ResendWriteBuffer.GetBufferLen());
			PostRecv(pContext);
			return true;
		}
		// Add the message to out message
		// Don't forget there could be a partial, 1, 1 or more + partial messages
		pContext->m_CompressionBuffer.Write(pContext->m_byInBuffer,dwIoSize);

		m_pNotifyProc((LPVOID) m_pFrame, pContext, NC_RECEIVE);
		// Check real Data
		while (pContext->m_CompressionBuffer.GetBufferLen() > HDR_SIZE)
		{
			BYTE bPacketFlag[FLAG_SIZE];
			CopyMemory(bPacketFlag, pContext->m_CompressionBuffer.GetBuffer(), sizeof(bPacketFlag));

			if (memcmp(m_bPacketFlag, bPacketFlag, sizeof(m_bPacketFlag)) != 0)
				throw "bad buffer";

			int nSize = 0;
			CopyMemory(&nSize, pContext->m_CompressionBuffer.GetBuffer(FLAG_SIZE), sizeof(int));

			// Update Process Variable
			pContext->m_nTransferProgress = pContext->m_CompressionBuffer.GetBufferLen() * 100 / nSize;

			if (nSize && (pContext->m_CompressionBuffer.GetBufferLen()) >= nSize)
			{
				int nUnCompressLength = 0;
				// Read off header
				pContext->m_CompressionBuffer.Read((PBYTE) bPacketFlag, sizeof(bPacketFlag));
				pContext->m_CompressionBuffer.Read((PBYTE) &nSize, sizeof(int));
				pContext->m_CompressionBuffer.Read((PBYTE) &nUnCompressLength, sizeof(int));

				////////////////////////////////////////////////////////
				// SO you would process your data here
				// I'm just going to post message so we can see the data
				int	nCompressLength = nSize - HDR_SIZE;
				PBYTE pData = new BYTE[nCompressLength];
				PBYTE pDeCompressionData = new BYTE[nUnCompressLength];
				if (pData == NULL || pDeCompressionData == NULL)
					throw "bad Allocate";
				pContext->m_CompressionBuffer.Read(pData, nCompressLength);
				/////////////////////////////////////////////////////////
				unsigned long	destLen = nUnCompressLength;
				int	nRet = uncompress(pDeCompressionData, &destLen, pData, nCompressLength);
				/////////////////////////////////////////////////////////
				if (nRet == Z_OK)
				{
					pContext->m_DeCompressionBuffer.ClearBuffer();
					pContext->m_DeCompressionBuffer.Write(pDeCompressionData, destLen);
					m_pNotifyProc((LPVOID) m_pFrame, pContext, NC_RECEIVE_COMPLETE);
				}
				else
				{
					throw "bad buffer";
				}

				delete [] pData;
				delete [] pDeCompressionData;
				pContext->m_nMsgIn++;
			}
			else
				break;
		}
		// Post to WSARecv Next
		PostRecv(pContext);
	}catch(...)
	{
		pContext->m_CompressionBuffer.ClearBuffer();
		// 要求重发，就发送0, 内核自动添加数包标志
		Send(pContext, NULL, 0);
		PostRecv(pContext);
	}
	return true;
}
//************************************
// Method:    OnClientWriting
// FullName:  CIOCPServer::OnClientWriting
// Access:    public 
// Returns:   bool
// Qualifier: Called when client is writing
// Parameter: ClientContext * pContext
// Parameter: DWORD dwIoSize
//************************************
bool CIOCPServer::OnClientWriting(ClientContext* pContext, DWORD dwIoSize)
{
	try
	{
		static DWORD nLastTick = GetTickCount();
		static DWORD nBytes = 0;
		nBytes += dwIoSize;
		if (GetTickCount() - nLastTick >= 1000)
		{
			nLastTick = GetTickCount();
			InterlockedExchange((LPLONG)&(m_nSendKbps), nBytes);
			nBytes = 0;
		}
		//////////////////////////////////////////////////////////////////////////
		ULONG ulFlags = MSG_PARTIAL;
		// Finished writing - tidy up
		pContext->m_WriteBuffer.Delete(dwIoSize);
		if (pContext->m_WriteBuffer.GetBufferLen() == 0)
		{
			pContext->m_WriteBuffer.ClearBuffer();
			// Write complete
			SetEvent(pContext->m_hWriteComplete);
			return true;			// issue new read after this one
		}
		else
		{
			OVERLAPPEDPLUS * pOverlap = new OVERLAPPEDPLUS(IOWrite);

			m_pNotifyProc((LPVOID) m_pFrame, pContext, NC_TRANSMIT);

			pContext->m_wsaOutBuffer.buf = (char*) pContext->m_WriteBuffer.GetBuffer();
			pContext->m_wsaOutBuffer.len = pContext->m_WriteBuffer.GetBufferLen();

			int nRetVal = WSASend(pContext->m_Socket, 
				&pContext->m_wsaOutBuffer,
				1,
				&pContext->m_wsaOutBuffer.len, 
				ulFlags,
				&pOverlap->m_ol, 
				NULL);
			if ( nRetVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING )
			{
				RemoveStaleClient( pContext, FALSE );
			}
		}
	}catch(...){}
	return false;			// issue new read after this one
}
//************************************
// Method:    CloseCompletionPort
// FullName:  CIOCPServer::CloseCompletionPort
// Access:    public 
// Returns:   void
// Qualifier: Close down the IO Complete Port
//************************************
void CIOCPServer::CloseCompletionPort()
{

	while (m_nWorkerCnt)
	{
		PostQueuedCompletionStatus(m_hCompletionPort, 0, (DWORD) NULL, NULL);
		Sleep(100);
	}

	// Close the CompletionPort and stop any more requests
	CloseHandle(m_hCompletionPort);

	ClientContext* pContext = NULL;

	do 
	{
		POSITION pos  = m_listContexts.GetHeadPosition();
		if (pos)
		{
			pContext = m_listContexts.GetNext(pos);			
			RemoveStaleClient(pContext, FALSE);
		}
	}
	while (!m_listContexts.IsEmpty());

	m_listContexts.RemoveAll();

}
//************************************
// Method:    AssociateSocketWithCompletionPort
// FullName:  CIOCPServer::AssociateSocketWithCompletionPort
// Access:    public 
// Returns:   bool
// Qualifier: 
// Parameter: SOCKET socket
// Parameter: HANDLE hCompletionPort
// Parameter: DWORD dwCompletionKey
//************************************
bool CIOCPServer::AssociateSocketWithCompletionPort(SOCKET socket, HANDLE hCompletionPort, DWORD dwCompletionKey)
{
	HANDLE h = CreateIoCompletionPort((HANDLE) socket, hCompletionPort, dwCompletionKey, 0);
	return h == hCompletionPort;
}
//************************************
// Method:    RemoveStaleClient
// FullName:  CIOCPServer::RemoveStaleClient
// Access:    public 
// Returns:   void
// Qualifier: close socket and remove context from our list
// Parameter: ClientContext * pContext
// Parameter: BOOL bGraceful
//************************************
void CIOCPServer::RemoveStaleClient(ClientContext* pContext, BOOL bGraceful)
{
	CLock cs(m_cs, _T("RemoveStaleClient"));
	TRACE("CIOCPServer::RemoveStaleClient\n");
	LINGER lingerStruct;
	// If we're supposed to abort the connection, set the linger value
	// on the socket to 0.
	if ( !bGraceful ) 
	{
		lingerStruct.l_onoff = 1;
		lingerStruct.l_linger = 0;
		setsockopt( pContext->m_Socket, SOL_SOCKET, SO_LINGER,
			(char *)&lingerStruct, sizeof(lingerStruct) );
	}
	// Free context structures
	if (m_listContexts.Find(pContext)) 
	{
		// Now close the socket handle.  This will do an abortive or  graceful close, as requested.  
		CancelIo((HANDLE) pContext->m_Socket);

		closesocket( pContext->m_Socket );
		pContext->m_Socket = INVALID_SOCKET;

		while (!HasOverlappedIoCompleted((LPOVERLAPPED)pContext)) 
			Sleep(0);

		m_pNotifyProc((LPVOID) m_pFrame, pContext, NC_CLIENT_DISCONNECT);
		MoveToFreePool(pContext);
	}
}


//************************************
// Method:    Shutdown
// FullName:  CIOCPServer::Shutdown
// Access:    public 
// Returns:   void
// Qualifier: close the server
//************************************
void CIOCPServer::Shutdown()
{
	if (m_bInit == false)
		return;
	m_bInit = false;
	m_bTimeToKill = true;
	// Stop the listener
	Stop();
	closesocket(m_socListen);	
	WSACloseEvent(m_hEvent);
	CloseCompletionPort();
	DeleteCriticalSection(&m_cs);
	while (!m_listFreePool.IsEmpty())
		delete m_listFreePool.RemoveTail();
}

//************************************
// Method:    MoveToFreePool
// FullName:  CIOCPServer::MoveToFreePool
// Access:    public 
// Returns:   void
// Qualifier: Moves an 'used/stale' Context to the free pool for reuse
// Parameter: ClientContext * pContext
//************************************
void CIOCPServer::MoveToFreePool(ClientContext *pContext)
{
	CLock cs(m_cs, _T("MoveToFreePool"));
	// Free context structures
	POSITION pos = m_listContexts.Find(pContext);
	if (pos) 
	{
		pContext->m_CompressionBuffer.ClearBuffer();
		pContext->m_WriteBuffer.ClearBuffer();
		pContext->m_DeCompressionBuffer.ClearBuffer();
		pContext->m_ResendWriteBuffer.ClearBuffer();
		m_listFreePool.AddTail(pContext);
		m_listContexts.RemoveAt(pos);
	}
}
//************************************
// Method:    AllocateContext
// FullName:  CIOCPServer::AllocateContext
// Access:    public 
// Returns:   ClientContext*
// Qualifier: Checks free pool otherwise allocates a context
//************************************
ClientContext*  CIOCPServer::AllocateContext()
{
	ClientContext* pContext = NULL;
	CLock cs(CIOCPServer::m_cs, _T("AllocateContext"));
	if (!m_listFreePool.IsEmpty())
	{
		pContext = m_listFreePool.RemoveHead();
	}
	else
	{
		pContext = new ClientContext;
	}
	ASSERT(pContext);
	if (pContext != NULL)
	{
		ZeroMemory(pContext, sizeof(ClientContext));
		pContext->m_bIsMainSocket = false;
		memset(pContext->m_Dialog, 0, sizeof(pContext->m_Dialog));
	}
	return pContext;
}

//************************************
// Method:    ResetConnection
// FullName:  CIOCPServer::ResetConnection
// Access:    public 
// Returns:   void
// Qualifier: 
// Parameter: ClientContext * pContext
//************************************
void CIOCPServer::ResetConnection(ClientContext* pContext)
{
	CString strHost;
	ClientContext* pCompContext = NULL;
	CLock cs(CIOCPServer::m_cs, _T("ResetConnection"));
	POSITION pos  = m_listContexts.GetHeadPosition();
	while (pos)
	{
		pCompContext = m_listContexts.GetNext(pos);			
		if (pCompContext == pContext)
		{
			RemoveStaleClient(pContext, TRUE);
			break;
		}
	}
}
//************************************
// Method:    DisconnectAll
// FullName:  CIOCPServer::DisconnectAll
// Access:    public 
// Returns:   void
// Qualifier: 
//************************************
void CIOCPServer::DisconnectAll()
{
	m_bDisconnectAll = true;
	CString strHost;
	ClientContext* pContext = NULL;
	CLock cs(CIOCPServer::m_cs, _T("DisconnectAll"));
	POSITION pos  = m_listContexts.GetHeadPosition();
	while (pos)
	{
		pContext = m_listContexts.GetNext(pos);			
		RemoveStaleClient(pContext, TRUE);
	}
	m_bDisconnectAll = false;
}
//************************************
// Method:    IsRunning
// FullName:  CIOCPServer::IsRunning
// Access:    public 
// Returns:   bool
// Qualifier:
//************************************
bool CIOCPServer::IsRunning()
{
	return m_bInit;
}
