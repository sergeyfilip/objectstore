//
// Implementation of the SHA256 logic
//

#include "hash.hh"
#include "error.hh"

#if defined(__unix__) || defined(_WIN32)
# include <openssl/sha.h>
#endif

#if defined(__APPLE__)
# define COMMON_DIGEST_FOR_OPENSSL
# include <CommonCrypto/CommonDigest.h>
#endif

#include <sstream>
#include <iomanip>

sha256::sha256()
{
}

sha256 sha256::hash(const std::vector<uint8_t> &data)
{
  std::vector<uint8_t> raw(SHA256_DIGEST_LENGTH);

  // Compute raw hash
  uint8_t dummy;
  SHA256_CTX sha;
  SHA256_Init(&sha);
  SHA256_Update(&sha, data.empty() ? &dummy : &data[0], data.size());
  SHA256_Final(&raw[0], &sha);

  return parse(raw);
}

sha256 sha256::hash(const std::string &data)
{
  return hash(std::vector<uint8_t>(data.begin(), data.end()));
}

sha256 sha256::parse(const std::vector<uint8_t> &dat)
{
  if (dat.size() != SHA256_DIGEST_LENGTH)
    throw error("Raw sha256 digest has bad length");

  sha256 hash;
  hash.m_raw = dat;

  // Hex encode
  std::ostringstream myhash;
  for (size_t i = 0; i != dat.size(); ++i)
    myhash << std::hex << std::setw(2) << std::setfill('0') << uint32_t(dat[i]);

  hash.m_hex = myhash.str();
  return hash;
}

sha256 sha256::parse(const std::string &str)
{
  sha256 hash;
  hash.m_hex = str;

  // Validate size
  if (str.size() != 64)
    throw error("Hex string has bad length for sha256");

  // Parse hex
  for (size_t i = 0; i != 64; i += 2) {
    std::istringstream s(str.substr(i, 2));
    uint32_t val;
    s >> std::hex >> val;
    MAssert(!(val >> 8), "Hex value decoding error - byte too big");
    if (s.fail())
      throw error("Hex value parse error - cannot parse byte");
    if (!s.eof())
      throw error("Trailing bytes after hex byte");
    hash.m_raw.push_back(uint8_t(val));
  }

  return hash;
}

bool sha256::empty() const
{
  return m_hex.empty();
}

void sha256::clear()
{
  m_hex.clear();
  m_raw.clear();
}

bool sha256::operator==(const sha256 &o) const
{
  return m_raw == o.m_raw;
}

bool sha256::operator<(const sha256 &o) const
{
  return m_raw < o.m_raw;
}
