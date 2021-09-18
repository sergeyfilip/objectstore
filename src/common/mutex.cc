//
// Selector for mutex implementation
//

#if defined(__unix__) || defined(__APPLE__)
# include "mutex_posix.cc"
#endif

#ifdef _WIN32
# include "mutex_win32.cc"
#endif


MutexLock::MutexLock(Mutex &mutex)
  : m_mutex(mutex)
  , m_locked(true)
{
  m_mutex.ll_lock();
}

void MutexLock::unlock()
{
  if (m_locked) {
    m_mutex.ll_unlock();
    m_locked = false;
  }
}

MutexLock::~MutexLock()
{
  if (m_locked) {
    m_mutex.ll_unlock();
  }
}

