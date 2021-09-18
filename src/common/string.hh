///-*-C++-*-
///
/// String utility routines
///

#ifndef COMMON_STRING_HH
#define COMMON_STRING_HH

#include <string>

//! For converting a URL-encoded string to a plain string
std::string url2str(const std::string &src);

//! For converting a plain string into an URL-encoded string
std::string str2url(const std::string &src);

//! Lower-case a string
std::string str2lower(const std::string &s);

//! Validate that string is valid UTF-8 - throw on error
void validateUTF8(const std::string &s);

//! Random string (characters a-z) of given length
std::string randStr(size_t len);

#if defined(_WIN32)
//! Convert from UTF-16 to UTF-8
std::string utf16_to_utf8(const std::wstring&);
//! Convert from UTF-8 to UTF-16
std::wstring utf8_to_utf16(const std::string&);
#endif

# include "string.hcc"
#endif
