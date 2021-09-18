//
/// Configuration file parsing
//
// $Id: config.hh,v 1.9 2013/10/16 08:49:25 sf Exp $
//

#ifndef SERVERBACKUP_CONFIG_HH
#define SERVERBACKUP_CONFIG_HH

#include <string>
#include <set>
#include <stdint.h>

#include "common/optional.hh"
#include "common/time.hh"
#include "common/mutex.hh"

//! Client configuration file parser - we store our access token in a
//! configuration file along with file exclusion filters etc.
class Config {
public:
  Config(const std::string& cn);

  //! Whenever you read or write configuration members, you need to
  //! hold this mutex!  You also need to hold it before calling read()
  //! or write().
  Mutex m_lock;

  //! Read config
  Config &read();

  //! Write config
  Config &write();

  //! API server hostname
  std::string m_apihost;

  //! Configuration interface socket
  std::string m_cmdsocket;

  //! Access token - if configured
  Optional<std::string> m_token;
  //! Access token password - if configured
  Optional<std::string> m_password;

  //! Device name - if configured
  Optional<std::string> m_device;
 //! Device id - if configured
  Optional<std::string> m_device_id;
  
  //!User id - if configured
  Optional<std::string> m_user_id;

  //! Cache file name
  std::string m_cachename;

  //! Optional - CDP trigger timeout - or unset if no CDP trigger
  Optional<DiffTime> m_cdp;

  //! Number of worker threads to use for backup
  size_t m_workers;

  //! List of file system types to exclude from the backup. If none
  //! are mentioned in the configuration file we set a default list
  //! of: tmpfs, proc, sysfs, devpts, rpc_pipefs
  std::set<std::string> m_skiptypes;

  //! List of specific directories to skip
  std::set<std::string> m_skipdirs;

private:
  enum op_t { op_r, op_w };
  //! Document handler - read or write
  Config &doch(op_t);
  //! File name
  std::string m_file;

  //! When we get a skiptype add it to our list
  bool  gotSkipType(const std::string &);

  //! When we want to get a skiptype from our list
  bool getSkipType(std::set<std::string>::iterator&, std::string&);

  //! When we get a skipdir add it to our list
  bool gotSkipDir(const std::string &);

  //! When we want to get a skipdir from our list
  bool  getSkipDir(std::set<std::string>::iterator&, std::string&);

};


#endif
