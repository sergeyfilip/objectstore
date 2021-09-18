///
/// Tray icon handling
///
//
// $Id: tray.cc,v 1.52 2013/10/15 07:39:50 sf Exp $
//

#include <QtGui>

#include <fstream>
#include <sstream>
#include <iomanip>

#include "tray.hh"
#include "utils.hh"
#include "signin.hh"
#include "visitweb.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "common/scopeguard.hh"
#include "xml/xmlio.hh"
//#include "command.hh"
namespace {
  trace::Path t_main("/main");
  trace::Path t_sched("/main/sched");
}

Tray::Tray(SvcCfg &cfg, const std::string &appdata)
  : m_cfg(cfg)
  , m_appdata(appdata)
  , trayIcon(new QSystemTrayIcon())
  , signinAction(new QAction(tr("&Sign in"), this))
  , restoreAction(new QAction(tr("&Restore"), this))
  , settingsAction(new QAction(tr("S&ettings"), this))
  , statusAction(new QAction(tr("S&tatus"), this))
  , visitwebAction(new QAction(tr("See your files &online"), this))
  , logoutAction(new QAction(tr("&Logout"), this))
  , quitAction(new QAction(tr("&Quit"), this))
  , signin(new SignIn(cfg,appdata))
  , status(new Status)
  , fs_cache(0)
  , connection(0)
  , engine(0)
  , visitweb(new VisitWeb(cfg))
  , updater(0)
  , m_c_icon(-1)
  ,m_attributes_mask(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)
{
  // Find out where our home dir is
  m_homedir = Utils::getFolderPath(FOLDERID_Profile, "Profile");

  // Set up our meta cache
  fs_cache = new FSCache(m_appdata + "\\cache.db");
  logout = new LogOut(cfg,fs_cache,m_appdata);

  // Set up connection parameters
  connection = new ServerConnection(m_cfg.m_ngserver, 443, true);

  // Set up the updater
  updater = new AppUpdater(*connection);
  updater->setQuit(papply(this, &Tray::myQuit));
  updater->start();

  // Exclude our own config and database
  m_excludes.push_back(m_appdata);

  //
  // Set up our basic exclude list
  //
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_LocalAppData, "LocalAppData"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_CDBurning, "CDBurning"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_Cookies, "Cookies"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_Downloads, "Downloads"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_History, "History"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_InternetCache, "InternetCache"));
 
 

  m_excludes.push_back(Utils::getFolderPath(FOLDERID_StartMenu, "StartMenu"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_ProgramData, "ProgramData"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_Links, "Links"));

  // Extended exclude list

  //  m_excludes.push_back(getFolderPath(FOLDERID_PrintersFolder, "Printers"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_SendTo, "SendTo"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_Recent, "RecentItems"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_SavedGames, "SavedGames"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_Templates, "Templates"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_LocalAppData, "Local"));
  m_excludes.push_back(Utils::getFolderPath(FOLDERID_SavedSearches, "Searches"));



  



  //
  // Parse application definitions and add excludes for that
  //
#if 0
  MTrace(t_main, trace::Info, "Parsing application definitions");
  { AppDefs app(m_appdata + "\\appdefs.xml");
    // Process Local App Data excludes
    const std::string lap(Utils::getFolderPath(FOLDERID_LocalAppData, "LocalAppData"));
    for (std::list<std::string>::const_iterator i = app.m_lad_ex.begin();
         i != app.m_lad_ex.end(); ++i) {
      m_excludes.push_back(lap + "\\" + *i);
      MTrace(t_main, trace::Info, " - excluding " << m_excludes.back());
    }
  }
#endif

  // Connect the "intial" tray menu
  connect(signinAction, SIGNAL(triggered()), signin, SLOT(show()));
  connect(signin, SIGNAL(signedIn()), this, SLOT(updateContextMenu()));

  // Connect the "normal" tray menu
  connect(statusAction, SIGNAL(triggered()), status, SLOT(show()));
  connect(settingsAction, SIGNAL(triggered()), signin, SLOT(show()));
  connect(logoutAction, SIGNAL(triggered()), logout, SLOT(doLogout()));
  connect(visitwebAction, SIGNAL(triggered()), visitweb, SLOT(doVisit()));
  

  // Finally, the quit option is on both tray menus
  connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));

  updateContextMenu();

  trayIcon->setIcon(QIcon(":/trayicon_warn.png"));
  { QString mess(QApplication::applicationName()
                 + " " + QApplication::applicationVersion());
    trayIcon->setToolTip(mess);
  }
connect(trayIcon,SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this,SLOT(trayIconClicked(QSystemTrayIcon::ActivationReason))
           );
  trayIcon->show();

  // Start our tray icon update timer
  m_trayupdater = new QTimer(this);
  connect(m_trayupdater, SIGNAL(timeout()), this, SLOT(updateStatusIcon()));
  m_trayupdater->start(125);

  m_refreshmanager = new QTimer(this);
  connect(m_refreshmanager, SIGNAL(timeout()), this, SLOT(refresh_engine()));
  m_refreshmanager->start(1000);


  setWindowTitle(tr("main"));
  resize(0, 0);
 // Command *cmd = new Command(engine, m_cfg,logout);
// cmd->start();
}


void Tray::myQuit()
{
 
  if(trayIcon) {
  trayIcon->hide();
  
  }
   qApp->quit();
}
Tray::~Tray() {
  myQuit();
}
void Tray::updateStatusIcon()
{
  // Inquire status
  Status::status_t stat;
  status->getStatus(stat);

  // See if anything is running
  bool idle = true;
  for (std::vector<Upload::threadstatus_t>::const_iterator i
	 = stat.m_threads.begin(); idle && i != stat.m_threads.end(); ++i)
    if ((i->state != Upload::threadstatus_t::OSIdle) && (i->state != Upload::threadstatus_t::OSScanning))
      idle = false;

  if (idle) {
    if (m_c_icon != -1) {
      trayIcon->setIcon(QIcon(":/trayicon_blank.png"));
      m_c_icon = -1;
    }
    return;
  }

  // Ok, we're not idle. So increment the icon
  m_c_icon = (m_c_icon + 1) % 8;
  std::ostringstream icon;
  icon << ":/keepitwork" << std::setw(2) << std::setfill('0') << m_c_icon
       << ".png";
  trayIcon->setIcon(QIcon(icon.str().c_str()));

}

void Tray::handleSigninComplete()
{
  MTrace(t_main, trace::Info, "Sign in complete!");
  signin->hide();
  // If we have credentials, set up the server connection with credentials
  connection->setDefaultBasicAuth(m_cfg.m_aname, m_cfg.m_apass);
  // And set up the upload engine
  if (engine) delete engine;
  MTrace(t_main, trace::Info, "Will back up tree: " << m_homedir);
  if(m_cfg.m_ngserver.find("ws-test") == std::string::npos) {
  engine = new UploadManager(*fs_cache, *connection, m_cfg.m_devname); }
  else 
  {
   MTrace(t_main, trace::Info, "Before INIT" << m_cfg.m_device_id << "---   " << m_cfg.m_user_id);
  // engine = new UploadManager(*fs_cache, *connection, m_cfg.m_devname);
   engine = new UploadManager(*fs_cache, *connection, m_cfg.m_device_id, m_cfg.m_user_id);
  }
  MTrace(t_main, trace::Info, "After INIT" << m_cfg.m_device_id << "---   " << m_cfg.m_user_id);
  engine->setProgressLog(papply(this, &Tray::progressInfo))
    .setFilter(papply(this, &Tray::filterExcludes));

  engine->addUploadRoot(m_homedir);
  engine->addPathMonitor(m_homedir);
    
  //engine->addUploadRoot("\\\\?\\c:\\Temp");
  //engine->addPathMonitor("\\\\?\\c:\\Temp");

  //engine->addUploadRoot("\\\\?\\e:\\");
  //engine->addPathMonitor("\\\\?\\e:\\");

  // Now show the status window and start a backup  
  status->show();
  { Status::status_t s;
    status->newStatus(s);
  }
  // Go!
  status->hide(); //minimize status widget to tray
  engine->startUploadAllRoots();
  
}

void Tray::progressInfo(UploadManager &uploadManager)
{
  MTrace(t_main, trace::Debug, "Got progress info");
  if (engine) {
    Status::status_t s;
    s.m_threads = engine->getProgressInfo();
    status->newStatus(s);
    return;
  }
}

bool Tray::filterExcludes(const std::string &o )
{
  DWORD res;
  std::string realpath = o.substr(4);
  std::wstring wrealpath(realpath.begin(),realpath.end());
  res = GetFileAttributes(wrealpath.c_str());

  if (!(res & FILE_ATTRIBUTE_DIRECTORY)) {
    // If it is not directory we will look for attributes
    // If file has attributes as mask - reject it (system and hidden)
    if (res & m_attributes_mask) {    
    MTrace(t_main, trace::Info, "What we exclude:"<<  realpath << " with attributes  " << res);
    return false;
    }

  }
  // See if this object resides under any of our most basic exclude
  // paths.
  for (std::list<std::string>::const_iterator i = m_excludes.begin();
       i != m_excludes.end(); ++i)
    if (o.find(*i) == 0)
      return false;

  return true;
}

void Tray::updateContextMenu()
{
  QMenu *oldmenu = trayIcon->contextMenu();
  QMenu *trayMenu = new QMenu();

  // If we are not signed in (meaning, if we do not have an aname and
  // apass in the configuration), all we offer is sign in. We do, by
  // the way, also open the sign in dialog if it is not open already
  if (m_cfg.m_aname.empty() || m_cfg.m_apass.empty() || m_cfg.m_devname.empty()) {
    // Sign in
    MTrace(t_main, trace::Debug, "No access token - present sign-up/sign-in");
    trayMenu->addAction(signinAction);
    signin->show();    
  } else {
    // Fine, we are signed in
    MTrace(t_main, trace::Debug, "Access token found - normal operations");
    trayMenu->addAction(statusAction);
    trayMenu->addAction(visitwebAction);
    trayMenu->addAction(logoutAction);
    handleSigninComplete();
  }

  trayMenu->addSeparator();
  trayMenu->addAction(quitAction);

  trayIcon->setContextMenu(trayMenu);

  delete oldmenu;
}

void Tray:: setFilterMask(const DWORD& mask=FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) 
{
  m_attributes_mask = mask;
}

DWORD Tray::getFilterMask() {
  return m_attributes_mask;
}

void Tray::refresh_engine() {
  if(engine) {
    //FIXME: we will need to implement expiration timer in directly in UploadManager
    engine->oneSecondTimer();
    MTrace(t_main, trace::Info, "Call refreshManager\n");
  }
  else {
    MTrace(t_main, trace::Info, "Engine is not initialized!\n");
  }

}
void Tray::trayIconClicked(QSystemTrayIcon::ActivationReason reason)
{
  int x,y,w,h;                                  // show menu on menu left click
  QMenu * traymenu  =  trayIcon->contextMenu();
  (trayIcon->geometry()).getRect(&x,&y,&w,&h); // We must calculate position manually
  traymenu->move(x+12,y-90);                  // Qt doesn't want to do this correctly for left click
  trayIcon->show();
  if(reason == QSystemTrayIcon::Trigger)
      if(!trayIcon->contextMenu()->isVisible())
        traymenu->show();
      else traymenu->hide();
}
