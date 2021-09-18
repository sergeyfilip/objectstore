//
/// Configuration file parsing
//
// $Id: config.hh,v 1.4 2013/03/20 15:14:22 joe Exp $
//

#ifndef SERVERBACKUP_CONFIG_HH
#define SERVERBACKUP_CONFIG_HH

#include <string>
#include <stdint.h>

#include "common/optional.hh"
#include "common/time.hh"
#include "common/mutex.hh"

//! Client configuration file parser - we store our access token in a
//! configuration file along with file exclusion filters etc.
class Config
{
public:
  Config();

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

  //! Email - if configured
  Optional<std::string> m_email;

  //! Access token - if configured
  Optional<std::string> m_token;
  //! Access token password - if configured
  Optional<std::string> m_password;

  //! Device name - if configured
  Optional<std::string> m_device;
    
    //! Device id - if configured
    Optional<std::string> m_deviceID;

  //! Access token - if configured
  Optional<std::string> m_deviceTokenL;
  //! Access token password - if configured
  Optional<std::string> m_deviceTokenP;

    Optional<std::string> m_userID;
    
  //! Cache file name
  std::string m_cachename;
};

#endif
