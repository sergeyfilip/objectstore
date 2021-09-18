//
// SHA256 hash routines
//

#ifndef COMMON_HASH_HH
#define COMMON_HASH_HH

#include <stdint.h>
#include <vector>
#include <string>

struct sha256 {
  /// Constructs an "empty" hash
  sha256();

  /// Named constructor. Computes the hash of a block of data
  static sha256 hash(const std::vector<uint8_t> &data);

  /// Named constructor. Computes the hash of a block of data
  static sha256 hash(const std::string &data);

  /// Named constructor. Parses the hash from a 32-byte raw string
  static sha256 parse(const std::vector<uint8_t> &str);

  /// Named constructor. Parses the hash from a 64-character
  /// lower-case hex string
  static sha256 parse(const std::string &str);

  /// Simply compare the raw content of two hashes
  bool operator==(const sha256&) const;

  /// Allow use as key in a set or map
  bool operator<(const sha256&) const;

  /// Returns true if the hash is empty
  bool empty() const;
  /// Clears the hash (makes it "empty")
  void clear();
  /// A 64-character lower-case hex encoded version of the hash
  std::string m_hex;
  /// The 32 raw bytes that make up the hash
  std::vector<uint8_t> m_raw;
};



#endif

