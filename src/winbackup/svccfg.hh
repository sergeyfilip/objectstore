///
/// Our service configuration
///
//
// $Id: svccfg.hh,v 1.6 2013/10/09 14:34:30 sf Exp $
//

#ifndef WINBACKUP_SVCCFG_HH
#define WINBACKUP_SVCCFG_HH

#include <string>
#include <list>

/// Our configuration
class SvcCfg {
public:
  SvcCfg();
  /// Set file name
  SvcCfg &setFName(const std::string &);
  /// Read config
  SvcCfg &read();
  /// Write config
  SvcCfg &write();

  /// Back end server to connect to
  std::string m_ngserver;
  /// Optional access token name
  std::string m_aname;
  /// Optional access token password
  std::string m_apass;
  /// Optional device name
  std::string m_devname;

  /// Optional user token name
  std::string m_uname;
  /// Optional user token password
  std::string m_upass;
  /// User id
  std::string m_user_id;
  /// Device id
  std::string m_device_id;


private:
  enum op_t { op_r, op_w };
  std::string m_fname;
  /// Document handler - read or write config
  SvcCfg &doch(op_t);
};

/// Application definitions configuration
class AppDefs {
public:
  /// Initialize from file
  AppDefs(const std::string &);

  /// List of LocalAppData excludes
  std::list<std::string> m_lad_ex;

private:
  bool gotLadEx(const std::string&);
};



#endif

