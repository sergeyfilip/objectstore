//
//! \file common/ntohll.hh
//
//! Implementations of ntohll() and htonll() - only defined if the
//! platform does not define it by itself
//

#ifndef COMMON_NTOHLL_HH
#define COMMON_NTOHLL_HH

#if defined(__sun__)
# include <sys/byteorder.h>
#endif

#if !defined(__sun__) && !defined(__APPLE__) && defined(__unix__)
# include <endian.h>
#endif

#if defined(_WIN32)
# include <winsock2.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
# include <arpa/inet.h>
#endif

#include <stdint.h>

inline uint64_t ntohll(uint64_t i) {
# if __BYTE_ORDER == __BIG_ENDIAN
  return i;
#  else
  union sw {
    uint64_t ll;
    uint32_t l[2];
  };
  sw *r(reinterpret_cast<sw*>(&i));
  sw w;
  w.l[0] = ntohl(r->l[1]);
  w.l[1] = ntohl(r->l[0]);
  return w.ll;
# endif
}

inline uint64_t htonll(uint64_t i) {
# if __BYTE_ORDER == __BIG_ENDIAN
  return i;
#  else
  union sw {
    uint64_t ll;
    uint32_t l[2];
  };
  sw *r(reinterpret_cast<sw*>(&i));
  sw w;
  w.l[0] = htonl(r->l[1]);
  w.l[1] = htonl(r->l[0]);
  return w.ll;
# endif
}


#endif
