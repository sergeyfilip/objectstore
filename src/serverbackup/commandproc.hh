///
/// Command line processor for the restore utility
///
//
// $Id: commandproc.hh,v 1.5 2013/06/17 13:24:42 joe Exp $
//

#ifndef SERVERBACKUP_COMMANDPROC_HH
#define SERVERBACKUP_COMMANDPROC_HH

#include "client/serverconnection.hh"
#include "objparser/objparser.hh"
#include "common/time.hh"
#include <stack>

class CommProc {
public:
  CommProc(ServerConnection &conn);

  /// Call this method to start the command line processor. This
  /// method returns when the user has quit the command line.
  void run();


private:
  /// Our server connection
  ServerConnection &m_conn;

  /// Object download. Will retry on retry-able errors.
  std::vector<uint8_t> getObject(const sha256 &);

  void cmd_help(const std::vector<std::string>&);
  void cmd_ls(const std::vector<std::string>&);
  void cmd_cd(const std::vector<std::string>&);
  void cmd_get(const std::vector<std::string>&);
  void cmd_rawget(const std::vector<std::string>&);

  void cmd_ls_devices();
  bool printLine(const std::string &);

  void cmd_ls_snapshots();

  void cmd_ls_directory();

  /// Given a device (m_device) and a timestamp (m_snaptime), set the
  /// first element of the path to hold the hash that matches
  void init_root_path();
  /// filter function used by init_root_path - given a hash and a
  /// tstamp from the XML it may init the path.
  bool filterSnapHash(const Time &, const std::string &);

  /// Restore the given directory under the given local directory
  void restoreDirectory(const objseq_t&, const std::string &);

  /// Utility routine for printing dir/file permissions
  static std::string printPerm(uint32_t);
  /// Same, for file size
  static std::string printSize(uint64_t);
  /// Same, for mtime/ctime
  static std::string printDate(uint64_t);

  /// Convert string user to numeric user
  static uid_t str2user(const std::string &);
  /// Convert string group to numeric group
  static gid_t str2group(const std::string &);

  /// The currently selected device, if any (empty if none)
  std::string m_device;

  /// The currently selected backup history item timestamp, if any
  /// (BEGINNING_OF_TIME if none)
  Time m_snaptime;

  /// The current path (stack of name,hash pairs)
  typedef std::list<FSDir::dirent_t> path_t;
  path_t m_path;

  /// Utility routine for printing the given path
  static std::string printPath(const path_t &);
};

#endif
