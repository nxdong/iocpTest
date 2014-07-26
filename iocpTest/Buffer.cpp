/********************************************************************
	created:	2014/07/26
	created:	26:7:2014   8:59
	filename: 	iocpTest\Buffer.cpp
	file path:	iocpTest\iocpTest
	file base:	Buffer
	file ext:	cpp
	author:		princ1ple
	
	purpose:	implementation of the CBuffer class
*********************************************************************/
#include "stdafx.h"
#include "Buffer.h"

//************************************
// Method:    CBuffer
// FullName:  CBuffer::CBuffer
// Access:    public 
// Returns:   
// Qualifier:
//************************************
CBuffer::CBuffer(){
	m_nSize = 0;
	m_pBase = NULL;
	m_pPtr  = NULL;
}
//************************************
// Method:    ~CBuffer
// FullName:  CBuffer::~CBuffer
// Access:    virtual public 
// Returns:   
// Qualifier:
//************************************
CBuffer::~CBuffer(){
	if (m_pBase)
		VirtualFree(m_pBase,0,MEM_RELEASE);
}
//************************************
// Method:    Write
// FullName:  CBuffer::Write
// Access:    public 
// Returns:   UINT
// Qualifier:
// Parameter: PBYTE pData
// Parameter: UINT nSize
//************************************
UINT CBuffer::Write(PBYTE pData,UINT nSize){
	ReAllocateBuffer(nSize + GetBufferLen());
	CopyMemory(m_pPtr,pData,nSize);
	m_pPtr += nSize;
	return nSize;
}
UINT CBuffer::Write(CString& strData)
{
	int nSize = strData.GetLength();
	return Write((PBYTE) strData.GetBuffer(nSize), nSize);
}
//************************************
// Method:    Insert
// FullName:  CBuffer::Insert
// Access:    public 
// Returns:   UINT
// Qualifier: 
// Parameter: PBYTE pData
// Parameter: UINT nSize
//************************************
UINT CBuffer::Insert(PBYTE pData, UINT nSize){
	ReAllocateBuffer(nSize + GetBufferLen());
	MoveMemory(m_pBase+nSize,m_pBase,GetMemSize() - nSize);
	CopyMemory(m_pBase,pData,nSize);
	m_pPtr+=nSize;
	return nSize;
}
UINT CBuffer::Insert(CString& strData)
{
	int nSize = strData.GetLength();
	return Insert((PBYTE) strData.GetBuffer(nSize), nSize);
}

//************************************
// Method:    Read
// FullName:  CBuffer::Read
// Access:    public 
// Returns:   UINT
// Qualifier: Read data from the buffer and delete what it reads
// Parameter: PBYTE pData
// Parameter: UINT nSize
//************************************
UINT CBuffer::Read(PBYTE pData, UINT nSize){
	if (nSize > GetMemSize())      //GetMemSize return m_nSize,memory size
		return 0;             
	if (nSize > GetBufferLen())    //GetBufferlen return real buffer size
		nSize = GetBufferLen();
	if (nSize)
	{
		CopyMemory(pData,m_pBase,nSize);
		MoveMemory(m_pBase,m_pBase+nSize,GetMemSize() - nSize);
		m_pPtr -= nSize;
	}
	DeAllocateBuffer(GetBufferLen());
	return nSize;
}
//************************************
// Method:    GetMemSize
// FullName:  CBuffer::GetMemSize
// Access:    private 
// Returns:   UINT
// Qualifier: Returns the physical memory allocated to the buffer
//************************************
UINT CBuffer::GetMemSize(){
	return m_nSize;
}
//************************************
// Method:    GetBufferLen
// FullName:  CBuffer::GetBufferLen
// Access:    public 
// Returns:   UINT
// Qualifier: Get the buffer 'data' length
//************************************
UINT CBuffer::GetBufferLen() {
	if (m_pBase == NULL)
		return 0;
	return (m_pPtr - m_pBase);
}
//************************************
// Method:    ReAllocateBuffer
// FullName:  CBuffer::ReAllocateBuffer
// Access:    private 
// Returns:   UINT The new physical size
// Qualifier: ReAllocateBuffer the Buffer to the requested size
// Parameter: UINT nRequestedSize
//************************************
UINT CBuffer::ReAllocateBuffer(UINT nRequestedSize){
	if (nRequestedSize < GetMemSize())
		return 0;
	// Allocate new size
	UINT nNewSize = (UINT) ceil(nRequestedSize / 1024.0) * 1024;
	PBYTE pNewBuffer = (PBYTE) VirtualAlloc(NULL,nNewSize,MEM_COMMIT,PAGE_READWRITE);
	UINT nBufferLen = GetBufferLen();
	CopyMemory(pNewBuffer,m_pBase,nBufferLen);
	if (m_pBase)
		VirtualFree(m_pBase,0,MEM_RELEASE);
	// Refresh the pointer
	m_pBase = pNewBuffer;
	// Realign position pointer
	m_pPtr = m_pBase + nBufferLen;
	m_nSize = nNewSize;
	return m_nSize;
}
//************************************
// Method:    DeAllocateBuffer
// FullName:  CBuffer::DeAllocateBuffer
// Access:    private 
// Returns:   UINT The new physical size
// Qualifier: DeAllocates the Buffer to the requested size
// Parameter: UINT nRequestedSize
//************************************
UINT CBuffer::DeAllocateBuffer(UINT nRequestedSize){
	if (nRequestedSize < GetBufferLen())
		return 0;
	// Allocate new size
	UINT nNewSize = (UINT) ceil(nRequestedSize / 1024.0) * 1024;
	if (nNewSize < GetMemSize())
		return 0;
	// New Copy Data Over
	PBYTE pNewBuffer = (PBYTE) VirtualAlloc(NULL,nNewSize,MEM_COMMIT,PAGE_READWRITE);
	UINT nBufferLen = GetBufferLen();
	CopyMemory(pNewBuffer,m_pBase,nBufferLen);
	VirtualFree(m_pBase,0,MEM_RELEASE);
	// Hand over the pointer
	m_pBase = pNewBuffer;
	// Realign position pointer
	m_pPtr = m_pBase + nBufferLen;
	m_nSize = nNewSize;
	return m_nSize;
}
//************************************
// Method:    Scan
// FullName:  CBuffer::Scan
// Access:    public 
// Returns:   int
// Qualifier: Scans the buffer for a given byte sequence
// Parameter: PBYTE pScan
// Parameter: UINT nPos
//************************************
int CBuffer::Scan(PBYTE pScan,UINT nPos){
	if (nPos > GetBufferLen() )
		return -1;
	PBYTE pStr = (PBYTE) strstr((char*)(m_pBase+nPos),(char*)pScan);
	int nOffset = 0;
	if (pStr)
		nOffset = (pStr - m_pBase) + strlen((char*)pScan);
	return nOffset;
}
//************************************
// Method:    ClearBuffer
// FullName:  CBuffer::ClearBuffer
// Access:    public 
// Returns:   void
// Qualifier: Clears/Resets the buffer
//            Force the buffer to be empty
//************************************
void CBuffer::ClearBuffer(){
	m_pPtr = m_pBase;
	DeAllocateBuffer(1024);
}
//************************************
// Method:    Copy
// FullName:  CBuffer::Copy
// Access:    public 
// Returns:   void
// Qualifier: Copy from one buffer object to another
// Parameter: CBuffer & buffer
//************************************
void CBuffer::Copy(CBuffer& buffer){
	int nReSize = buffer.GetMemSize();
	int nSize = buffer.GetBufferLen();
	ClearBuffer();
	ReAllocateBuffer(nReSize);
	m_pPtr = m_pBase + nSize;
	CopyMemory(m_pBase,buffer.GetBuffer(),buffer.GetBufferLen());
}
//************************************
// Method:    GetBuffer
// FullName:  CBuffer::GetBuffer
// Access:    public 
// Returns:   PBYTE
// Qualifier: Returns a pointer to the physical memory determined by the offset
// Parameter: UINT nPos
//************************************
PBYTE CBuffer::GetBuffer(UINT nPos){
	return m_pBase+nPos;
}
//************************************
// Method:    FileWrite
// FullName:  CBuffer::FileWrite
// Access:    public 
// Returns:   void
// Qualifier: Write the buffer to the file
// Parameter: const CString & strFileName
//************************************
void CBuffer::FileWrite(const CString& strFileName){
	CFile file;

	if (file.Open(strFileName, CFile::modeWrite | CFile::modeCreate))
	{
		file.Write(m_pBase,GetBufferLen());
		file.Close();
	}
}
//************************************
// Method:    Delete
// FullName:  CBuffer::Delete
// Access:    public 
// Returns:   UINT   real deleted size
// Qualifier: Delete data from the head.
// Parameter: UINT nSize
//************************************
UINT CBuffer::Delete(UINT nSize){
	if (nSize > GetMemSize())
		return 0;
	if (nSize > GetBufferLen())
		nSize = GetBufferLen();
	if (nSize)
	{
		MoveMemory(m_pBase,m_pBase+nSize,GetMemSize() - nSize);
		m_pPtr -= nSize;
	}
	DeAllocateBuffer(GetBufferLen());
	return nSize;
}