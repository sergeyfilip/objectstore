//
//! \file httpd/headers.hh
//! Declaration of our headers structure
//

#ifndef HTTPD_HEADERS_HH
#define HTTPD_HEADERS_HH

#include <string>
#include <map>
#include <list>

//! \class HTTPHeaders
//
//! Headers list
class HTTPHeaders {
public:
  //! Add a header to the list of headers. If key is already present,
  //! value is appended to existing value
  //
  //! \param key   key of header: will be lower-cased before insertion
  //! \param value value of header. Inserted verbatim.
  HTTPHeaders &add(const std::string &key,
                   const std::string &value);

  //! Search for given key, returns true if found
  //
  //! \param key   key to search for: is lower cased before search
  bool hasKey(const std::string &key) const;

  //! Return value for given key. Throws if not found.
  //
  //! \param key   key to search for: is lower cased before search
  //! \throws error if key not found
  std::string getValue(const std::string &key) const;

  //! Returns a list of values for given key. Throws if not
  //! found. List can still be empty though if header key with no
  //! value is supplied.
  std::list<std::string> getValues(const std::string &key) const;

  typedef std::map<std::string,std::string> m_headers_t;
  //! Gain access directly to the headers (for serialization)
  const m_headers_t &getHeaders() const;

private:
  //! Our header data key/value pairs
  m_headers_t m_headers;
};


#endif
