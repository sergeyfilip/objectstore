//
//  utils.cc
//  Keepit
//
//  Created by vg on 5/15/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#include "utils.hh"
#include "common/trace.hh"
#include "common/error.hh"
#include "common/scopeguard.hh"
#include "common/time.hh"
#include "common/string.hh"
#include "xml/xmlio.hh"

#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <dirent.h>
# include <pwd.h>
# include <grp.h>
# include <errno.h>
#endif

namespace {
  //! Tracer for utils
  trace::Path t_util("/util");
}

FolderSizeCalc::FolderSizeCalc(const BindF1Base<bool,uint64_t> &f)
: m_reportResult(f.clone())
, m_filter(0)
, m_shouldexit(false)
{
}

FolderSizeCalc::~FolderSizeCalc()
{
  delete m_reportResult;
  delete m_filter;
}

FolderSizeCalc &FolderSizeCalc::setFilter(const BindF1Base<bool,const std::string &> &f)
{
  delete m_filter;
  m_filter = f.clone();
  return *this;
}

FolderSizeCalc &FolderSizeCalc::setRoot(const std::string &path)
{
  m_root = path;
  return *this;
}

void FolderSizeCalc::stop()
{
  m_shouldexit = true;
  join_nothrow();
}

void FolderSizeCalc::run()
{
  try {
    uint64_t size;
    m_shouldexit = false;
    do {
      size = calculateSizeOfBackup(m_root);
      MTrace(t_util, trace::Info, "The size of: " << m_root << "is: " << size << " bytes");
    } while (!m_shouldexit && (*m_reportResult)(size));
  } catch (error &e) {
    MTrace(t_util, trace::Warn, "Aborting folder size calculation: " << e.toString());
  }
}

bool FolderSizeCalc::isWorking()
{
  return active();
}

#if defined(__unix__) || defined(__APPLE__)
# include "utils_posix.cc"
#endif

#if defined(_WIN32)
# include "utils_win32.cc"
#endif
