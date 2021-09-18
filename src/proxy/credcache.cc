///
/// Implementation of the credentials cache
///
// $Id: credcache.cc,v 1.3 2013/09/04 13:48:05 joe Exp $
//

#include "credcache.hh"

CredCache::CredCache(const DiffTime &timeout)
  : m_ttl(timeout)
{
}

bool CredCache::isValid(const std::string &authstr,
                        uint64_t &account, uint64_t &access, access_type_t &tt)
{
  MutexLock lock(m_cachelock);
  cache_t::iterator i = m_cache.find(authstr);
  if (i == m_cache.end())
    return false;

  // See if entry has expired
  if (i->second.eol < Time::now()) {
    m_cache.erase(i);
    return false;
  }

  // Success then!
  account = i->second.account_id;
  access = i->second.access_id;
  tt = i->second.token_type;
  return true;
}

void CredCache::cacheOk(const std::string &authstr,
                        uint64_t account, uint64_t access, access_type_t tt)
{
  MutexLock lock(m_cachelock);
  // Insert or replace this string
  m_cache[authstr] = c_t(Time::now() + m_ttl,
                         account, access, tt);
}

void CredCache::invalidate(const std::string &authstring)
{

  MutexLock lock(m_cachelock);
  m_cache.erase(authstring);
}

