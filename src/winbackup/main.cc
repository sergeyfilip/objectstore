///
/// Main program for the Native Windows backup application
///
// $Id: main.cc,v 1.23 2013/10/09 14:34:53 sf Exp $
//

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "common/error.hh"
#include "common/trace.hh"
#include "common/scopeguard.hh"
#include "common/string.hh"
#include "utils.hh"
#include "tray.hh"
#include "tracedest.hh"
#include "version.hh"
#include <QtGui>

#include <iostream>
#include <fstream>

namespace {
  std::string cafile;
  trace::Path t_main("/main");
}

int main(int argc, char *argv[])
{
  Q_INIT_RESOURCE(keepitw);

  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  app.setApplicationName("Keepit");
  app.setApplicationVersion(g_getVersion());
  app.setWindowIcon(QIcon(":/KeepitLogo.png"));

  QDialogDestination logstream;
  try {
    //
    // Set up tracing
    //
    trace::Path::addDestination(trace::Warn, "*", logstream);
    trace::Path::addDestination(trace::Info, "*", logstream);
    trace::Path::addDestination(trace::Debug, "/AppUpdater", logstream);

#if 0
    std::ofstream outf("keepitw.log", std::ios::app);
    trace::StreamDestination streamf(outf);
    trace::Path::addDestination(trace::Warn, "*", streamf);
    trace::Path::addDestination(trace::Info, "*", streamf);
    trace::Path::addDestination(trace::Debug, "/AppUpdater", streamf);
#endif

    for (int i = 1; i < argc; ++i) {
      if (argv[i] == std::string("-d"))
	logstream.show();
    }

    /// Our service configuration
    SvcCfg svccfg;

    // Find out where to store our configuration
    std::string appdata = Utils::getFolderPath(FOLDERID_LocalAppData, "LocalAppData", true) + "\\Keepit Data";
    
    // Legacy handling; if we have a KeepitNG directory, rename it to Keepit Data
    { const std::string legacy = Utils::getFolderPath(FOLDERID_LocalAppData, "LocalAppData", true)
                                 + "\\KeepitNG";
      MoveFile(utf8_to_utf16(legacy).c_str(),
               utf8_to_utf16(appdata).c_str());
    }
    
    // Attempt creating our KeepitNG sub directory no matter what
    CreateDirectory(utf8_to_utf16(appdata).c_str(), 0);
    svccfg.setFName(appdata + "\\winbackup.xml");
    
    // Attempt reading the configuration file
    try {
      svccfg.read();
      MTrace(t_main, trace::Debug, "Successfully read configuration");
    } catch (error &e) {
      // Error - we need to set up defaults
      MTrace(t_main, trace::Info, "Unable to load config: \"" << e.toString() << "\" "
             << "- will set defaults");
      svccfg.m_ngserver = "ws.keepit.com";
      svccfg.write();
    }

    //
    // Start up winsock
    //
    { WSADATA tmp;
      if (WSAStartup(0x0202, &tmp))
	throw error("Cannot initialize winsock");
    }
    ON_BLOCK_EXIT(WSACleanup);

    // Set up the path to our CA file - the CA file lies with the
    // executable
    { cafile = getCurrentModuleDirectory() + "\\ca-bundle.crt";
      // Now init SSL
      global_config::cafile = cafile.c_str();
      MTrace(t_main, trace::Info, "CA File at: " << cafile);
    }

    for (int i = 1; i < argc; ++i) {
      if (argv[i] == std::string("-u")) {
        if (i+2 >= argc)
          throw error("-u option without PID and BUILD");
        // Execute upgrade with given PID
        runUpgrade(string2Any<DWORD>(argv[i+1]),
                   string2Any<uint32_t>(argv[i+2]),
                   svccfg.m_ngserver);
        // Exit when done.
        return 0;
      }
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable())
      throw error("System tray not available");

    //
    // Instantiate and run our application
    //
    Tray tray(svccfg, appdata);
    tray.show();
    return app.exec();
  } catch (error &e) {
    // We should NEVER get to this place - all errors that can usually
    // happen (network connectivity issues, configuration errors, what
    // have you) should be handled within the relevant sections of the
    // application. This dialogue is only here to ensure that in case
    // an unhandled error occurs, we actually do present the user with
    // a message before closing down. At least that will allow
    // troubleshooting if nothing else.
    QMessageBox::critical(0, QObject::tr("Error"),
			  QObject::tr((e.toString()).c_str()));
   
    return 1;

  }
}

