/********************************************************************
	created:	2014/07/26
	created:	26:7:2014   7:57
	filename: 	iocpTest\Buffer.h
	file path:	iocpTest
	file base:	Buffer
	file ext:	h
	author:		princ1ple
	
	purpose:	the buffer.cpp interface
*********************************************************************/

#pragma once
#include "stdafx.h"
#include <math.h>

class CBuffer{
	// The attributes of CBuffer class 
protected:
	PBYTE m_pBase;
	PBYTE m_pPtr;
	UINT  m_nSize;
	// Methods
private:
	UINT ReAllocateBuffer(UINT nRequestedSize);
	UINT DeAllocateBuffer(UINT nRequestedSize);
	UINT GetMemSize();
public:
	CBuffer();
	virtual ~CBuffer();
	
	UINT Write(PBYTE pData,UINT nSize);
	UINT Write(CString& strData);
	UINT Insert(PBYTE pData, UINT nSize);
	UINT Insert(CString& strData);
	UINT Read(PBYTE pData, UINT nSize);
	UINT Delete(UINT nSize);

	void Copy(CBuffer& buffer);
	int Scan(PBYTE pScan,UINT nPos);
	void FileWrite(const CString& strFileName);

	void ClearBuffer();
	UINT GetBufferLen();
	PBYTE GetBuffer(UINT nPos=0);

};