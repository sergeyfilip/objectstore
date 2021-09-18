///
/// Simple little watchdog for package updates
///
/// This object will regularly check
///
///  /files/download/updates/desktop/pc/latest-$(ARCH_NAME)/package.xml
///
/// to see if the build number is newer than the build number of the
/// currently running software.
///
/// Should that be the case, then the updater will copy the
/// "update.exe" executable in the directory of the currently running
/// module to a temporary location and execute it. It will also
/// initiate a quit of the currently running application (by calling
/// the supplied Quit callback).
///
/// It is then the job of the update.exe application to fetch the
/// update and apply it, and to re-start the keepitw.exe executable.
///
#ifndef WINBACKUP_APPUPDATER_HH
#define WINBACKUP_APPUPDATER_HH

#include "common/thread.hh"
#include "common/semaphore.hh"
#include "common/partial.hh"
#include "client/serverconnection.hh"

class AppUpdater : public Thread {
public:
  /// Instantiate with prototype connection
  AppUpdater(ServerConnection &conn);

  /// We need special handling for shutdown
  ~AppUpdater();

  /// This closure is called when we want to quit to allow an update
  AppUpdater &setQuit(BindBase<void> &);

protected:
  /// Our actual update check worker...
  void run();

private:
  /// Our connection
  ServerConnection m_conn;

  /// Our quit callback
  BindBase<void> *m_quit;

  /// Our worker thread will exit when this semaphore can be
  /// decremented
  Semaphore m_quitsem;
};


// Upgrade routine to be run by main when invoked with the -u <pid>
// <build> switch
void runUpgrade(DWORD pid, uint32_t buildno, const std::string host);


// Returns the full path of the currently running executable
std::string getCurrentModule();

// Returns the containing directory of the currently running
// executable.
std::string getCurrentModuleDirectory();

#endif
