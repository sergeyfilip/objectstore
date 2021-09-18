///
/// Implementation of base64 encoding and decoding
///
// $Id: base64.cc,v 1.1 2013/01/16 10:31:06 joe Exp $
//

#include "base64.hh"
#include <string.h>

namespace {
  const char base64map[]
  = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
      'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
      'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
      'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
      'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
      'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
      'w', 'x', 'y', 'z', '0', '1', '2', '3',
      '4', '5', '6', '7', '8', '9', '+', '/'};

  char decode_base64_char(char c)
  {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+')
      return 62;
    if (c == '/')
      return 63;
    if (c == '=')
      return -2;
    return -1;
  }

}


void base64::encode(std::string& dst, const std::vector<uint8_t>& src)
{
  unsigned int accu = 0;
  int inc = 0;
  dst.clear();

  for (std::vector<uint8_t>::const_iterator i = src.begin();
       i != src.end(); ++i) {

    accu = (accu << 8) | *i;

    if (++inc == 3) {
      dst += base64map[accu >> 18];
      dst += base64map[(accu >> 12) & 0x3f];
      dst += base64map[(accu >> 6) & 0x3f];
      dst += base64map[accu & 0x3f];
      inc = 0;
      accu = 0;
    }
  }
  if (inc == 1) {
    accu = accu << 4;
    dst += base64map[accu >> 6];
    dst += base64map[accu & 0x3f];
    dst += "==";
  } else if (inc == 2) {
    accu = accu << 2;
    dst += base64map[(accu >> 12) & 0x3f];
    dst += base64map[(accu >> 6) & 0x3f];
    dst += base64map[accu & 0x3f];
    dst += "=";
  }
}

void base64::decode(std::vector<uint8_t>& dst, const std::string& src)
{
  dst.clear();
  std::string::const_iterator i = src.begin();
  char in[4];
  while (i != src.end()) {
    // Try to read 4 chars in order to decode them to 3 bytes.
    memset(in, 0x00, 4);

    int num = 0;
    while (i != src.end() && num != 4) {
      char c = decode_base64_char(*i);
      if (c == -2) {
        // end of content input
        ++i;
        break;
      }
      if (c < 0) {
        // ignore character
        ++i;
        continue;
      }
      in[num] = *i;
      ++i;
      ++num;
    }

    // Okey decode the 4 characters to 3 bytes
    uint8_t c1 = uint8_t(decode_base64_char(in[0]));
    uint8_t c2 = uint8_t(decode_base64_char(in[1]));
    uint8_t c3 = uint8_t(decode_base64_char(in[2]));
    uint8_t c4 = uint8_t(decode_base64_char(in[3]));
    if (num > 1) {
      dst.push_back((c1 << 2) | ((c2 & 0x30) >> 4));
    }
    if (num > 2) {
      dst.push_back(((c2 & 0x3F) << 4) | ((c3 & 0x3C) >> 2));
    }
    if (num > 3) {
      dst.push_back(((c3 & 0x03) << 6) | (c4 & 0x3F));
    }

    if (num != 4) {
      // End of input must have been reached.
      return;
    }
  }
}
