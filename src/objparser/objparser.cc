///
/// Object parsing utilities
///
// $Id: objparser.cc,v 1.6 2013/07/25 08:41:13 vg Exp $
//

#include "objparser.hh"
#include "common/time.hh"
#include "common/trace.hh"
#include <sstream>
#include <iomanip>

namespace {
  trace::Path t_obj("/objparser");
}

struct lom_unix_regfile {
  lom_unix_regfile(const std::vector<uint8_t> &obj, size_t &ofs);
  std::string name;
  std::string owner;
  std::string group;
  uint32_t permissions;
  uint64_t ctime;
  uint64_t mtime;
  uint64_t filesize;
};

struct lom_unix_directory {
  lom_unix_directory(const std::vector<uint8_t> &obj, size_t &ofs);
  std::string name;
  std::string owner;
  std::string group;
  uint32_t permissions;
  uint64_t ctime;
  uint64_t mtime;
};

struct lom_windows_regfile {
  lom_windows_regfile(const std::vector<uint8_t> &obj, size_t &ofs);
  std::string name;
  std::string owner;
  uint32_t attributes;
  std::string sddl;
  uint64_t mtime;
  uint64_t btime;
  uint64_t filesize;
};

struct lom_windows_directory {
  lom_windows_directory(const std::vector<uint8_t> &obj, size_t &ofs);
  std::string name;
  std::string owner;
  uint32_t attributes;
  std::string sddl;
  uint64_t mtime;
  uint64_t btime;
};

std::string lom_entry_extract_name(const std::vector<uint8_t> &obj, size_t &ofs)
{
  // Offset 0, 1 byte, meta type.
  const uint8_t mt = des<uint8_t>(obj, ofs);
  switch (mt) {
  case 0x01: // UNIX regular file
    return lom_unix_regfile(obj, ofs).name;
  case 0x02: // UNIX directory
    return lom_unix_directory(obj, ofs).name;
  case 0x11: // Windows regular file
    return lom_windows_regfile(obj, ofs).name;
  case 0x12: // Windows directory
    return lom_windows_directory(obj, ofs).name;
  default:
    throw error("Unsupported LoM entry");
  }
}

lom_unix_regfile::lom_unix_regfile(const std::vector<uint8_t> &obj, size_t &ofs)
{
  name = des<std::string>(obj, ofs);
  owner = des<std::string>(obj, ofs);
  group = des<std::string>(obj, ofs);
  permissions = des<uint32_t>(obj, ofs);
  ctime = des<uint64_t>(obj, ofs);
  mtime = des<uint64_t>(obj, ofs);
  filesize = des<uint64_t>(obj, ofs);
}

lom_unix_directory::lom_unix_directory(const std::vector<uint8_t> &obj, size_t &ofs)
{
  name = des<std::string>(obj, ofs);
  owner = des<std::string>(obj, ofs);
  group = des<std::string>(obj, ofs);
  permissions = des<uint32_t>(obj, ofs);
  ctime = des<uint64_t>(obj, ofs);
  mtime = des<uint64_t>(obj, ofs);
}

lom_windows_regfile::lom_windows_regfile(const std::vector<uint8_t> &obj, size_t &ofs)
{
  name = des<std::string>(obj, ofs);
  owner = des<std::string>(obj, ofs);
  attributes = des<uint32_t>(obj, ofs);
  sddl = des<std::string>(obj, ofs);
  mtime = des<uint64_t>(obj, ofs);
  btime = des<uint64_t>(obj, ofs);
  filesize = des<uint64_t>(obj, ofs);
}

lom_windows_directory::lom_windows_directory(const std::vector<uint8_t> &obj,
                                             size_t &ofs)
{
  name = des<std::string>(obj, ofs);
  owner = des<std::string>(obj, ofs);
  attributes = des<uint32_t>(obj, ofs);
  sddl = des<std::string>(obj, ofs);
  mtime = des<uint64_t>(obj, ofs);
  btime = des<uint64_t>(obj, ofs);
}


FSDir::FSDir(const BindF1Base<std::vector<uint8_t>,const sha256&> &g, const objseq_t &h)
  : dirsize(0)
  , m_get(g.clone())
{
  // Fetch every object and parse it
  for (objseq_t::const_iterator hashiter = h.begin();
       hashiter != h.end(); ++hashiter)
    try {
      parse((*m_get)(*hashiter));
    } catch (error &e) {
      throw error(hashiter->m_hex + ": " + e.toString());
    }
}

FSDir::FSDir(const std::vector<uint8_t> &obj)
  : dirsize(0)
{
  parse(obj);
}

void FSDir::parse(const std::vector<uint8_t> &obj)
{
  // Parse
  size_t ofs = 0;

  // First, see that it is a version 0 object
  if (des<uint8_t>(obj, ofs))
    throw error("Object is not version 0");

  uint8_t objType= des<uint8_t>(obj, ofs);
  if (objType != 0xdd && objType != 0xde)
    throw error("Object is not a directory");

  // Cumulative size of this object and all its children
  dirsize += des<uint64_t>(obj, ofs);

  // Next, we have a 32-bit integer with the length of our LoR
  const uint32_t lor_len = des<uint32_t>(obj, ofs);

  // Now read LoR
  std::vector<objseq_t> lor;
  for (size_t i = 0; i != lor_len; ++i) {
    lor.push_back(des<objseq_t>(obj, ofs));
  }
  // And read the same number of elements in the LoM
  for (size_t i = 0; i != lor_len; ++i) {
    // Fill out dirent structure
    dirent_t de;
    de.hash = lor[i];
    // Read data type entry
    const uint8_t entrytype = des<uint8_t>(obj, ofs);
    switch (entrytype) {
    case 0x01: { // 0x01 => regular file
      lom_unix_regfile uf(obj, ofs);
      de.type = FSDir::dirent_t::UNIXFILE;
      de.name = uf.name;
      de.user = uf.owner;
      de.group = uf.group;
      de.mode = uf.permissions;
      de.mtime = uf.mtime;
      de.size = uf.filesize;
      break;
    }
    case 0x02: { // 0x02 => directory
      lom_unix_directory ud(obj, ofs);
      de.type = FSDir::dirent_t::UNIXDIR;
      de.name = ud.name;
      de.user = ud.owner;
      de.group = ud.group;
      de.mode = ud.permissions;
      de.mtime = ud.mtime;
      de.size = 0;
      break;
    }
    case 0x11: { // 0x11 => windows file
      lom_windows_regfile wf(obj, ofs);
      de.type = FSDir::dirent_t::WINFILE;
      de.name = wf.name;
      de.user = wf.owner;
      de.group = std::string();
      de.mode = 0600;
      de.mtime = wf.mtime;
      de.size = wf.filesize;
      break;
    }
    case 0x12: { // 0x12 => windows directory
      lom_windows_directory wd(obj, ofs);
      de.type = FSDir::dirent_t::WINDIR;
      de.name = wd.name;
      de.user = wd.owner;
      de.group = std::string();
      de.mode = 0700;
      de.mtime = wd.mtime;
      de.size = 0;
      break;
    }
    default:
      throw error("Unknown meta data type entry");
    }

    // Add to list of entries
    dirents.push_back(de);
  }
}

FSDir::dirent_t::dirent_t()
  : type(UNIXFILE)
  , mode(0)
  , mtime(0)
  , size(0)
{
}

namespace {

  std::string printPerm(uint32_t perm)
  {
    return std::string()
      + (perm & 0400 ? "r" : "-")
      + (perm & 0200 ? "w" : "-")
      + (perm & 0100 ? "x" : "-")
      + (perm & 0040 ? "r" : "-")
      + (perm & 0020 ? "w" : "-")
      + (perm & 0010 ? "x" : "-")
      + (perm & 0004 ? "r" : "-")
      + (perm & 0002 ? "w" : "-")
      + (perm & 0001 ? "x" : "-");
  }

  std::string printSize(uint64_t size)
  {
    size_t suffix = 0; // 0->0, 1->k, 2->M, 3->G, ...
    double s = size;
    while (s >= 1000) {
      suffix++;
      s /= 1024;
    }
    std::ostringstream out;
    out.precision(3);
    out << s;
    switch (suffix) {
    case 0: break;
    case 1: out << "ki"; break;
    case 2: out << "Mi"; break;
    case 3: out << "Gi"; break;
    case 4: out << "Ti"; break;
    case 5: out << "Pi"; break;
    case 6: out << "Ei"; break;
    case 7: out << "Zi"; break;
    case 8: out << "Yi"; break;
    default: out << "?"; break;
    }
    out << "B";
    return std::string(8 - out.str().size(), ' ') + out.str();
  }

  std::string printDate(uint64_t date)
  {
    Time t((time_t(date)));
    std::ostringstream s;
    s << t;
    return s.str();
  }
}



std::string FSDir::dirent_t::toListStr() const
{
  std::ostringstream out;
  switch (type) {
  case UNIXFILE:
  case WINFILE:
    out << "-" << printPerm(mode) << " "
        << std::setw(8) << user << " " << std::setw(8) << group
        << printSize(size) << " "
        << printDate(mtime) << " "
        << name;
    break;
  case UNIXDIR:
  case WINDIR:
    out << "d" << printPerm(mode) << " "
        << std::setw(8) << user << " " << std::setw(8) << group << " "
        << "        "
        << printDate(mtime) << " "
        << name;
  }
  return out.str();
}

