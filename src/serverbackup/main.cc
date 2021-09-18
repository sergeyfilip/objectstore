//
/// Server Backup for Keepit
//
// $Id: main.cc,v 1.6 2013/10/17 14:29:20 sf Exp $
//

#include "common/error.hh"
#include "common/trace.hh"
#include "common/scopeguard.hh"

#include "config.hh"
#include "engine.hh"
#include "command.hh"
#include "version.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <string.h>
# include <signal.h>
#include <iostream>
#include <fstream>
namespace {
  bool sig_exit(false);
  void sig_exit_hnd(int) { sig_exit = true; }
}
#endif

namespace {
  //! Tracer for high-level operations
  trace::Path t_ks("/kservd");
}


int main(int argc, char **argv) try
{
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " {configuration file}" << std::endl;
    return 1;
  }

# if defined(__unix__) || defined(__APPLE__)
  struct sigaction nact;
  struct sigaction pact;
  memset(&nact, 0, sizeof nact);
  nact.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &nact, &pact);
  ON_BLOCK_EXIT(sigaction, SIGPIPE, &pact, &nact);
# endif

  Config conf(argv[1]);
using namespace std;
 
  trace::StreamDestination logstream(std::cerr);
  trace::SyslogDestination logsys("kservd");

  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Info, "*", logstream);
  trace::Path::addDestination(trace::Warn, "*", logsys);
  trace::Path::addDestination(trace::Info, "*", logsys);
  MTrace(t_ks, trace::Info, "Keepit Server Backup "
         << g_getVersion() << " starting...");

  // Set up the engine with our cache, triggers and filters
  Engine eng(conf);

  // Set up the command interface - it needs access to change/save
  // configuration and it needs access to send commands to the engine
  Command cmd(conf, eng);
MTrace(t_ks, trace::Info, "STARTING!!!");
  // Start the command processor!
  cmd.start();
MTrace(t_ks, trace::Info, "CONTINUING!!!");
  // Start a backup - if we are configured
  if (conf.m_password.isSet() && conf.m_device.isSet()) {
    MTrace(t_ks, trace::Info, "SUBMITTING!!!");
    eng.submit(Engine::CBACKUP);
    MTrace(t_ks, trace::Info, "SUBMITTed!!!");
  } else {
    MTrace(t_ks, trace::Info, "Not configured yet, not starting a backup");
  }

#if defined(__unix__) || defined(__APPLE__)
  { struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_handler = sig_exit_hnd;
    if (sigaction(SIGINT, &act, 0))
      throw syserror("sigaction", "configuring termination handling");
    while (!sig_exit && !cmd.shouldExit()) {
      sleep(10);
      // Print status of system
      MTrace(t_ks, trace::Info, "Status: " + eng.status_en());
    }
  }
#endif

  // Tell command processor to stop
  cmd.shutdown();


  MTrace(t_ks, trace::Info, "Keepit Server Backup "
         << g_getVersion() << " shutting down...");
} catch (error &e) {
  std::cerr << e.toString() << std::endl;
  return 1;
}
