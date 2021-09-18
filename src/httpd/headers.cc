//
//! \file httpd/headers.cc
//! Implementation of our headers logic
//
// $Id: headers.cc,v 1.2 2013/08/22 09:33:23 joe Exp $
//

#include "headers.hh"
#include "common/error.hh"

#include <cctype>

namespace {

  std::string lowercase(const std::string &lowercase)
  {
    std::string ret;
    for (size_t i = 0; i != lowercase.size(); ++i)
      ret += char(isupper(lowercase[i])
                  ? tolower(lowercase[i])
                  : lowercase[i]);
    return ret;
  }

}

HTTPHeaders &HTTPHeaders::add(const std::string &key,
                              const std::string &value)
{
  // Append data...
  m_headers[lowercase(key)] += value;
  return *this;
}

bool HTTPHeaders::hasKey(const std::string &key) const
{
  return m_headers.count(lowercase(key));
}

std::string HTTPHeaders::getValue(const std::string &key) const
{
  m_headers_t::const_iterator i = m_headers.find(lowercase(key));
  if (i == m_headers.end())
    throw error("No header \"" + key + "\"");
  return i->second;
}

std::list<std::string> HTTPHeaders::getValues(const std::string &key) const
{
  std::list<std::string> ret;
  std::string v = getValue(key);
  // Now strip off spaces and tokenize on comma
  while (!v.empty()) {
    // Skip leading space
    while (!v.empty() && (isspace(v[0]) || v[0] == ',')) v.erase(0, 1);
    // Consume until comma or end of line
    std::string value = v.substr(0, v.find(','));
    v.erase(0, value.size());
    if (!value.empty())
      ret.push_back(value);
  }
  return ret;
}



const HTTPHeaders::m_headers_t &HTTPHeaders::getHeaders() const
{
  return m_headers;
}
