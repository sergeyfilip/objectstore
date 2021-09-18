///
/// Logout
///
// $Id: logout.hh,v 1.0 2013/07/17 12:59:21 sf Exp $
///

#ifndef WINBACKUP_LOGOUT_HH
#define WINBACKUP_LOGOUT_HH

#include <QLabel>
#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QGroupBox>
#include <QObject>
#include "backup\metatree.hh"

#include "svccfg.hh"

/// We have a sign-in window for the sign-in process
class LogOut : public QObject {
  Q_OBJECT

public:
  LogOut(SvcCfg &, FSCache *, const std::string &);

public slots:
  void doLogout();
  
signals:
  void logOut();

private:
  SvcCfg &m_cfg;
  FSCache *fs_cache;
  std::string appdata;
  bool DeleteFileNow(const wchar_t * );
};
#endif
