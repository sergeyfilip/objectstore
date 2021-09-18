//
//! \file common/trace.cc
//
//! Implementations of trace functionality
//
// $Id: trace.cc,v 1.5 2013/05/06 13:49:18 joe Exp $
//

#include "trace.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <syslog.h>
#endif

trace::Path *trace::Path::m_pchain = 0;

trace::Destination::~Destination()
{
}


void trace::Path::addDestination(level_t level, const std::string &patrn,
                                 Destination& dest)
{
  // Iterate through the entire chain, adding the destination where it
  // matches
  for (trace::Path *iter = m_pchain; iter; iter = iter->getNext())
    if (iter->matches(patrn))
      iter->addDestination(level, &dest);
}



bool trace::Path::matches(const std::string &patrn) const
{
  // This is very simple matching; we should support path separators
  // in the future to allow matching like "system/*" etc.
  if (patrn == "*") return true;
  if (patrn == m_name) return true;
  return false;
}


trace::StreamDestination::StreamDestination(std::ostream &stream)
  : m_stream(stream)
{
}

#if defined(__unix__) || defined(__APPLE__)

trace::SyslogDestination::SyslogDestination(const std::string &appname)
  : namebuf(appname.begin(), appname.end())
{
  namebuf.push_back(0);
  openlog(&namebuf[0], 0, LOG_DAEMON);
}

trace::SyslogDestination::~SyslogDestination()
{
  closelog();
}

void trace::SyslogDestination::output(const std::string &str)
{
  syslog(LOG_INFO, "%s", str.c_str());
}

#endif
