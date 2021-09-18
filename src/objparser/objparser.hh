///
/// Object parsing utilities
///
//
// $Id: objparser.hh,v 1.3 2013/05/10 07:25:57 joe Exp $
//

#ifndef OBJPARSER_OBJPARSER_HH
#define OBJPARSER_OBJPARSER_HH

#include "common/error.hh"
#include "common/hash.hh"
#include "common/partial.hh"
#include "common/refcountptr.hh"

#include <vector>
#include <list>

typedef std::vector<sha256> objseq_t;

/// De-serialisation. Given a buffer and an offset, decode data and
/// increment offset
template <typename t>
t des(const std::vector<uint8_t> &b, size_t &ofs);
template <typename t>
t des(const std::string &b, size_t &ofs);

template <>
inline uint8_t des<uint8_t>(const std::vector<uint8_t> &b, size_t &ofs)
{
  if (b.size() < ofs + 1)
    throw error("Object ended before uint8_t");
  uint8_t res = b[ofs];
  ofs ++;
  return res;
}
template <>
inline uint8_t des<uint8_t>(const std::string &b, size_t &ofs)
{
  if (b.size() < ofs + 1)
    throw error("Object ended before uint8_t");
  uint8_t res = b[ofs];
  ofs ++;
  return res;
}
template <>
inline uint16_t des<uint16_t>(const std::vector<uint8_t> &b, size_t &ofs)
{
  if (b.size() < ofs + 2)
    throw error("Object ended before uint16_t");
  uint16_t res = (uint16_t(b[ofs]) << 8) | b[ofs + 1];
  ofs += 2;
  return res;
}

template <>
inline uint32_t des<uint32_t>(const std::vector<uint8_t> &b, size_t &ofs)
{
  if (b.size() < ofs + 4)
    throw error("Object ended before uint32_t");
  uint32_t res
    = (uint32_t(b[ofs]) << 24)
    | (uint32_t(b[ofs+1]) << 16)
    | (uint32_t(b[ofs+2]) << 8)
    | b[ofs + 3];
  ofs += 4;
  return res;
}

template <>
inline uint64_t des<uint64_t>(const std::vector<uint8_t> &b, size_t &ofs)
{
  if (b.size() < ofs + 8)
    throw error("Object ended before uint64_t");
  uint64_t res
    = (uint64_t(b[ofs]) << 56)
    | (uint64_t(b[ofs+1]) << 48)
    | (uint64_t(b[ofs+2]) << 40)
    | (uint64_t(b[ofs+3]) << 32)
    | (uint64_t(b[ofs+4]) << 24)
    | (uint64_t(b[ofs+5]) << 16)
    | (uint64_t(b[ofs+6]) << 8)
    | b[ofs + 7];
  ofs += 8;
  return res;
}

template <>
inline objseq_t des<objseq_t>(const std::vector<uint8_t> &b, size_t &ofs)
{
  const uint32_t len = des<uint32_t>(b, ofs);
  if (b.size() < ofs + len * 32)
    throw error("Object ended before objseq_t");
  objseq_t res(len);
  for (size_t i = 0; i != len; ++i) {
    res[i] = sha256::parse(std::vector<uint8_t>(&b[ofs], &b[ofs + 32]));
    ofs += 32;
  }
  return res;
}

template <>
inline std::string des<std::string>(const std::vector<uint8_t> &b, size_t &ofs)
{
  const uint32_t len = des<uint32_t>(b, ofs);
  if (b.size() < ofs + len)
    throw error("Object ended before string");
  std::string res(&b[ofs], &b[ofs + len]);
  ofs += len;
  return res;
}


/// Parse and consume a LoM entry, return the name of the entity
std::string lom_entry_extract_name(const std::vector<uint8_t> &, size_t &);


/// A File System Directory object - both 0x02 (UNIX DIRECTORY) and
/// 0x12 (WINDOWS DIRECTORY) are supported.
class FSDir {
public:
  /// Given an 'object fetch' closure and a hash sequence, load
  /// directory object and parse it.
  FSDir(const BindF1Base<std::vector<uint8_t>,const sha256&> &, const objseq_t &h);

  /// Given a plain object, parse it
  FSDir(const std::vector<uint8_t> &);

  struct dirent_t {
    dirent_t();

    /// Return 'ls' output line string
    std::string toListStr() const;

    std::string name;
    objseq_t hash;

    enum t { UNIXFILE, UNIXDIR, WINFILE, WINDIR } type;
    std::string user;  // UF, UD, WF, WD
    std::string group; // UF, UD,   ,
    uint32_t mode;     // UF, UD,   ,
    time_t mtime;      // UF,   , WF,
    uint64_t size;     // UF,   , WF,
  };
  typedef std::list<dirent_t> dirents_t;

  uint64_t dirsize;
  dirents_t dirents;

private:
  refcount_ptr<BindF1Base<std::vector<uint8_t>,const sha256&> > m_get;
  void parse(const std::vector<uint8_t>&);
};

//! Our max chunk size
const size_t ng_chunk_size = 8 * 1024 * 1024;



#endif
