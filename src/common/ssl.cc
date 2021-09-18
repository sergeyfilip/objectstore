///
/// Initialisation of OpenSSL
///
//
// $Id: ssl.cc,v 1.4 2013/05/30 21:22:45 vg Exp $
//

# include <openssl/ssl.h>
# include <openssl/err.h>

#include "ssl.hh"
#include "mutex.hh"
#include "error.hh"

#include <vector>

namespace {
    //! Mutex for ssl initialisation
    Mutex init_ssl_lock;

    //! Did we init?
    bool init_ssl_done = false;

    //! Our locking vector for SSL locking
    std::vector<Mutex> ssl_locks;

    //! Our locking callback
    extern "C" {
      void ssl_locker(int mode, int n, const char *, int) {
        if (mode & CRYPTO_LOCK)
          ssl_locks[n].ll_lock();
        else
          ssl_locks[n].ll_unlock();
      }
      unsigned long ssl_getid() {
#if defined(__unix__) || defined(__APPLE__)
        return (unsigned long)pthread_self();
#endif
#if defined(_WIN32)
	return GetCurrentThreadId();
#endif
      }
    }

  }

GCC_DIAG_OFF(deprecated-declarations);
void init_openssl_library()
{
  // Lock-less fast path
  if (init_ssl_done)
    return;

  // Fine, we may not have initialised. We must enter a critical
  // section, re-test and then initialise if needed
  MutexLock lock(init_ssl_lock);
  if (init_ssl_done)
    return;

  // Fine, init then
  init_ssl_done = true;
  SSL_library_init();
  SSL_load_error_strings();

  // Set locking callback
  ssl_locks.resize(CRYPTO_num_locks());
  CRYPTO_set_locking_callback(&ssl_locker);
  CRYPTO_set_id_callback(&ssl_getid);
}
GCC_DIAG_ON(deprecated-declarations);
