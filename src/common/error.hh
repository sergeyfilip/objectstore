//
//! \file common/error.hh
//! Definition of common error handling entitites
//

#ifndef COMMON_ERROR_HH
#define COMMON_ERROR_HH

#include <string>
#include <iostream>
#include <sstream>
#include <stdlib.h>

#if defined(_WIN32)
# include <windows.h>
#endif

class error {
public:
  error(const std::string &explanation);
  virtual ~error();

  virtual std::string toString() const;
protected:
  error();
private:
  const std::string m_explanation;
};

class syserror : public error {
public:
  syserror(const std::string &syscall,
           const std::string &what_failed);
  
  std::string toString() const;
private:
  const std::string m_syscall;
  const std::string m_what_failed;

#if defined(_WIN32)
  DWORD m_error;
#endif

#if defined(__unix__) || defined(__APPLE__)
  int m_error;
#endif
};

#define MAssert(cond, expr)                                             \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::ostringstream ostr;                                          \
      ostr << "Assertion failed: " << expr;                             \
      std::cerr << ostr.str() << std::endl;                             \
      abort();                                                          \
    }                                                                   \
  } while (false)

// Suppressing GCC Warnings for OpenSSL on Mac OS
#if defined(__APPLE__) && ((__GNUC__ * 100) + __GNUC_MINOR__) >= 402
#define GCC_DIAG_STR(s) #s
#define GCC_DIAG_JOINSTR(x,y) GCC_DIAG_STR(x ## y)
# define GCC_DIAG_DO_PRAGMA(x) _Pragma (#x)
# define GCC_DIAG_PRAGMA(x) GCC_DIAG_DO_PRAGMA(GCC diagnostic x)
# if ((__GNUC__ * 100) + __GNUC_MINOR__) >= 406
#  define GCC_DIAG_OFF(x) GCC_DIAG_PRAGMA(push) \
GCC_DIAG_PRAGMA(ignored GCC_DIAG_JOINSTR(-W,x))
#  define GCC_DIAG_ON(x) GCC_DIAG_PRAGMA(pop)
# else
#  define GCC_DIAG_OFF(x) GCC_DIAG_PRAGMA(ignored GCC_DIAG_JOINSTR(-W,x))
#  define GCC_DIAG_ON(x)  GCC_DIAG_PRAGMA(warning GCC_DIAG_JOINSTR(-W,x))
# endif
#else
# define GCC_DIAG_OFF(x)
# define GCC_DIAG_ON(x)
#endif


#endif
