///
/// Credentials cache - for speeding up authentication in workers
///
//
// $Id: credcache.hh,v 1.4 2013/09/04 13:48:05 joe Exp $
//

#ifndef PROXY_CREDCACHE_HH
#define PROXY_CREDCACHE_HH

#include "common/time.hh"
#include "common/mutex.hh"

#include <map>
#include <string>

class CredCache {
public:
  CredCache(const DiffTime &timeout);

  //! Access tokens can have various types that may limit their access
  //! to API calls. The actual restrictions are implemented in the
  //! various API methods. The m_access_type variable is set by the
  //! authenticate() method.
  enum access_type_t { AT_None = 0x00,
                       AT_AnonymousParent = 0x01,
                       AT_PartnerParent = 0x02,
                       AT_User = 0x04,
                       AT_Device = 0x08
  };

  /// See if this username:password is already cached and ok
  bool isValid(const std::string &authstring,
               uint64_t &account, uint64_t &accesss, access_type_t &tt);

  /// Insert this username:password in cache - it must resolve to the
  /// given account,access,tt
  void cacheOk(const std::string &authstring,
               uint64_t account, uint64_t access, access_type_t tt);

  /// Remove this authstring from the cache - used when renaming or
  /// deleting tokens. Does nothing if authstring was not in cache.
  void invalidate(const std::string &authstring);

private:

  /// Our cache entry Time-To-Live (after which they will expire and
  /// have to be re-inserted)
  const DiffTime m_ttl;

  /// Cache mutual exclusion
  Mutex m_cachelock;

  /// Cache entry
  struct c_t {
    c_t() : account_id(-1), access_id(-1), token_type(AT_None) { }
    c_t(const Time &t, uint64_t accnt, uint64_t accs, access_type_t tt)
      : eol(t), account_id(accnt), access_id(accs), token_type(tt) { }
    Time eol; /// End-of-Life of entry
    uint64_t account_id; /// Account it
    uint64_t access_id;  /// Access token id
    access_type_t token_type; /// Access token type
  };

  /// Mapping from authstring into cache entry expiry
  typedef std::map<std::string,c_t> cache_t;
  cache_t m_cache;

};


#endif
