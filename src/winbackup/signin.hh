///
/// Sign in or sign up
///
// $Id: signin.hh,v 1.15 2013/10/15 12:32:36 sf Exp $
///

#ifndef WINBACKUP_SIGNIN_HH
#define WINBACKUP_SIGNIN_HH
#include "backup/upload.hh"
#include <QLabel>
#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QGroupBox>
#include <QTabWidget>

#include "svccfg.hh"


/// We have a sign-in window for the sign-in process
class SignIn : public QDialog {
  Q_OBJECT

public:
  SignIn(SvcCfg &cfg, std::string adata);

public slots:
  void doSignin();
  void doRegistration();
  void clearwarning();
  void clearfields();
  
signals:
  void signedIn();
  void registered();

private:
  SvcCfg &m_cfg;
  Mutex m_slock;
  QLabel *m_signin_toptext;
  QPushButton *m_signin_go;
  QLineEdit *email_entry;
  QLineEdit *passw_entry;

  QLabel *m_logo;
  QLabel *m_topbar;
  QLabel *m_progress;
  QGroupBox *m_activities;

  QPushButton *m_proceed_go;
  QLineEdit *email_entry2;
  QLineEdit *passw_entry2;
  QLineEdit *name_entry;
  QLabel *m_err;

  QTabWidget *m_tabwidget;
  std::string m_defaulturl;
  std::string m_testurl;
  std::string m_adata;
  std::string email;
  std::string password;
  void showerror(QString);
  struct dList {
    /// For parsing - load temporary name and id into here
    std::string tmp_name;
    std::string tmp_id;

    /// Call to add tmp into map - always returns true
    bool add();

    /// Map from extension to dlist type
    std::map<std::string,std::string> m_list;
  } hList;
std::string getDeviceIdbyName(std::string name);  
std::string checkForExistingDevice(const std::string&);
std::string replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr);
};

#endif
