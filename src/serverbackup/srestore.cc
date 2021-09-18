//
/// Command line restore utility
//
// $Id: srestore.cc,v 1.2 2013/04/25 08:26:33 joe Exp $
//

#include "common/error.hh"
#include "common/trace.hh"

#include "client/serverconnection.hh"

#include "config.hh"
#include "commandproc.hh"

#include <string>


int main(int argc, char **argv) try
{
  std::string cfgfile;
  if (argc == 2)
    cfgfile = argv[1];
  else
    cfgfile = "/etc/serverbackup/serverbackup.xml";

  trace::StreamDestination logstream(std::cerr);
  trace::Path::addDestination(trace::Warn, "*", logstream);
  trace::Path::addDestination(trace::Info, "*", logstream);

  Config conf(cfgfile);

  if (!conf.m_token.isSet()) {
    std::cerr << "Authentication token not set" << std::endl;
    return -2;
  }
  if (!conf.m_password.isSet()) {
    std::cerr << "Authentication password not set" << std::endl;
    return -2;
  }
  if (!conf.m_device.isSet()) {
    std::cerr << "Device name not set" << std::endl;
    return -2;
  }

  ServerConnection conn(conf.m_apihost, 443, true);
  conn.setDefaultBasicAuth(conf.m_token.get(), conf.m_password.get());

  // Fine, now enter the command line processor.
  CommProc cp(conn);
  cp.run();
} catch (error &e) {
  std::cerr << "Error: " << e.toString()
            << std::endl;
  return -1;
}

