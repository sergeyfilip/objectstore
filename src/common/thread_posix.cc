//
// Implementation of thread class for pthreads
//
// Uses a pipe to signal that a thread has ended execution which can
// then be read from poll() using getFD().
//
// $Id: thread_posix.cc,v 1.3 2013/04/05 09:14:45 joe Exp $
//

#include "thread.hh"
#include "error.hh"
#include "trace.hh"

#include <signal.h>

namespace {
  trace::Path t_thr("/thread");
}

Thread::Thread()
  : m_thread_id(pthread_t())
  , m_active(false)
{
}

Thread::~Thread()
{
  // Join if we created a thread
  if (m_thread_id != pthread_t())
    pthread_join(m_thread_id, 0);
}

void Thread::start()
{
  int rc = pthread_create(&m_thread_id, 0, Thread::runThread, this);
  if (rc)
    throw syserror("pthread_create", "startup of new thread");
}

void Thread::join_nothrow()
{
  int rc = pthread_join(m_thread_id, 0);
  MAssert(!rc, "Thread::join_nothrow failed");
  m_thread_id = pthread_t();
}

bool Thread::active() const
{
  return m_active;
}

void* Thread::runThread(void* object)
{
  static_cast<Thread*>(object)->m_active = true;
  try {
    static_cast<Thread*>(object)->run();
  } catch (error &e) {
    MTrace(t_thr, trace::Warn, "Thread died with: " << e.toString());
  } catch (...) {
    MTrace(t_thr, trace::Warn, "Thread died unknown exception");
  }
  static_cast<Thread*>(object)->m_active = false;
  return 0;
}

