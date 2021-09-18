//
//! \file common/error.cc
//! Basic error handling
//
// $Id: error.cc,v 1.5 2013/05/16 07:39:36 joe Exp $
//

#include "error.hh"
#include "string.hh"
#include <cerrno>
#include <cstring>

error::error(const std::string &explanation)
  : m_explanation(explanation)
{
}

error::error()
{
}

error::~error()
{
}

std::string error::toString() const
{
  return m_explanation;
}


syserror::syserror(const std::string &a_sc,
                   const std::string &a_wf)
  : m_syscall(a_sc)
  , m_what_failed(a_wf)
#if defined(__unix__) || defined(__APPLE__)
  , m_error(errno)
#endif
#if defined(_WIN32)
  , m_error(GetLastError())
#endif
{
}

std::string syserror::toString() const
{
#if defined(__unix__) || defined(__APPLE__)
  return "System call \"" + m_syscall
    + "\" failed (" + strerror(m_error) + ")"
    + " during " + m_what_failed;
#endif
#if defined(_WIN32)
  static wchar_t buf[4096];
  DWORD res = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM
			    | FORMAT_MESSAGE_IGNORE_INSERTS,
			    0, m_error, 0, buf, sizeof buf, 0);
  if (!res) {
    buf[0] = 0;
  }
  buf[(sizeof buf) / (sizeof buf[0]) - 1] = 0;
  return "System call \"" + m_syscall
    + "\" failed (" + utf16_to_utf8(buf) + ")"
    + " during " + m_what_failed;
#endif
}
