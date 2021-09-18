///
/// Implementation of logout
///
// $Id: logout.cc,v 1.0 2013/07/17 13:50:14 sf Exp $
///

#include "signin.hh"

#include <QImage>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QPixmap>
#include <QMessageBox>
#include <QProcess>
#include <QApplication>
#include <QDebug>

#include "xml/xmlio.hh"
#include "common/string.hh"
#include "client/serverconnection.hh"
#include "common/trace.hh"
#include "logout.hh"
#include <windows.h>
#include <iostream>

namespace {
  trace::Path t_logout("/logout");
}

LogOut::LogOut(SvcCfg &cfg, FSCache *fs_c, const std::string &ad)
  : m_cfg(cfg),fs_cache(fs_c), appdata(ad)
{ 
}

void LogOut::doLogout()
{
  try {
  MTrace(t_logout, trace::Info, "Will attempt to logout");
  // MessageBox for logout
  QMessageBox msgBox;
  msgBox.setText("Logout.");
  msgBox.setInformativeText("Do you want to logout?");
  msgBox.setStandardButtons(QMessageBox::Ok |  QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Cancel);
  MTrace(t_logout, trace::Info, "MBox ready");   
  int ret = msgBox.exec();
  MTrace(t_logout, trace::Info, "MBox done");  
 
  switch (ret) {
  case QMessageBox::Ok:
      // OK was clicked
      // We will delete configuration
      m_cfg.m_aname = std::string();
      m_cfg.m_apass = std::string();
      m_cfg.m_uname = std::string();
      m_cfg.m_upass = std::string();
      m_cfg.m_devname = "";
      m_cfg.m_user_id = "";
	  m_cfg.m_device_id = std::string();
      m_cfg.write();
       // Emit our log-out complete signal
      //  fs_cache->clearCache();

	  
      emit logOut();
      // Make smart application restart - detach new and kill old if success
      if ( QProcess::startDetached(QString("\"") + QApplication::applicationFilePath() + "\"") )
	{ 
	   QApplication::quit(); 
      }
      else {
        qDebug("Cannot restart process! Try to make it manually!");
        MTrace(t_logout, trace::Info, "Logout: cannot restart process, do it manually.");
      }
      break;
  case QMessageBox::Cancel:
      // Cancel was clicked
    return;
      break;
  default:
      // should never be reached
MTrace(t_logout, trace::Info, "Logout  should never be reached");
    return;
      break;
  }
}  
  catch(error &e)
    {
             MTrace(t_logout, trace::Info, "Logout attempt failed!");
	     QMessageBox::critical(0, QObject::tr("Error"), QObject::tr(e.toString().c_str()));
    }

}

