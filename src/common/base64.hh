///
/// Base64 encoding and decoding
///
// $Id: base64.hh,v 1.1 2013/01/16 10:31:06 joe Exp $
//

#ifndef COMMON_BASE64_HH
#define COMMON_BASE64_HH

#include <string>
#include <vector>
#include <stdint.h>

namespace base64 {

  /// Given a string of base64 encoded data, decode this data and
  /// append it to the given vector
  void decode(std::vector<uint8_t>&, const std::string&);

  /// Given a vector of data, append the base64 encoded representation
  /// of those data to the given string
  void encode(std::string&, const std::vector<uint8_t>&);

}


#endif
