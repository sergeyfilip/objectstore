//
//! \file httpd/request.cc
//! Implementation of the request structure methods
//

#include "request.hh"
#include "common/error.hh"
#include "common/string.hh"

#include <sstream>
#include <algorithm>

HTTPRequest::HTTPRequest()
  : m_id(0)
  , m_method(mNONE)
{
}


HTTPRequest::HTTPRequest(uint64_t id,
                         const std::string &data)
  : m_id(id)
{
  //
  // rfc2616 says in Section 5:
  //
  // Request       = Request-Line              ; Section 5.1
  //                 *(( general-header        ; Section 4.5
  //                  | request-header         ; Section 5.3
  //                  | entity-header ) CRLF)  ; Section 7.1
  //
  //
  // rfc2616 says in Section 5.1:
  //
  // Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
  //

  size_t pos = 0; // position of req that we have parsed to

  // Method
  { const std::string s_meth = data.substr(pos, data.find(' ', pos) - pos);
    pos += s_meth.size();
    if (data.size() > pos && data[pos] == ' ')
      pos++;
    else
      throw error("Expected space after method \"" + s_meth + "\"");

    if (s_meth == "GET") m_method = mGET;
    else if (s_meth == "POST") m_method = mPOST;
    else if (s_meth == "PUT") m_method = mPUT;
    else if (s_meth == "DELETE") m_method = mDELETE;
    else if (s_meth == "HEAD") m_method = mHEAD;
    else if (s_meth == "OPTIONS") m_method = mOPTIONS;
    else throw error("Unknown method: \"" + s_meth + "\"");
  }

  // Request-URI
  { m_uri = data.substr(pos, data.find(' ', pos) - pos);
    pos += m_uri.size();
    if (data.size() > pos && data[pos] == ' ')
      pos++;
    else
      throw error("Expected space after URI \"" + m_uri + "\"");
  }

  // Parse options from URI
  parseOptions();

  // HTTP version
  { const std::string s_httpver = data.substr(pos, data.find("\r\n", pos) - pos);
    pos += s_httpver.size();
    if (data.size() > pos && data[pos] == '\r'
        && data.size() + 1 > pos && data[pos+1] == '\n')
      pos += 2;
    else
      throw error("Expected CRLF after HTTP version");

    // We only support HTTP version 1.1
    if (s_httpver != "HTTP/1.1")
      throw error("Unsupported HTTP version: \"" + s_httpver
                  + "\" - only version 1.1 supported");
  }

  //
  // Now parse the headers
  //
  // rfc2616 section 4.2 states:
  // -------------------------------------------
  // message-header = field-name ":" [ field-value ]
  // field-name     = token
  // field-value    = *( field-content | LWS )
  // field-content  = <the OCTETs making up the field-value
  //                   and consisting of either *TEXT or combinations
  //                   of token, separators, and quoted-string>
  // -------------------------------------------
  //
  while (true) {
    // If we are at the end of the buffer, it ended without header
    // termination. That really cannot happen because we would never
    // be called without a full header in the buffer - but let us test
    // anyway (for future uses)
    if (pos == data.size())
      throw error("Request header block not terminated");

    // If this is a CRLF, we have the end of the headers
    if (pos < data.size() && data[pos] == '\r'
        && pos + 1 < data.size() && data[pos + 1] == '\n')
      break;

    // Fetch field-name
    const std::string s_field = data.substr(pos, data.find(':', pos) - pos);
    pos += s_field.size();
    if (data.size() > pos && data[pos] == ':')
      ++pos;
    else
      throw error("No colon in message header");

    std::string s_content;
    while (true) {
      // If we reached the end of the buffer, something is wrong.
      if (pos == data.size())
        throw error("Met end of buffer in request header parse");

      // Skip LWS
      //
      // rfc2616 section 2.2 defines LWS as
      // -------------------------------------------
      //  LWS            = [CRLF] 1*( SP | HT )
      //  SP             = <US-ASCII SP, space (32)>
      //  HT             = <US-ASCII HT, horizontal-tab (9)>
      //  CTL            = <any US-ASCII control character
      //                   (octets 0 - 31) and DEL (127)>
      //  OCTET          = <any 8-bit sequence of data>
      // -------------------------------------------
      if (data.size() > pos && (data[pos] == ' ' || data[pos] == '\t')) {
        pos++;
        continue;
      }
      if (data.size() > pos && data[pos] == '\r'
          && data.size() > pos+1 && data[pos+1] == '\n'
          && data.size() > pos+2 && (data[pos+2] == ' '
                                    || data[pos+2] == '\t')) {
        pos += 3;
        continue;
      }
      //
      // If we meet a CRLF which is NOT an LWS, then it means the
      // current header ended
      //
      if (data.size() > pos && data[pos] == '\r'
          && data.size() > pos+1 && data[pos+1] == '\n') {
        pos += 2;
        break;
      }
      //
      // Now fetch field-content
      //
      size_t fclen = 0;
      while (pos + fclen < data.size()
             && uint8_t(data[pos + fclen]) > 32   // 0-31 for CTL and HT and 32 for SP
             && data[pos + fclen] != 127)
        ++fclen;
      const std::string s_fc = data.substr(pos, fclen);
      pos += fclen;
      // Add it (space separated) to the already parsed content
      if (!s_content.empty())
        s_content.append(" ");
      s_content.append(s_fc);
    }

    // Header parsed
    m_headers.add(s_field, s_content);
  }

  // rfc2616 section 14.23 states:
  // -------------------------------------------
  // A client MUST include a Host header field in all HTTP/1.1 request
  // messages .
  // ...
  // All Internet-based HTTP/1.1 servers MUST respond with a 400 (Bad
  // Request) status code to any HTTP/1.1 request message which lacks
  // a Host header field.
  // -------------------------------------------
  if (!m_headers.hasKey("host"))
    throw error("Request does not contain host header");
}

bool HTTPRequest::consumeComponent(const std::string &c)
{
  // If we match, consume and report success
  if (!m_uri.compare(0, c.size(), c, 0, c.size())) {
    m_uri.erase(0, c.size());
    return true;
  }
  // Nope.
  return false;
}

bool HTTPRequest::matchComponent(const std::string &c) const
{
  return !m_uri.compare(0, c.size(), c, 0, c.size());
}

std::string HTTPRequest::getNextComponent()
{
  // Remove leading slashes...
  while (!m_uri.empty() && *m_uri.begin() == '/')
    m_uri.erase(m_uri.begin());
  // Locate the next '/' or the end of the string
  std::string::iterator token_end = std::find(m_uri.begin(),
                                              m_uri.end(), '/');
  // The next token is from the start of the remainder till the separator
  const std::string next_token(m_uri.begin(), token_end);
  // Remove this token from the remainder
  m_uri.erase(m_uri.begin(), token_end);
  // Decode the URL-encoded string and return it
  return url2str(next_token);
}

bool HTTPRequest::isExitMessage() const
{
  return m_method == mNONE;
}

void HTTPRequest::addBody(const std::string &data)
{
  m_body.append(data);
}

bool HTTPRequest::hasHeader(const std::string &key) const
{
  return m_headers.hasKey(key);
}

std::string HTTPRequest::getHeader(const std::string &key) const
{
  return m_headers.getValue(key);
}

uint64_t HTTPRequest::getId() const
{
  return m_id;
}

const std::string &HTTPRequest::getURI() const
{
  return m_uri;
}

HTTPRequest::method_t HTTPRequest::getMethod() const
{
  return m_method;
}

std::string HTTPRequest::popComponent()
{
  // Find first component limits
  size_t pos = m_uri.find('/');
  if (!pos)
    pos = m_uri.find('/', 1);
  const std::string pop = m_uri.substr(0, pos);
  m_uri.erase(0, pos);
  return pop;
}

std::string HTTPRequest::toString() const
{
  std::ostringstream os;
  switch (getMethod()) {
  case mNONE: os << "{uninitialized} "; break;
  case mGET: os << "GET "; break;
  case mPUT: os << "PUT "; break;
  case mHEAD: os << "HEAD "; break;
  case mDELETE: os << "DELETE "; break;
  case mPOST: os << "POST "; break;
  case mOPTIONS: os << "OPTIONS "; break;
  }
  os << getURI();
  return os.str();
}

void HTTPRequest::parseOptions()
{
  size_t next = std::string::npos;
  const size_t qpos = m_uri.find("?");
  for (size_t iter = qpos;
       iter < m_uri.size();
       iter = next) {
    // Skip the separator - '?' or '&'
    ++iter;
    // Locate next option
    next = m_uri.find("&", iter);
    const std::string opt = m_uri.substr(iter, next - iter);
    // Empty options are ignored
    if (opt.empty())
      continue;
    // Split option as key=value - but note it may have empty value
    // and even only the key with no assignment.
    const size_t assign = opt.find("=");
    if (assign == std::string::npos) {
      // No assignment. Key only.
      m_options.insert(std::make_pair(opt, std::string()));
    } else if (assign == 0) {
      // Assignment is first character...
      throw error("Option starts with assignment: " + opt);
    } else {
      // key=value option.
      m_options.insert(std::make_pair(opt.substr(0, assign),
                                      opt.substr(assign + 1)));
    }
    // Done.
    iter = next;
  }
  // Kill the options from the URI
  if (qpos != std::string::npos)
    m_uri.erase(qpos);
}

bool HTTPRequest::hasOption(const std::string &key) const
{
  return m_options.count(key);
}

std::string HTTPRequest::getOption(const std::string &key) const
{
  std::map<std::string,std::string>::const_iterator i = m_options.find(key);
  if (i == m_options.end())
    throw error("Non-existing option \"" + key + "\" requested");
  return i->second;
}



bool UF::process(HTTPRequest& in) const
{
  HTTPRequest::Backup backup(in);
  if (in.getNextComponent() == match) {
    return true;
  }
  backup.restore();
  return false;
}


bool UD::process(HTTPRequest& in) const
{
  HTTPRequest::Backup backup(in);
  // We succeed if we managed to get a nonempty token...
  dest = in.getNextComponent();
  if (!dest.empty()) {
    return true;
  }
  // Otherwise we fail...
  backup.restore();
  return false;
}

bool UP::process(HTTPRequest& in) const
{
  dest.clear();
  for (std::string next = in.getNextComponent();
       !next.empty(); next = in.getNextComponent())
    dest += "/" + next;
  // We always succeed because we match any path - even the empty one.
  return true;
}

