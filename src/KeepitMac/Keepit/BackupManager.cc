
#include "BackupManager.hh"
#include "common/partial.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "common/time.hh"
#include "client/serverconnection.hh"
#include "xml/xmlio.hh"

namespace {
  //! Tracer for Backup Manager
  trace::Path t_bm("/BackupManager");
}

BackupManager::BackupManager(Config &cfg)
: m_cfg(cfg)
, m_fsCache(cfg.m_cachename)
, m_serverConnection(0)
, m_uploadManager(0)
, m_shouldexit(false)
, m_folderSizeCalc(papply(this, &BackupManager::didCalcFileSize))
{
}

BackupManager::~BackupManager(void)
{
  // Make our thread exit first.
  stop();
  join_nothrow();
  
  while (!m_cmdQueue.empty()) {
    delete m_cmdQueue.front();
    m_cmdQueue.pop();
  }
}

bool BackupManager::shouldExit() const
{
  return m_shouldexit;
}

void BackupManager::run()
{
  // Accept connections and process commands...
  while (!shouldExit()) {

    CmdBase* cmdBase = 0;
    // We access queue data
    { MutexLock l(m_cmdQueueLock);
      if (!m_cmdQueue.empty()) {
        cmdBase = m_cmdQueue.front();
        MAssert(cmdBase, "");
      }
    }
    if (cmdBase) {
      try {
        if (CmdRegister* cmd = dynamic_cast<CmdRegister*>(cmdBase)) {
          doRegister(*cmd);
        } else if (CmdDeRegister* cmd = dynamic_cast<CmdDeRegister*>(cmdBase)) {
          doDeregister(*cmd);
        } else if (CmdNewUser* cmd = dynamic_cast<CmdNewUser*>(cmdBase)) {
          doNewUser(*cmd);
        } else if (CmdGetUserDetails* cmd = dynamic_cast<CmdGetUserDetails*>(cmdBase)) {
          doGetUserDetails(*cmd);
        } else if (CmdNewDevice* cmd = dynamic_cast<CmdNewDevice*>(cmdBase)) {
          doNewDevice(*cmd);
        } else if (CmdMonitor* cmd = dynamic_cast<CmdMonitor*>(cmdBase)) {
          doMonitor(*cmd);
        } else if (CmdStopMonitor* cmd = dynamic_cast<CmdStopMonitor*>(cmdBase)) {
          doStopMonitor(*cmd);
        } else {
          MAssert(false, "Unknown command");
        }
      } catch (error &e) {
        MTrace(t_bm, trace::Warn, "Backup Manager error: " << e.toString());
      }
      // We access queue data
      { MutexLock l(m_cmdQueueLock);
        m_cmdQueue.pop();
      }
      delete cmdBase;
    }

    // Wait
    m_cmdQueueSem.decrement();
    // Go again
  }
  MTrace(t_bm, trace::Info, "Backup manager exiting.");
}

void BackupManager::updateHost()
{
    { MutexLock cfglock(m_cfg.m_lock);
        m_cfg.read();
    }
}

void BackupManager::changeCache()
{
    m_fsCache.changeCache();
    m_fsCache.clearCache();
}

void BackupManager::start()
{
  Thread::start();
}

void BackupManager::stop()
{
  // We want to exit
  m_shouldexit = true;
  
  // Wake up poll to make thread realise we should process new command
  wakeUpCmdThread();
}

void BackupManager::wakeUpCmdThread()
{
  m_cmdQueueSem.increment();
}

void BackupManager::execAsync(CmdBase* cmd)
{
  // We access queue data
  { MutexLock l(m_cmdQueueLock);
    m_cmdQueue.push(cmd);
  }
  wakeUpCmdThread();
}

std::string BackupManager::getEmail()
{
  // We access config data
  MutexLock cfglock(m_cfg.m_lock);
  return m_cfg.m_email.isSet()?m_cfg.m_email.get():"";
}

std::string BackupManager::getDeviceName()
{
  // We access config data
  MutexLock cfglock(m_cfg.m_lock);
  return m_cfg.m_device.isSet()?m_cfg.m_device.get():"";
}

bool BackupManager::isMonitoring()
{
  return (m_uploadManager);
}

bool BackupManager::isUploading()
{
  return (m_uploadManager && m_uploadManager->isWorking());
}

bool BackupManager::isCalculating()
{
  return m_folderSizeCalc.active();
}

///////////////////////////////////////////////////////////////////////////
//
// Register - get access token
//
bool BackupManager::cmdRegister(const std::string& email, const std::string& password)
{
  execAsync(new CmdRegister(email, password));
  return true;
}

void BackupManager::doRegister(const CmdRegister& cmd)
{
  std::string apihost;
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    apihost = m_cfg.m_apihost;
  }
  
  //
  // Fine, send to server
  //
  ServerConnection conn(apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
  req.setBasicAuth(cmd.email, cmd.password);

  std::string type("User");
  std::string descr("KeepitMac");
  std::string anameU = randStr(16);
  std::string apassU = randStr(16);
  {
    using namespace xml;
    {
      char hname[1024];
      if (gethostname(hname, sizeof hname))
        throw syserror("gethostname", "retrieving local host name");
      descr +=  " on " + std::string(hname);
    }
    
    const IDocument &doc = mkDoc
    (Element("token")
     (Element("type")(CharData<std::string>(type))
      & Element("descr")(CharData<std::string>(descr))
      & Element("aname")(CharData<std::string>(anameU))
      & Element("apass")(CharData<std::string>(apassU))));
    
    req.setBody(doc);
  }
  
  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() == 201) {
    // Request "Device" token
    ServerConnection::Request req(ServerConnection::mPOST, "/tokens/");
    req.setBasicAuth(anameU, apassU);
    
    std::string type("Device");
    std::string anameD = randStr(16);
    std::string apassD = randStr(16);
    {
      using namespace xml;      
      const IDocument &doc = mkDoc
      (Element("token")
       (Element("type")(CharData<std::string>(type))
        & Element("descr")(CharData<std::string>(descr))
        & Element("aname")(CharData<std::string>(anameD))
        & Element("apass")(CharData<std::string>(apassD))));
      
      req.setBody(doc);
    }
    
    rep = conn.execute(req);
    if (rep.getCode() == 201) {
      MTrace(t_bm, trace::Info, "Access token acquired and stored");
      // We access config data
      { MutexLock cfglock(m_cfg.m_lock);
        m_cfg.m_email = cmd.email;
        m_cfg.m_token = anameU;
        m_cfg.m_password = apassU;
        m_cfg.m_deviceTokenL = anameD;
        m_cfg.m_deviceTokenP = apassD;
        m_cfg.write();
      }
      
      // Notify about the result
      notify(papply<void>(&BackupManagerListener::didRegister));
      return;
    }
  }
  
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    m_cfg.m_token = Optional<std::string>();
    m_cfg.m_password = Optional<std::string>();
    m_cfg.m_deviceTokenL = Optional<std::string>();
    m_cfg.m_deviceTokenP = Optional<std::string>();
    m_cfg.write();
  }
  MTrace(t_bm, trace::Warn, (rep.getCode() == 401?"Authentication failed":rep.toString()));
  // Notify about the result
  notify(papply<void>(&BackupManagerListener::didFailToRegister
                      , std::string(rep.getCode() == 401
                                    ?"Authentication failed":rep.toString())));
}

bool BackupManager::isRegistered()
{
  // We access config data
  MutexLock cfglock(m_cfg.m_lock);
  return m_cfg.m_token.isSet() && m_cfg.m_deviceTokenL.isSet();
}

bool BackupManager::checkRegistration()
{
    // We access config data
    MutexLock cfglock(m_cfg.m_lock);
    return m_cfg.m_userID.isSet() && m_cfg.m_deviceID.isSet();
}

bool BackupManager::doReRegisterUser()
{
    std::string devname;
    std::string pass;
    
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        if (m_cfg.m_device.isSet()) devname = m_cfg.m_device.get();
        if (m_cfg.m_password.isSet()) pass = m_cfg.m_password.get();
    }
    
    if (devname != "" && pass != "") return BackupManager::getInst().checkForExistingDevice(devname, pass);
    
    return false;
}

// End Register
///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////
//
// Deregister - delete access token
//
bool BackupManager::cmdDeregister()
{
  execAsync(new CmdDeRegister());
  return true;
}

void BackupManager::doDeregister(const CmdDeRegister& cmd)
{
  MTrace(t_bm, trace::Info, "Deregistered");
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
      m_cfg.m_email = Optional<std::string>();
    m_cfg.m_token = Optional<std::string>();
    m_cfg.m_password = Optional<std::string>();
    m_cfg.m_deviceTokenL = Optional<std::string>();
    m_cfg.m_deviceTokenP = Optional<std::string>();
    m_cfg.write();
  }
  
  // Notify about the result
  notify(papply<void>(&BackupManagerListener::didDeregister));
}
// End Deregister
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// Helper routine to retrieve information (ID) about the authenticated user
// (throws on errors)
//
std::string BackupManager::getAuthUserID(const std::string &u, const std::string &p)
{
  std::string apihost;
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    apihost = m_cfg.m_apihost;
  }

  ServerConnection conn(apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mGET, "/users");
  req.setBasicAuth(u, p);

  ServerConnection::Reply rep = conn.execute(req);

  if (rep.getCode() != 200) {
    throw error("Failed to get user ID: " + rep.toString());
  }

  std::string userId;
  using namespace xml;
  const IDocument &ddoc
  = mkDoc(Element("user")
          (Element("id")(CharData<std::string>(userId))));

  try {
    std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
    XMLexer lexer(body);
    ddoc.process(lexer);
  } catch (error &e) {
    throw error("Failed to parse user ID: " + e.toString());
  }
    
    { MutexLock cfglock(m_cfg.m_lock);
        m_cfg.m_userID = userId;
        m_cfg.write();
    }

  // Success!
  return userId;
}

///////////////////////////////////////////////////////////////////////////
//
// NewUser - create new user
//
bool BackupManager::cmdNewUser(const std::string& email, const std::string& password, const std::string& userName)
{
  execAsync(new CmdNewUser(email, password, userName));
  return true;
}

void BackupManager::doNewUser(const CmdNewUser& cmd)
{
    std::string apihost;
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        apihost = m_cfg.m_apihost;
    }
    
    // Now ask server to create this
    ServerConnection conn(apihost, 443, true);
    ServerConnection::Request req(ServerConnection::mGET, "/users/");
    
    
    
    std::string email = cmd.email;
    std::string password = cmd.password;
    std::cout<<m_cfg.m_apihost<<std::endl;
    
    req.setBasicAuth("public-signup",
                     "public-signup");
    
    
    ServerConnection::Reply rep = conn.execute(req);
    
    
    
    
    if (rep.getCode() != 200) {
        MTrace(t_bm, trace::Warn, "Failed to create user: " + rep.toString());
        
        // Notify about the result
        notify(papply<void>(&BackupManagerListener::didFailToCreateUser, rep.toString()));
        return;
    }else {
        using namespace xml;
        std::string parentId;
        const IDocument &ddoc
        = mkDoc(Element("user")
                (Element("id")(CharData<std::string>(parentId))
                 ));
        try {
            std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
            XMLexer lexer(body);
            ddoc.process(lexer);
            
            
            // Success!
            MTrace(t_bm, trace::Info, "Successfully created new user");
            // We access config data
            { MutexLock cfglock(m_cfg.m_lock);
                m_cfg.m_email = cmd.email;
                m_cfg.write();
            }
            
            BackupManager::getInst().registerUser(parentId, cmd);
        } catch (error &e) {
            MTrace(t_bm, trace::Warn, "Failed to parse user details: " + e.toString());
            notify(papply<void>(&BackupManagerListener::didFailToGetUserDetails, rep.toString()));
            return;
        }
    }
    
}

void BackupManager::registerUser(const std::string& idParent, const CmdNewUser& cmd)
{
    std::string apihost;
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        apihost = m_cfg.m_apihost;
    }
    
    // Now ask server to create this
    ServerConnection conn(apihost, 443, true);
    
    std::string m_email = cmd.email;
    std::string m_password = cmd.password;
    
    ServerConnection::Request req(ServerConnection::mPOST, "/users/" +idParent+ "/users/");
    
    req.setBasicAuth("public-signup",
                     "public-signup");
    
    
    using namespace xml;
    const IDocument &doc = mkDoc
    (Element("user_create")
     (
      Element("login")(CharData<std::string>(m_email))
      & Element("password")(CharData<std::string>(m_password))
      )
     );
    
    req.setBody(doc);
    
    ServerConnection::Reply rep = conn.execute(req);
    if (rep.getCode() != 201) {
        MTrace(t_bm, trace::Warn, "Failed to create user: " + rep.toString());
        
        // Notify about the result
        notify(papply<void>(&BackupManagerListener::didFailToCreateUser, rep.toString()));
        return;
    }
    
    std::string header = rep.readHeader();
    
    // Success!
    MTrace(t_bm, trace::Info, "Successfully created new user");
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        m_cfg.m_email = cmd.email;
        if (!header.empty()) m_cfg.m_userID = header;
        m_cfg.write();
    }
    
    if (!header.empty()) {
        BackupManager::getInst().doSetUserDetails(cmd, header);
    }else {
        // Notify about the result
        notify(papply<void>(&BackupManagerListener::didCreateUser));
    }
}
// End NewUser
///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////
//
// GetUserDetails - get all contacts for registered user
//
bool BackupManager::cmdGetUserDetails()
{
  execAsync(new CmdGetUserDetails());
  return true;
}

void BackupManager::doSetUserDetails(const CmdNewUser& cmd, const std::string& userID)
{
    
    Optional<std::string> m_fullName;
    Optional<std::string> email;
    Optional<std::string> type;
    std::string apihost;
    std::string token;
    std::string password;
    
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        apihost = m_cfg.m_apihost;
        email = m_cfg.m_email;
    }
    
    type = "p";
    email = cmd.email;
    m_fullName = cmd.userName;
    
    ServerConnection conn(apihost, 443, true);
    ServerConnection::Request req(ServerConnection::mPOST, "/users/" + userID + "/contacts");
    std::cout<<"usetr password is: " + cmd.password<<std::endl;
    req.setBasicAuth(cmd.email, cmd.password);
    
    
    using namespace xml;
    const IDocument &doc = mkDoc
    (Element("contact")
     (Element("type")(CharData<Optional<std::string> >(type))
      & Element("email")(CharData<Optional<std::string> >(email))
      & Element("fullname")(CharData<Optional<std::string> >(m_fullName))));
    req.setBody(doc);
    
    ServerConnection::Reply rep = conn.execute(req);
    if (rep.getCode() != 201) {
        MTrace(t_bm, trace::Warn, "Failed to create user details: " + rep.toString());
    }
    // Notify about the result
    notify(papply<void>(&BackupManagerListener::didCreateUser));
}

void BackupManager::doGetUserDetails(const CmdGetUserDetails& cmd)
{
  std::string apihost;
  std::string token;
  std::string password;
  std::string userId;

  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    apihost = m_cfg.m_apihost;
    token = m_cfg.m_token.get();
    password = m_cfg.m_password.get();
  }

  try {
    userId = getAuthUserID(token, password);
  } catch (error &e) {
    notify(papply<void>(&BackupManagerListener::didFailToGetUserDetails, e.toString()));
    return;
  }

  ServerConnection conn(apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mGET, "/users/" + userId + "/contacts/p");
  req.setBasicAuth(token, password);

  ServerConnection::Reply rep = conn.execute(req);
  
  if (rep.getCode() != 200) {
    MTrace(t_bm, trace::Warn, "Failed to get user details: " + rep.toString());
    notify(papply<void>(&BackupManagerListener::didFailToGetUserDetails, rep.toString()));
    return;
  }

  using namespace xml;
  const IDocument &ddoc
  = mkDoc(Element("contact")
              (Element("type")(CharData<std::string>(m_userDetails.type))
              & !Element("email")(CharData<Optional<std::string> >(m_userDetails.email))
              & !Element("companyname")(CharData<Optional<std::string > >(m_userDetails.companyname))
              & !Element("fullname")(CharData<Optional<std::string> >(m_userDetails.fullname))
              & !Element("phone")(CharData<Optional<std::string> >(m_userDetails.phone))
              & !Element("street1")(CharData<Optional<std::string> >(m_userDetails.street1))
              & !Element("street2")(CharData<Optional<std::string> >(m_userDetails.street2))
              & !Element("city")(CharData<Optional<std::string> >(m_userDetails.city))
              & !Element("state")(CharData<Optional<std::string> >(m_userDetails.state))
              & !Element("zipcode")(CharData<Optional<std::string> >(m_userDetails.zipcode))
              & !Element("country")(CharData<Optional<std::string> >(m_userDetails.country))));

  try {
    std::istringstream body(std::string(rep.refBody().begin(), rep.refBody().end()));
    XMLexer lexer(body);
    ddoc.process(lexer);
  } catch (error &e) {
    MTrace(t_bm, trace::Warn, "Failed to parse user details: " + e.toString());
    notify(papply<void>(&BackupManagerListener::didFailToGetUserDetails, e.toString()));
    return;
  }

  // Success!
  MTrace(t_bm, trace::Info, "Successfully got user details");
  notify(papply<void>(&BackupManagerListener::didGetUserDetails));
}
// GetUserDetails
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
//
// NewDevice - create new device
//
bool BackupManager::cmdNewDevice(const std::string& devname, const std::string& password)
{
  execAsync(new CmdNewDevice(devname, password));
  return true;
}

void BackupManager::doNewDevice(const CmdNewDevice& cmd)
{
    if (BackupManager::getInst().checkForExistingDevice(cmd.devname, cmd.password)) return;
    
  std::string apihost;
    std::string userId;
    std::string token;
    std::string password;
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    apihost = m_cfg.m_apihost;
      token = m_cfg.m_token.get();
      password = m_cfg.m_password.get();
  }
  
    userId = getAuthUserID(token, password);
    
  // Now ask server to create this
  ServerConnection conn(apihost, 443, true);
  ServerConnection::Request req(ServerConnection::mPOST, "/users/"+ userId + "/devices/");
  
  // We access config data
  { MutexLock cfglock(m_cfg.m_lock);
    req.setBasicAuth(m_cfg.m_deviceTokenL.get(),
                     m_cfg.m_deviceTokenP.get());
  }
  
  std::string devname = cmd.devname;
  using namespace xml;
  const IDocument &doc = mkDoc
  (Element("pc")
   (Element("name")(CharData<std::string>(devname))));
  
  req.setBody(doc);
  
  ServerConnection::Reply rep = conn.execute(req);
  if (rep.getCode() != 201 && rep.getCode() != 409) {
    MTrace(t_bm, trace::Warn, "Failed to create device: " + rep.toString());
    notify(papply<void>(&BackupManagerListener::didFailToCreateDevice, rep.toString()));
    return;
  }else if (rep.getCode() == 409){
      // Notify about the result
      notify(papply<void>(&BackupManagerListener::didCreateDevice));
      return;
  }

    std::string deviceId;
    deviceId = rep.readHeader();
    std::cout<<"header is: "<<deviceId<<std::endl;
    
    
    // Success!
    MTrace(t_bm, trace::Info, "Successfully created and set new device");
    // We access config data
    { MutexLock cfglock(m_cfg.m_lock);
        m_cfg.m_device = cmd.devname;
        m_cfg.m_deviceID = deviceId;
        m_cfg.write();
    }
    
    
  
  // Notify about the result
  notify(papply<void>(&BackupManagerListener::didCreateDevice));
}

// End NewDevice
///////////////////////////////////////////////////////////////////////////

bool BackupManager::checkForExistingDevice(const std::string& devname, const std::string& password)
{
    std::string apihost;
    std::string userId;
    std::string email;
    std::string passowrd;
    std::string token;
    
    { MutexLock cfglock(m_cfg.m_lock);
        apihost = m_cfg.m_apihost;
        email = m_cfg.m_email.get();
        userId = m_cfg.m_userID.get();
        passowrd = m_cfg.m_password.get();
    }
    
//    passowrd = cmd.password;
    
    token = m_cfg.m_token.get();
    
    // Now ask server to create this
    ServerConnection conn(apihost, 443, true);
    ServerConnection::Request req(ServerConnection::mGET, "/users/" + userId + "/devices/");
    req.setBasicAuth(token, passowrd);
    
    ServerConnection::Reply rep = conn.execute(req);
    
    
    if (rep.getCode() != 200) {
        MTrace(t_bm, trace::Warn, "Failed to get list of devices: " + rep.toString());
        notify(papply<void>(&BackupManagerListener::didFailToCreateDevice, rep.toString()));
        return false;
    }
    
    std::string responce(rep.refBody().begin(), rep.refBody().end());
    
    std::size_t found = responce.rfind(devname);
    
    if (found!=std::string::npos) {
        
        std::string substring = responce.substr (0,found);
        std::string replacing ("<guid>");
        
        found = substring.rfind(replacing, found-1);
        std::string thirdIteration = substring.substr(found);
        
        std::string deviceUID;
        
        std::string strArr[] = {"<guid>", "</guid>", "<name>"};
        
        std::vector<std::string> strVec(strArr, strArr + 3);
        
        for (int i = 0; i < strVec.size(); i++) {
            std::string str = strVec[i];
            thirdIteration = BackupManager::getInst().replaceStringWithString(thirdIteration, str, "");
        }
        
        deviceUID = thirdIteration;
        
        
        { MutexLock cfglock(m_cfg.m_lock);
            m_cfg.m_device = devname;
            m_cfg.m_deviceID = deviceUID;
            m_cfg.write();
        }
        
        notify(papply<void>(&BackupManagerListener::didCreateDevice));
        return true;
    }
    
    notify(papply<void>(&BackupManagerListener::didFailToCreateDevice, rep.toString()));
    return false;
}

std::string BackupManager::replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr)
{
    std::string strToReplace = replaceableStr;
    std::string strReplacing = replacingStr;
    std::string wordReplacing =replacingWord;
    
    std::size_t found;
    found = replaceableStr.rfind(wordReplacing);
    if (found != std::string::npos) strToReplace.replace (found, wordReplacing.length(), strReplacing);
    
    return strToReplace;
}

// End CheckDevice
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
//
// Monitor - start monitoring for changes
//
bool BackupManager::cmdMonitor()
{
  execAsync(new CmdMonitor());
  return true;
}

void BackupManager::doMonitor(const CmdMonitor& cmd)
{
  MutexLock l(m_engineCallbackVars);

  try {
    delete m_uploadManager;
    delete m_serverConnection;

    // If we have credentials, set up the upload engine
    { MutexLock cfglock(m_cfg.m_lock);
      m_serverConnection = new ServerConnection(m_cfg.m_apihost, 443, true);
      m_serverConnection->setDefaultBasicAuth(m_cfg.m_deviceTokenL.get(),
                                             m_cfg.m_deviceTokenP.get());
      
      m_uploadManager = new UploadManager(m_fsCache, *m_serverConnection, m_cfg.m_deviceID.get(), m_cfg.m_userID.get());
    }

    m_uploadManager->setFilter(papply(this, &BackupManager::backupFilter));
    m_uploadManager->setProgressLog(papply(this, &BackupManager::progressNotification));

//    m_uploadManager->setChangeNotification(papply(this,
//                                                        &BackupManager::changeNotification));
//    m_uploadManager->setCompletionNotification(papply(this,
//                                                            &BackupManager::snapshotNotification));

    m_uploadManager->addUploadRoot("/");
    m_uploadManager->addUploadRoot("/Volumes");
    
    // Go!
    std::vector<std::string> includePaths;
    includePaths.reserve(m_backupFolders.size());
    for(includeFolders_t::const_iterator it = m_backupFolders.begin(); it != m_backupFolders.end(); ++it) {
      m_uploadManager->addPathMonitor(it->first);
    }
    
    m_uploadManager->startUploadAllRoots();

    // Notify about the result
    notify(papply<void>(&BackupManagerListener::didStartMonitoring));
  } catch(error& e) {
    MTrace(t_bm, trace::Warn, "Failed to start monitoring: " + e.toString());
    // Notify about the result
    notify(papply<void>(&BackupManagerListener::didFailToStartMonitoring, e.toString()));
  }
}

bool BackupManager::cmdStopMonitor()
{
  execAsync(new CmdStopMonitor());
  return true;
}

void BackupManager::doStopMonitor(const CmdStopMonitor& cmd)
{
  MutexLock l(m_engineCallbackVars);

  try {
    delete m_uploadManager;
    m_uploadManager = 0;
    
    delete m_serverConnection;
    m_serverConnection = 0;

    // Notify about the result
    notify(papply<void>(&BackupManagerListener::didStopMonitoring));
  } catch(error& e) {
    MTrace(t_bm, trace::Warn, "Failed to stop monitoring: " + e.toString());
    // Notify about the result
    notify(papply<void>(&BackupManagerListener::didFailToStopMonitoring, e.toString()));
  }
}
// End Monitor
///////////////////////////////////////////////////////////////////////////

bool BackupManager::cmdStop()
{
  return true;
}

bool BackupManager::cdpFilter(const std::string &o)
{
  return filter(o, m_backupFolders, false);
}

bool BackupManager::backupFilter(const std::string &o)
{
  return filter(o, m_backupFolders);
}

bool BackupManager::sizeCalcFilter(const std::string &o)
{
  return filter(o, m_sizeScanFolders);
}

bool BackupManager::filter(const std::string &o, const includeFolders_t &includeFolders, bool bIncludeParents)
{
  MutexLock l(m_skip_lock);

  if (includeFolders.empty()) {
    // Include all folders if no filter was specified
    return true;
  }

  MTrace(t_bm, trace::Debug, "scanFilter:"+o);

  // Find intermediary folders that a parents to our Included folder,
  // and therefore should be included into backup set
  // Note. that step can be optimized by creating a map(set) with all intermediary paths for Included folder
  for (includeFolders_t::const_iterator it = includeFolders.begin();
       it != includeFolders.end(); ++it) {
    const std::string& includePath = it->first;
    if (includePath.compare(0, o.length(), o) == 0) {
      MTrace(t_bm, trace::Debug, "scanFilter Included:"+o);
      // This is an intermediate folder in the path of one of the Includes, add
      return true;
    }
  }
  
  std::string currentIncludePath = o;
  while (!currentIncludePath.empty()) {
    MTrace(t_bm, trace::Debug, "Search in Includes:"+currentIncludePath);
    includeFolders_t::const_iterator includeIt = includeFolders.find(currentIncludePath);
    if (includeIt != includeFolders.end()) {
      // Check, whether current subpath is among the Excluded ones
      std::string currentExcludePath = o;
      while (!includeIt->second.empty() && currentExcludePath.size() > currentIncludePath.size()) {
        MTrace(t_bm, trace::Debug, "Search in Excludes:"+currentExcludePath);
        if (includeIt->second.find(currentExcludePath) != includeIt->second.end()) {
          // Folder found in Excludes of current Include, skip
          return false;
        }
        currentExcludePath = currentExcludePath.substr(0,currentExcludePath.find_last_of("/"));
      }
      // Folder found in Includes and NOT found in Excludes, add
      return true;
    }
    
    if (currentIncludePath == "/") {
      break;
    }
    currentIncludePath = currentIncludePath.substr(0,currentIncludePath.find_last_of("/"));
    if (currentIncludePath.empty()) {
      currentIncludePath = "/";
    }
  }
  
  // Folder not found in Includes, skip
  return false;
}

///////////////////////////////////////////////////////////////////////////
//
// Start calculating sizes of selected backup files/foldes
//
bool BackupManager::cmdCalcFileSizes()
{
  MutexLock l(m_engineCallbackVars);
  if (m_folderSizeCalc.active()) {
    m_folderSizeCalc.stop();
  }
  m_backupFoldersIt = m_backupFolders.begin();
  if (m_backupFoldersIt != m_backupFolders.end()) {
    m_folderSizeCalc.setFilter(papply(this, &BackupManager::sizeCalcFilter));
    m_includeFoldersSizes[m_backupFoldersIt->first] = -1;
    m_sizeScanFolders = includeFolders_t();
    m_sizeScanFolders.insert(std::make_pair(m_backupFoldersIt->first, m_backupFoldersIt->second));
    m_folderSizeCalc.setRoot(m_backupFoldersIt->first);
    m_folderSizeCalc.start();
  }

  return true;
}

void BackupManager::setBackupFolders(const includeFolders_t &backupFolders)
{
  MutexLock l(m_engineCallbackVars);
  if (m_folderSizeCalc.active()) {
    m_folderSizeCalc.stop();
  }
  m_backupFolders = backupFolders;
  m_includeFoldersSizes.clear();
}

bool BackupManager::didCalcFileSize(uint64_t result)
{
  MutexLock l(m_engineCallbackVars);
  m_includeFoldersSizes[m_backupFoldersIt->first] = result;
  notify(papply<void>(&BackupManagerListener::didCalculateFileSize, m_backupFoldersIt->first));
  if (++m_backupFoldersIt != m_backupFolders.end()) {
    m_sizeScanFolders = includeFolders_t(m_backupFoldersIt, m_backupFoldersIt);
    m_sizeScanFolders.insert(std::make_pair(m_backupFoldersIt->first, m_backupFoldersIt->second));
    m_folderSizeCalc.setRoot(m_backupFoldersIt->first);
    // Should make another calculation iteration
    return true;
  }
  // Stop
  return false;
}

uint64_t BackupManager::fileSizeForPath(const std::string &path)
{
  MutexLock l(m_engineCallbackVars);
  includeFoldersSizes_t::const_iterator it = m_includeFoldersSizes.find(path);
  if (it != m_includeFoldersSizes.end()) {
    return it->second;
  }
  return -1;
}

void BackupManager::progressNotification(UploadManager &uploadManager)
{
  //MTrace(t_bm, trace::Debug, "Got progressNotification");
  notify(papply<void>(&BackupManagerListener::didGetProgressInfo));
}

const std::vector<Upload::threadstatus_t>& BackupManager::getProgressInfo()
{
  MutexLock l(m_engineCallbackVars);
  if (m_uploadManager) {
    return m_uploadManager->getProgressInfo();
  }
  static std::vector<Upload::threadstatus_t> emptyProgress;
  return emptyProgress;
}

UserDetails BackupManager::getUserDetails()
{
  MutexLock l(m_engineCallbackVars);
  return m_userDetails;
}
/*
void BackupManager::changeNotification(const std::string &path)
{
  MutexLock l(m_engineCallbackVars);
  //MTrace(t_bm, trace::Debug, "Got changeNotification");
  notify(papply<void>(&BackupManagerListener::didGetChangeInfo, &path));
}

void BackupManager::snapshotNotification(Upload*)
{
  MutexLock l(m_engineCallbackVars);
  //MTrace(t_bm, trace::Debug, "Got snapshotNotification");
  notify(papply<void>(&BackupManagerListener::didCreateSnapshot));
}
*/

void BackupManager::oneSecondTimer()
{
  MutexLock l(m_engineCallbackVars);
  if (m_uploadManager) {
    m_uploadManager->oneSecondTimer();
  }
}