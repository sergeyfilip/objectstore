///
/// Implementation of visitweb
///
// $Id: visitweb.cc,v 1.0 2013/07/17 13:56:14 sf Exp $
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
#include <QDesktopServices>
#include <QUrl>

#include "xml/xmlio.hh"
#include "common/string.hh"
#include "client/serverconnection.hh"
#include "common/trace.hh"
#include "logout.hh"
#include "visitweb.hh"

namespace {
  trace::Path t_visitweb("/visitweb");
}

VisitWeb::VisitWeb(SvcCfg &cfg)
  : m_cfg(cfg)
{
}

void VisitWeb::doVisit()
{
  try { 
  //open keepit site with tokens
  std::string url;
  std::string header("https://");
  if(m_cfg.m_ngserver.find("ws-test") == std::string::npos) {
  url = header + m_cfg.m_ngserver+"/autologin.html?tokenl="+m_cfg.m_uname+"&tokenp="+m_cfg.m_upass+"&redirecturi=/"+m_cfg.m_devname;
  }
  else
  {
  url = header + m_cfg.m_ngserver+"/autologin.html?tokenl="+m_cfg.m_uname+"&tokenp="+m_cfg.m_upass+"&redirecturi=/"+m_cfg.m_devname;
  }
  MTrace(t_visitweb, trace::Debug, "Will attempt to visite site");
  QDesktopServices::openUrl(QUrl(url.c_str()));
  return;
  }
  catch(error &e) {  // if attempt fail
   
     MTrace(t_visitweb, trace::Info, "Web visit failed.");
     QMessageBox::critical(0, QObject::tr("Error"), QObject::tr(e.toString().c_str()));
  }
}  



