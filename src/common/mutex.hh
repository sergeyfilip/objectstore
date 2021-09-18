//
//! \file common/mutex.hh
//! Mutex object interface
//
// $Id: mutex.hh,v 1.6 2013/07/15 07:56:42 sf Exp $
//

#ifndef COMMON_MUTEX_HH
#define COMMON_MUTEX_HH

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

//
//! The Mutex class defines a mutual exclusion semaphore.
//
//! The Mutex cannot be copied or assigned to
//
class Mutex {
public:
  //! Default construction
  Mutex();

  //! Semaphore destruction
  ~Mutex();

  //! You cannot copy a mutex as such. However, it makes perfect
  //! sense to protect data that can be copied, by a mutex, and
  //! having that mutex reside next to the variables it
  //! protects. Therefore, the copy operator on a Mutex actually
  //! initializes a new Mutex, it does not copy (the handle and lock
  //! state of) the existing mutex.
  Mutex(const Mutex&);

  //! By the same logic as above, it makes sense to have an
  //! assignment operator defined on a mutex, but the operator is a
  //! no-op.
  Mutex& operator=(const Mutex&);

  //! Low-level usage: NOTE: THIS IS NOT WHAT YOU WANT TO USE! You
  //! want to use the exception safe MutexLock locker below!
  void ll_lock();
  void ll_unlock();

private:
  friend class MutexLock;

# if defined(__unix__) || defined(__APPLE__)
  pthread_mutex_t m_handle;
# endif
# if defined(_WIN32)
  CRITICAL_SECTION m_handle;
# endif
};


//
//! The MutexLock class defines a lock on a mutual exclusion
//! semaphore. The locking constructor blocks until the lock is
//! taken. The lock is freed when the object expires.
//
class MutexLock {
public:
  //! Take the lock - block if necessary
  MutexLock(Mutex&);

  //! Release the lock manually
  void unlock();

  //! Release the lock
  ~MutexLock();

private:
  //! We keep a reference to the mutex we locked so that we can
  //! unlock it
  Mutex& m_mutex;
  bool m_locked;
};



# include "mutex.hcc"
#endif
