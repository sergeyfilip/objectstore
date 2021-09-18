//
//  utils.hh
//  Keepit
//
//  Created by vg on 5/15/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#ifndef Keepit_utils_hh
#define Keepit_utils_hh

#include "common/partial.hh"
#include "common/thread.hh"
#include "common/mutex.hh"

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/vfs.h>
#endif

#if defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(_WIN32)
# include <windows.h>
#endif

#include <string>
#include <stdint.h>

//
/// The filter is passed the following parameters, useful for its
/// filtering:
//
struct objinfo_t {
  /// absolute path of object
  std::string abspath;
#if defined(__unix__) || defined(__APPLE__)
  struct statfs fs;
  struct stat st;
#endif
#if defined(_WIN32)
  BY_HANDLE_FILE_INFORMATION fi;
#endif
};

class FolderSizeCalc : public Thread {
public:
  FolderSizeCalc(const BindF1Base<bool,uint64_t> &);
  /// We must destroy allocated structures
  ~FolderSizeCalc();
  
  /// Include a filter for exclude filtering. This closure is applied
  /// on every file system object we encounter, and if it returns
  /// false the object is skipped.
  //
  /// Note; the filter closure will be call concurrently by several
  /// worker threads. It must be re-entrant.
  FolderSizeCalc &setFilter(const BindF1Base<bool,const std::string &> &);

  /// Set path to the folder size of which we calculate
  FolderSizeCalc &setRoot(const std::string &path);

  void stop();
  /// Query whether the size calc is working
  bool isWorking();

protected:
  void run();
  uint64_t calculateSizeOfBackup(const std::string &path);

private:
  /// Protect against assignment
  FolderSizeCalc &operator=(const FolderSizeCalc&);
  
  /// absolute path of object
  std::string m_root;

  /// Callback with result
  const BindF1Base<bool,uint64_t> *m_reportResult;

  /// Our exclude filter
  const BindF1Base<bool,const std::string &> *m_filter;
  
  //! Should we exit?
  bool m_shouldexit;
};

#endif
