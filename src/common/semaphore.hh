//
//! \file common/semaphore.hh
//! Semaphore interface
//
// $Id: semaphore.hh,v 1.4 2013/06/05 11:02:46 joe Exp $
//

#ifndef COMMON_SEMAPHORE_HH
#define COMMON_SEMAPHORE_HH

# if defined(__unix__) || defined(__APPLE__)
#  include <semaphore.h>
# endif

#if defined(__APPLE__)
# include "mutex.hh"
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

#include "time.hh"

//
//! \class Semaphore
//
//! The Semaphore class defines a simple semaphore that can be
//! decremented (a possibly blocking operation) and incremented.
//
class Semaphore {
public:
  //! Initialize semaphore - with optional initial value
  inline Semaphore(size_t init = 0);

  //! Semaphore destruction. There must not be any threads waiting for
  //! this semaphore during time of destruction.
  inline ~Semaphore();

  //! Increment the semaphore. This operation will always succeed
  //! without blocking.
  inline void increment();

  //! Decrement the semaphore, possibly blocking the current
  //! thread.
  inline void decrement();

#if defined(__unix__) || defined(_WIN32)
  //! Decrement the semaphore, possibly blocking the current
  //! thread. If the given timeout is different from END_OF_TIME,
  //! decrement will return if that time is passed.
  //
  //! Returns true on successful decrement, false on timeout.
  inline bool decrement(const Time &timeout);
#endif

private:
# if defined(__unix__)
  //! Our semaphore
  sem_t m_sem;
# endif

# if defined(__APPLE__)
  //! Our semaphore
  sem_t *m_sem;
  //! We need a mutex for name sequence updating
  static Mutex m_sequence_mutex;
  //! The name sequence
  static size_t m_sequence;
# endif

# if defined(_WIN32)
  //! Our semaphore
  HANDLE m_sem;
# endif

};


# include "semaphore.hcc"
#endif
