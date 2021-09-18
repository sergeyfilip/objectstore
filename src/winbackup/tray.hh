///
/// Tray icon handler
///
// $Id: tray.hh,v 1.22 2013/10/08 07:55:03 vg Exp $
//

#ifndef WINBACKUP_TRAY_HH
#define WINBACKUP_TRAY_HH

#include <shlobj.h>

#include <QSystemTrayIcon>
#include <QDialog>
#include <QTimer>

#include "backup/upload.hh"
#include "signin.hh"
#include "status.hh"
#include "appupdater.hh"
#include "logout.hh"
#include "visitweb.hh"
//#include "command.hh"

///
/// The "Tray" functions as the main application window. It is never
/// shown, it only exists to function as a parent of the system tray
/// icon object, the tray icon actions and so on.
///
class Tray : public QDialog {
  Q_OBJECT

public:
  Tray(SvcCfg &cfg, const std::string &appdata);
  ~Tray();
   DWORD getFilterMask();
   void setFilterMask(const DWORD&);
   
private slots:
  void updateStatusIcon();
  /// Call this method to update the context menu depending on the
  /// state of our application (are we signed in etc.)
  void updateContextMenu();
  void trayIconClicked(QSystemTrayIcon::ActivationReason);
  void refresh_engine(); 
  
private:
  QSystemTrayIcon *trayIcon;
  QAction *signinAction;
  QAction *restoreAction;
  QAction *settingsAction;
  QAction *statusAction;
  QAction *logoutAction;
  QAction *quitAction;
  QAction *visitwebAction;
  SignIn *signin;
  Status *status;
  LogOut *logout;
  VisitWeb *visitweb;
 

  QTimer *m_trayupdater;
  QTimer *m_refreshmanager;
  
  /// Our tray icon status - either -1 for idle or 0...3 for the
  /// animation
  int m_c_icon;

  /// This method is called from our backup engine whenever it has new
  /// progress info
  void progressInfo(UploadManager &uploadManager);

  /// Exclude processing
  bool filterExcludes(const std::string &);

  void handleSigninComplete();

  /// Our service configuration
  SvcCfg &m_cfg;

  /// This is the directory for our application local data (config
  /// file and cache)
  std::string m_appdata;

  /// This is the user home directory
  std::string m_homedir;

  /// This is our list of exclude paths. Any object whose beginning
  /// matches a full entry in this list is excluded.  This list is
  /// initialized in our constructor and it is READ (but never
  /// modified) by the worker threads. Therefore we need no locking.
  std::list<std::string> m_excludes;

  DWORD m_attributes_mask;

  /// Our actual upload engine components
  FSCache *fs_cache;
  ServerConnection *connection;
  UploadManager *engine;

  /// Our update API checker
  AppUpdater *updater;

  /// Trigger QApplication quit
  void myQuit();

};



#endif
