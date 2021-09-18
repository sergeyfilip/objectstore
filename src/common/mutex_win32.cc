//
// I/O library mutex object implementation for win32
//
// $Id: mutex_win32.cc,v 1.2 2013/04/26 17:58:42 joe Exp $
//

#include "mutex.hh"
#include "error.hh"

Mutex::Mutex()
{
  InitializeCriticalSection(&m_handle);
}

Mutex::Mutex(const Mutex&)
{
  InitializeCriticalSection(&m_handle);
}

Mutex& Mutex::operator=(const Mutex&)
{
  return *this;
}

Mutex::~Mutex()
{
  DeleteCriticalSection(&m_handle);
}

void Mutex::ll_lock()
{
  EnterCriticalSection(&m_handle);
}

void Mutex::ll_unlock()
{
  LeaveCriticalSection(&m_handle);
}


