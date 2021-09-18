///
/// Directories change monitor
//
// $Id: $
//

#include "dir_monitor.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "common/scopeguard.hh"

#include <iostream>

namespace {
//! Tracer for high-level operations
trace::Path t_dm("/dir_monitor");
}

#if defined(__unix__) || defined(__APPLE__)
# include "dir_monitor_posix.cc"
#endif

#if defined(_WIN32)
# include "dir_monitor_win32.cc"
#endif

DirMonitor &DirMonitor::setChangeNotification(const BindF1Base<void,DirMonitor&> &f)
{
  delete m_changeNotify;
  m_changeNotify = f.clone();
  return *this;
}

bool DirMonitor::popFileChangeEvent(FileChangeEvent_t &event)
{
  MutexLock l(m_fileChangeEventsLock);
  if (m_fileChangeEvents.empty()) {
    return false;
  }

  event.root = m_fileChangeEvents.front().root;
  event.fileName = m_fileChangeEvents.front().fileName;

  m_fileChangeEvents.pop();
  return true;
}
