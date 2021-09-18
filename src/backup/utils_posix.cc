//
//  utils_posix.cc
//  Keepit
//
//  Created by vg on 5/15/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#include <string.h>
#include <errno.h>

uint64_t FolderSizeCalc::calculateSizeOfBackup(const std::string &path)
{
  MTrace(t_util, trace::Debug, "Handling children under " << path);
  
  uint64_t totalSize = 0;
  
  // Get file system info
  objinfo_t objinfo;
  objinfo.abspath = path;

  { int statrc;
    while ((statrc = statfs(path.c_str(), &objinfo.fs)) && errno == EINTR);
    if (statrc) {
      // If we fail for any reason, skip
      MTrace(t_util, trace::Info, "Skipping (due to statfs error: "
             << strerror(errno) << ") " << path);
      return 0;
    }
  }
  
  // Stat this to see what it is
  if (-1 == lstat(path.c_str(), &objinfo.st)) {
    // If lstat fails on this object for any reason, we log it and
    // skip it
    MTrace(t_util, trace::Info, "Scan skipping (due to lstat error: "
           << strerror(errno) << ") " << path);
    return 0;
  }
  
  //
  // Check with filter...
  //
  if (m_filter) {
    if (!(*m_filter)(objinfo.abspath)) {
      MTrace(t_util, trace::Debug, "Filter skipping " << objinfo.abspath);
      return 0;
    }
  }

  // This is a file
  if (S_ISREG(objinfo.st.st_mode)) {
    return objinfo.st.st_size;
  }

  // Return if this is not a folder
  if (!S_ISDIR(objinfo.st.st_mode)) {
    return 0;
  }

  // Traverse directory...
  DIR *dir = opendir(path.c_str());
  if (!dir) {
    // If we fail for any reason, skip
    MTrace(t_util, trace::Info, "Skipping (due to opendir error: "
           << strerror(errno) << ") " << path);
    return totalSize;
  }
  ON_BLOCK_EXIT(closedir, dir);
  
  struct dirent *de;
  while ((de = readdir(dir))) {
    if (m_shouldexit) {
      throw error("Exit requested");
    }

    // Skip dummy entries
    { const std::string entry(de->d_name);
      if (entry == "." || entry == "..")
        continue;
    }
    
    // We pass this struct on to the filter later on
    objinfo.abspath = (path == "/" ? path : (path + "/")) + de->d_name;
    
    totalSize += calculateSizeOfBackup(objinfo.abspath);
    
//    // Stat this to see what it is
//    if (-1 == lstat(objinfo.abspath.c_str(), &objinfo.st)) {
//      // If lstat fails on this object for any reason, we log it and
//      // skip it
//      MTrace(t_util, trace::Info, "Scan skipping (due to lstat error: "
//             << strerror(errno) << ") " << objinfo.abspath);
//      continue;
//    }
//    
//    //
//    // Check with filter...
//    //
//    if (m_filter) {
//      if (!(*m_filter)(objinfo)) {
//        MTrace(t_util, trace::Debug, "Filter skipping " << objinfo.abspath);
//        continue;
//      }
//    }
//    
//    // If this is a directory
//    if (S_ISDIR(objinfo.st.st_mode) && !S_ISLNK(objinfo.st.st_mode)) {
//      totalSize += calculateSizeOfBackup(objinfo.abspath);
//    } else if (S_ISREG(objinfo.st.st_mode)) {
//      totalSize += objinfo.st.st_size;
//    }
  }
  
  return totalSize;
}

