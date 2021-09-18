//
// URLencoding
//
// $Id: string.cc,v 1.9 2013/06/18 09:14:58 joe Exp $
//

#include "string.hh"
#include "error.hh"

#include <stdint.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <locale>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
# include <windows.h>
# include <wincrypt.h>
#endif


std::string str2lower(const std::string &s)
{
  std::locale loc;
  std::string ret;
  for (std::string::const_iterator i = s.begin(); i != s.end(); ++i)
    ret.push_back(std::isupper(*i,loc) ? std::tolower(*i,loc) : *i);
  return ret;
}

void validateUTF8(const std::string &s)
{
  // Validate UTF-8
  size_t ofs = 0;
  while (ofs != s.size()) {
    // Single bytes have pattern 0xxxxxxx
    if (!(0x80 & uint8_t(s[ofs]))) {
      ofs++;
      continue;
    }
    // Double bytes have pattern 110xxxxx 10xxxxxx
    if ((uint8_t(s[ofs]) >> 5) == 6
        && ofs+1 != s.size() && (uint8_t(s[ofs+1]) >> 6) == 2) {
      ofs += 2;
      continue;
    }
    // Triple bytes have pattern 1110xxxx 10xxxxxx 10xxxxxx
    if ((uint8_t(s[ofs]) >> 4) == 14
        && ofs+2 < s.size() && (uint8_t(s[ofs+1]) >> 6) == 2
        && (uint8_t(s[ofs+2]) >> 6) == 2) {
      ofs += 3;
      continue;
    }
    // Quad bytes have pattern 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((uint8_t(s[ofs]) >> 3) == 30
        && ofs+3 < s.size()
        && (uint8_t(s[ofs+1]) >> 6) == 2 && (uint8_t(s[ofs+2]) >> 6) == 2
        && (uint8_t(s[ofs+3]) >> 6) == 2) {
      ofs += 4;
      continue;
    }
    // Five bytes have pattern 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((uint8_t(s[ofs]) >> 2) == 62
        && ofs+4 < s.size()
        && (uint8_t(s[ofs+1]) >> 6) == 2 && (uint8_t(s[ofs+2]) >> 6) == 2
        && (uint8_t(s[ofs+3]) >> 6) == 2 && (uint8_t(s[ofs+4]) >> 6) == 2) {
      ofs += 5;
      continue;
    }
    // Six bytes have pattern 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((uint8_t(s[ofs]) >> 1) == 126
        && ofs+5 < s.size()
        && (uint8_t(s[ofs+1]) >> 6) == 2 && (uint8_t(s[ofs+2]) >> 6) == 2
        && (uint8_t(s[ofs+3]) >> 6) == 2 && (uint8_t(s[ofs+4]) >> 6) == 2
        && (uint8_t(s[ofs+5]) >> 6) == 2) {
      ofs += 6;
      continue;
    }
    // Error!
    throw error("Encountered non UTF-8 character in string");
  }
}



#if defined(_WIN32)
// Convert from UTF-16 to UTF-8
std::string utf16_to_utf8(const std::wstring &in_str)
{
  // First figure out the size of the output buffer by giving 0 as the
  // size and out buffer parameter.
  // The -1 argument indicates that the string is 0-terminated.
  int size = WideCharToMultiByte(CP_UTF8, 0, in_str.c_str(), -1, 0, 0, 0, 0);
  if (!size)
    throw error("Error converting string from UTF16 to UTF8");
  // Ok, we have the size of the output buffer. Let's do the conversion.
  std::vector<char> out_buf(size);
  if (!WideCharToMultiByte(CP_UTF8, 0, in_str.c_str(), -1, &out_buf[0], size, 0, 0))
    throw error("Error converting from UTF16");
  return std::string(out_buf.begin(),
		     std::find(out_buf.begin(), out_buf.end(), 0));
}
#endif

#if defined(_WIN32)
// Convert from UTF-8 to UTF-16
std::wstring utf8_to_utf16(const std::string &in_str)
{
  // Call MultiBytetoWideChar with a 0 size buffer to get the size of
  // UTF-16 string, and set the flag to MB_ERR_INVALID_CHARS to fail
  // if the string contains non UTF-8 characters.
  // The -1 argument indicates that the string is 0-terminated.
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 in_str.c_str(), -1, 0, 0);
  if (!size) { // The conversion failed
    if (GetLastError() != ERROR_NO_UNICODE_TRANSLATION)
      throw error("No UTF8-to-UTF16 translation");
    // So, the string wasn't in UTF-8, let's convert it using the
    // current ANSI codepage.
    size = MultiByteToWideChar(CP_ACP, 0, in_str.c_str(), -1, 0, 0);
    if (!size)
      throw error("Error converting from ANSI to UTF16");
    std::vector<wchar_t> out_buf(size);
    if (!MultiByteToWideChar(CP_ACP, 0, in_str.c_str(), -1, &out_buf[0], size))
      throw error("Error converting from ANSI");
    return std::wstring(out_buf.begin(), std::find(out_buf.begin(), out_buf.end(), 0));
  }
  // Ok, this string is UTF-8 and we can convert it, let's do it.
  std::vector<wchar_t> out_buf(size);
  if (!MultiByteToWideChar(CP_UTF8, 0, in_str.c_str(), -1, &out_buf[0], size))
    throw error("Error converting from UTF-8");
  return std::wstring(out_buf.begin(), std::find(out_buf.begin(), out_buf.end(), 0));
}
#endif


namespace {

  unsigned char2hex(char c)
  {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    throw error("Non hex character");
  }

  std::string char2enc(char c)
  {
    //
    // Convert a single character into a '%xx' string
    //
    std::ostringstream s;
    s << "%" << std::setw(2) << std::setfill('0') << std::hex << unsigned(uint8_t(c));
    return s.str();
  }

}

std::string url2str(const std::string &src)
{
  std::string dst;
  //
  // Any '%' character must be followed by two (case insensitive)
  // hexadecimal digits. This is translated into ISO-Latin character
  // number as given by the two hex digits.
  //
  // A '%' character that is *not* followed by precisely two hex
  // digits is invalid - but we choose to ignore that here, and simply
  // 'decode' it into a '%' character.
  //
  for (std::string::const_iterator si = src.begin();
       si != src.end(); ) {
    // Non percent non plus; no parsing
    if (*si != '%' && *si != '+') {
      dst += *si++;
      continue;
    }
    // If plus, add space
    if (*si == '+') {
      dst += " ";
      ++si;
      continue;
    }
    // Percent; parsing!
    std::string::const_iterator prep = ++si;
    try {
      // Parse hex digits
      if (si == src.end())
	throw error("premature end of string 1");
      unsigned d = char2hex(*si++) * 0x10;
      if (si == src.end())
	throw error("premature end of string 2");
      d += char2hex(*si++);
      // Add new char
      dst += char(d);
    } catch (error &) {
      dst += '%';
      si = prep; // go back to position right after '%'
    }
  }
  // Done
  return dst;
}

std::string str2url(const std::string &src)
{
  //
  // We must encode:
  //
  //  ASCII control characters; 0x00 - 0x1F, 0x7F
  //
  //  Non-ASCII: 0x80-0xFF
  //
  //  "Reserved" characters: 0x24 Dollar ("$")
  //                         0x26 Ampersand ("&")
  //                         0x2B Plus ("+")
  //                         0x2C Comma (",")
  //                         0x2F Forward slash ("/")
  //                         0x3A Colon (":")
  //                         0x3B Semi-colon (";")
  //                         0x3D Equals ("=")
  //                         0x3F Question mark ("?")
  //                         0x40 'At' symbol ("@")
  //
  // "Unsafe" characters: 0x20 Space
  //                      0x22 Quotation marks
  //                      0x3C 'Less Than' symbol ("<")
  //                      0x3E 'Greater Than' symbol (">")
  //                      0x23 'Pound' character ("#")
  //                      0x25 Percent character ("%")
  //                      0x7B Left Curly Brace ("{") 
  //                      0x7D Right Curly Brace ("}")
  //                      0x7C Vertical Bar/Pipe ("|")
  //                      0x5C Backslash ("\")
  //                      0x5E Caret ("^")
  //                      0x7E Tilde ("~")
  //                      0x5B Left Square Bracket ("[")
  //                      0x5D Right Square Bracket ("]")
  //                      0x60 Grave Accent ("`")
  //
  std::string dst;
  for (std::string::const_iterator si = src.begin();
       si != src.end(); ++si) {
    //
    // We do unsigned comparisons...
    //
    uint8_t c = *si;
    //
    // Encode control characters:
    //
    if (c <= 0x1F || c == 0x7F) {
      dst += char2enc(*si);
      continue;
    }
    //
    // Encode non-ASCII characters:
    //
    if (c >= 0x80) {
      dst += char2enc(*si);
      continue;
    }
    //
    // Decide if the character is special in other ways
    //
    switch (c) {
      // Reserved characters
    case 0x24: // dollar
    case 0x26: // ampersand
    case 0x2B: // plus
    case 0x2C: // comma
    case 0x2F: // slash
    case 0x3A: // colon
    case 0x3B: // semicolon
    case 0x3D: // equals
    case 0x3F: // question mark
    case 0x40: // 'at' symbol
      // Unsafe characters
    case 0x20: // space
    case 0x22: // quotation marks
    case 0x3C: // less than
    case 0x3E: // greater than
    case 0x23: // pund
    case 0x25: // percent
    case 0x7B: // left curly brace
    case 0x7D: // right curly brace
    case 0x7C: // pipe
    case 0x5C: // backslash
    case 0x5E: // caret
    case 0x7E: // tilde
    case 0x5B: // left square bracket
    case 0x5D: // right square bracket
    case 0x60: // accent grave
      dst += char2enc(*si);
      continue;
    default:
      // Character need no conversion
      dst += *si;
    }
  }
  // Done!
  return dst;
}


#if defined(_WIN32)
namespace {
  struct csp_t {
    HCRYPTPROV provider;
    csp_t() {
      if (!CryptAcquireContext(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	throw syserror("CryptAcquireContext", "initializing random string context");
    }
  };
}
#endif

std::string randStr(size_t len)
{
  std::string out;
#if defined(__unix__) || defined(__APPLE__)
  std::ifstream f("/dev/urandom");
  for (size_t i = 0; i != len; ++i)
    out += char((f.get() % 25) + 'a');
  if (!f.good())
    throw error("Cannot read random string from /dev/urandom");
#endif
#if defined(_WIN32)
  std::vector<BYTE> buffer(len);
  static csp_t csp;
  if (!CryptGenRandom(csp.provider, uint32_t(len), &buffer[0]))
    throw error("Cannot get random data");
  for (std::vector<BYTE>::const_iterator i = buffer.begin(); i != buffer.end(); ++i)
    out += char((*i % 25) + 'a');
#endif
  return out;
}
