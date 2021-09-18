#pragma once
#include <map>
#include <set>
#include <queue>
#include "common/singleton.hh"
#include "common/thread.hh"
#include "common/notifier.hh"
#include "common/mutex.hh"
#include "backup/metatree.hh"
#include "backup/upload.hh"
#include "config.hh"

// Service listener interface
struct BackupManagerListener
{
  // Service operation status
  virtual int onBackupManagerReady() {return 0;}
  virtual void onBackupManagerStopped() {}
  virtual void onBackupManagerError() {}
  
  // Service requests callbacks
  virtual void didRegister() {}
  virtual void didFailToRegister(const std::string explanation) {}
  
  virtual void didDeregister() {}
  virtual void didFailToDeregister(const std::string explanation) {}
  
  virtual void didCreateUser() {}
  virtual void didFailToCreateUser(const std::string explanation) {}
  
  virtual void didGetUserDetails() {}
  virtual void didFailToGetUserDetails(const std::string explanation) {}
  
  virtual void didCreateDevice() {}
  virtual void didFailToCreateDevice(const std::string explanation) {}
  
  virtual void didCreateSnapshot() {}
  virtual void didFailToCreateSnapshot(const std::string explanation) {}
  
  virtual void didCalculateFileSize(const std::string path) {}

  virtual void didGetProgressInfo() {}
  
  virtual void didGetChangeInfo(const std::string *path) {}
  
  virtual void didStartMonitoring() {}
  virtual void didFailToStartMonitoring(const std::string explanation) {}
  
  virtual void didStopMonitoring() {}
  virtual void didFailToStopMonitoring(const std::string explanation) {}
  
  virtual void didStartUpload() {}
  virtual void didFailToStartUpload(const std::string explanation) {}
  
  virtual void didStopUpload() {}
  virtual void didFailToStopUpload(const std::string explanation) {}
};

struct UserDetails
{
  std::string type;
  Optional<std::string> email;
  Optional<std::string> companyname;
  Optional<std::string> fullname;
  Optional<std::string> phone;
  Optional<std::string> street1;
  Optional<std::string> street2;
  Optional<std::string> city;
  Optional<std::string> state;
  Optional<std::string> zipcode;
  Optional<std::string> country;
};

class BackupManager
: public Singleton<BackupManager>
, public Thread
, public Notifier<BackupManagerListener>
{
public:
  BackupManager(Config &cfg);
  virtual ~BackupManager(void);
  
    void updateHost();
    
    void changeCache();
    
  void start(void);
  //! Signal command processor to exit
  void stop(void);
  
  //! Returns true if the main application should exit
  bool shouldExit() const;
  
  std::string getEmail();
  std::string getDeviceName();

  bool isMonitoring();
  bool isUploading();
  bool isCalculating();
  const std::vector<Upload::threadstatus_t>& getProgressInfo();
  UserDetails getUserDetails();

  typedef std::set<std::string> excludeFolders_t;
  typedef std::map<std::string, excludeFolders_t> includeFolders_t;
  typedef std::map<std::string, uint64_t> includeFoldersSizes_t;

  bool cmdRegister(const std::string& email, const std::string& password);
  bool isRegistered();
    //addition methods that check userID and deviceID if not, re-register user
    bool checkRegistration();
    bool doReRegisterUser();
    
  bool cmdDeregister();
  bool cmdNewUser(const std::string& email, const std::string& password, const std::string& userName);
  bool cmdGetUserDetails();
  bool cmdNewDevice(const std::string& devname, const std::string& password);
  bool cmdMonitor();
  bool cmdStopMonitor();
  bool cmdStop();
  bool cmdCalcFileSizes();
  uint64_t fileSizeForPath(const std::string &path);

  void setBackupFolders(const includeFolders_t& backupFolders);
  
  /// This routine is externally called every second
  void oneSecondTimer();

protected:
  struct CmdBase {
    virtual ~CmdBase() {}
  };
  
  struct CmdRegister: public CmdBase {
    CmdRegister(const std::string& anEmail, const std::string& aPassword)
    : email(anEmail)
    , password(aPassword)
    {}
    std::string email;
    std::string password;
  };
  
  struct CmdDeRegister: public CmdBase {
  };
  
  struct CmdNewUser: public CmdBase {
    CmdNewUser(const std::string& anEmail, const std::string& aPassword, const std::string& anUserName)
    : email(anEmail)
    , password(aPassword)
    , userName(anUserName)
    {}
    std::string email;
    std::string password;
    std::string userName;
  };
  
  struct CmdGetUserDetails: public CmdBase {
  };
  
  struct CmdNewDevice: public CmdBase {
    CmdNewDevice(const std::string& anDevname, const std::string& anPassword)
    : devname(anDevname)
      , password(anPassword)
    {}
    std::string devname;
      std::string password;
  };
  
  struct CmdMonitor: public CmdBase {
  };
  
  struct CmdStopMonitor: public CmdBase {
  };
  
  struct CmdStopUpload: public CmdBase {
  };
  
  //! Our command processor
  void run();
  
  void wakeUpCmdThread();
  void execAsync(CmdBase* cmd);
  
  void doRegister(const CmdRegister& cmd);
  void doDeregister(const CmdDeRegister& cmd);
  void doNewUser(const CmdNewUser& cmd);
    
#warning next two methods are new, for updated API
    void registerUser(const std::string& idParent, const CmdNewUser& cmd);
    void doSetUserDetails(const CmdNewUser& cmd, const std::string& userID);
    
  void doGetUserDetails(const CmdGetUserDetails& cmd);
    
#warning next method for checking list of devices
    bool checkForExistingDevice(const std::string& devname, const std::string& password);
    
  void doNewDevice(const CmdNewDevice& cmd);
  void doMonitor(const CmdMonitor& cmd);
  void doStopMonitor(const CmdStopMonitor& cmd);

  bool didCalcFileSize(uint64_t result);

  bool filter(const std::string &o, const includeFolders_t &includeFolders, bool bIncludeParents = true);
  bool cdpFilter(const std::string &o);
  bool backupFilter(const std::string &o);
  bool sizeCalcFilter(const std::string &o);
  
  /// This method is called from our backup engine whenever it has new
  /// progress info
  void progressNotification(UploadManager&);

  /// This method is called when CDP detects changes at certain path
  //void changeNotification(const std::string &path)

  /// This method is called when snapshot entry is created on server
  //void snapshotNotification(Upload*);
  
  /// Helper routine to retrieve information (ID) about the authenticated user
  std::string getAuthUserID(const std::string &u, const std::string &p);
    
    
    std::string replaceStringWithString(const std::string& replaceableStr, const std::string& replacingWord, const std::string& replacingStr);

private:
  //! Configuration reference
  Config &m_cfg;
  
  //! File system cache
  FSCache m_fsCache;
  
  //! Object for handling communication with server
  ServerConnection* m_serverConnection;
  
  UploadManager *m_uploadManager;

  /// We use a semaphore for waiting on work queue items
  Semaphore m_cmdQueueSem;
  Mutex m_cmdQueueLock;

  //! Our current command buffer that is being read into
  Mutex m_cmdMutex;
  
  /// Mutex on skip list
  Mutex m_skip_lock;
  
  /// Mutex on engine callback variables
  Mutex m_engineCallbackVars;

  //! Should we exit?
  bool m_shouldexit;

  includeFolders_t m_backupFolders;
  includeFolders_t::const_iterator m_backupFoldersIt;

  FolderSizeCalc m_folderSizeCalc;
  includeFolders_t m_sizeScanFolders;
  includeFoldersSizes_t m_includeFoldersSizes;

  std::queue<CmdBase*> m_cmdQueue;
  
  UserDetails m_userDetails;
};
