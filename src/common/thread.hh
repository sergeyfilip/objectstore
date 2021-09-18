//
// I/O library thread interface
//
// Subclass Thread and implement the run() function. The thread can
// then be started by calling start().
//
// Putting a running thread on the selector means it will be returned
// from poll() when thread execution has stopped and the thread has
// gone away.
//
// $Id: thread.hh,v 1.5 2013/09/19 14:14:08 vg Exp $
//

#ifndef __IO_IO_THREAD_HH
#define __IO_IO_THREAD_HH

#if defined(__unix__) || defined(__APPLE__)
# include <pthread.h>
# include <poll.h>
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

class Thread {
public:
  Thread();
  virtual ~Thread();

  /// Starts the thread. Throws on error.
  void start();

  /// Joins the thread. Will block until the thread exits. This method
  /// will never throw because it will frequently be used in
  /// destructors and we want to keep them clean. Upon thread join
  /// failure, this method will cause an abnormal termination of the
  /// application.
  void join_nothrow();

  /// Return whether or not our run() method is running in a
  /// thread. Be careful though - the thread will start some time
  /// after start() (so active() will return false a while after
  /// start() was called).
  bool active() const;

#if defined(_WIN32)
  /// Return Win32 thread handle if thread was started.
  /// (Otherwise return INVALID_HANDLE_VALUE)
  HANDLE getThreadHandle() const;
#endif

protected:
  /// This function will be called from the thread.
  virtual void run() = 0;

private:

#if defined(__unix__) || defined(__APPLE__)
  pthread_t m_thread_id;
  static void* runThread(void* object);
#endif

#if defined(_WIN32)
  static DWORD __stdcall runThread(void* object);
  DWORD m_thread_id;
  HANDLE m_thread_handle;
#endif

  /// State variable - is our thread active?
  bool m_active;
};

#endif
