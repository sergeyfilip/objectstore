//
// I/O library mutex object implementation for posix
//
// $Id: mutex_posix.cc,v 1.3 2013/04/26 17:58:42 joe Exp $
//

#include "mutex.hh"
#include "error.hh"

Mutex::Mutex()
{
  if (pthread_mutex_init(&m_handle, 0))
    throw syserror("pthread_mutex_init", "creation of mutex");
}

Mutex::Mutex(const Mutex&)
{
  if (pthread_mutex_init(&m_handle, 0))
    throw syserror("pthread_mutex_init", "copy-creation of mutex");
}

Mutex& Mutex::operator=(const Mutex&)
{
  return *this;
}

Mutex::~Mutex()
{
  pthread_mutex_destroy(&m_handle);
}

void Mutex::ll_lock()
{
  int ret = pthread_mutex_lock(&m_handle);
  if (ret)
    throw syserror("pthread_mutex_lock", "locking of mutex");
}

void Mutex::ll_unlock()
{
  pthread_mutex_unlock(&m_handle);
}
