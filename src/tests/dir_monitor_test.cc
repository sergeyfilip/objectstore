//
// Test utility for Directory Monitor component
//

#include "common/error.hh"
#include "common/trace.hh"
#include "common/scopeguard.hh"
#include "common/partial.hh"
#include "common/string.hh"

#include "backup/dir_monitor.hh"

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

#include <errno.h>
#include <stdint.h>

namespace {
  //! Tracer for high-level operations
  trace::Path t_dmt("/dir_monitor_test");
}

void changeNotification(DirMonitor &dirMonitor)
{
  DirMonitor::FileChangeEvent_t event;
  bool rc = dirMonitor.popFileChangeEvent(event);
  while (rc) {
    MTrace(t_dmt, trace::Info, "Changed:" << event.root << " " << event.fileName);
    rc = dirMonitor.popFileChangeEvent(event);
  }
}

int main(int argc, char **argv) try
{
  trace::StreamDestination logstream(std::cerr);

  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Info, "*", logstream);
  trace::Path::addDestination(trace::Debug, "*", logstream);

  MTrace(t_dmt, trace::Info, "dir_monitor_test started");

  DirMonitor dirMonitor;
  dirMonitor.setChangeNotification(papply(&changeNotification));
//  dirMonitor.addDir("A:\\");
//  dirMonitor.addDir("C:\\");
//  dirMonitor.addDir("E:\\");
//  dirMonitor.addDir("Z:\\");
  dirMonitor.addDir("/");
  dirMonitor.addDir("/home/rd/Downloads");
  dirMonitor.addDir("/Users/vg/Downloads");
//  dirMonitor.start();

  // Wait for AnyKey+Enter
  std::cin.get();

  return 0;
} catch (error &e) {
  std::cerr << std::endl
            << e.toString() << std::endl;
  return 1;
}
