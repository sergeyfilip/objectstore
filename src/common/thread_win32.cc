///
/// Implentation of thread class for win32
///
// $Id: thread_win32.cc,v 1.3 2013/09/19 14:14:08 vg Exp $
//

#include "thread.hh"
#include "error.hh"
#include "trace.hh"

namespace {
  trace::Path t_thr("/thread");
}

Thread::Thread()
  : m_thread_handle(INVALID_HANDLE_VALUE)
  , m_active(false)
{
}

Thread::~Thread()
{
  if (m_thread_handle != INVALID_HANDLE_VALUE)
    join_nothrow();
}

void Thread::start()
{
  m_thread_handle = CreateThread(0, 0, runThread, this, 0, 0);
  if (!m_thread_handle)
    throw syserror("CreateThread", "creating thread");
}

void Thread::join_nothrow()
{
  MAssert(m_thread_handle != INVALID_HANDLE_VALUE,
          "Join called on thread that was never started");

  // Wait for the thread to exit
  DWORD wrc = WaitForSingleObject(m_thread_handle, INFINITE);
  if (wrc == WAIT_OBJECT_0) {
    DWORD retval = 0;
    if (!GetExitCodeThread(m_thread_handle, &retval))
      MTrace(t_thr, trace::Warn, "Cannot get thread exit code");
    if (retval)
      MTrace(t_thr, trace::Warn, "Thread exited with nonzero exit code");
    CloseHandle(m_thread_handle);
    m_thread_handle = INVALID_HANDLE_VALUE;
    return;
  }
  MTrace(t_thr, trace::Warn, "Wait for thread on join was unsuccessful");
}

bool Thread::active() const
{
  return m_active;
}

HANDLE Thread::getThreadHandle() const
{
  return m_thread_handle;
}

DWORD Thread::runThread(void* object)
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
