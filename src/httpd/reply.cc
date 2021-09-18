//
//! \file httpd/reply.cc
//! Implementation of our HTTP reply
//
// $Id: reply.cc,v 1.22 2013/08/29 13:35:58 joe Exp $
//

#include "reply.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"

#include <sstream>
#include <algorithm>


namespace {
  //! Trace path for HTTP Reply parsing
  trace::Path t_rep("/HTTP/reply");
}


HTTPReply::HTTPReply()
  : m_id(0)
  , m_is_final(true)
  , m_status(0)
{
}

HTTPReply::HTTPReply(uint64_t id,
                     bool is_final,
                     uint16_t status,
                     const HTTPHeaders &headers,
                     const std::string &content)
  : m_id(id)
  , m_is_final(is_final)
  , m_status(status)
  , m_headers(headers)
  , m_body(content)
{
}

HTTPReply::HTTPReply(uint64_t id,
                     bool is_final,
                     const std::string &content)
  : m_id(id)
  , m_is_final(is_final)
  , m_status(0)
  , m_body(content)
{
}

uint64_t HTTPReply::getId() const
{
  return m_id;
}

void HTTPReply::setId(uint64_t i)
{
  m_id = i;
}

uint16_t HTTPReply::getStatus() const
{
  return m_status;
}

void HTTPReply::clearStatus()
{
  m_status = 0;
}

void HTTPReply::serialize(std::deque<std::vector<uint8_t> > &q) const
{
  std::ostringstream out;

  //
  // If we have a status, it means we are the first request in a
  // series
  //
  if (m_status) {

    // Serialize status
    out << "HTTP/1.1 " << m_status << " ";
    switch (m_status) {
    case 200: out << "OK"; break;
    case 201: out << "Created"; break;
    case 202: out << "Accepted"; break;
    case 204: out << "No Content"; break;
    case 304: out << "Not modified"; break;
    case 400: out << "Bad Request"; break;
    case 401: out << "Unauthorized"; break;
    case 403: out << "Forbidden"; break;
    case 404: out << "Not Found"; break;
    case 405: out << "Method Not Allowed"; break;
    case 409: out << "Conflict"; break;
    case 412: out << "Precondition Failed"; break;
    case 500: out << "Internal Server Error"; break;
    case 501: out << "Not Implemented"; break;
    case 503: out << "Service Unavailable"; break;
    default: out << "Code" << m_status;
    }
    out << "\r\n";

    // Serialize headers - except for a few that we want to set ourselves
    for (HTTPHeaders::m_headers_t::const_iterator i = m_headers.getHeaders().begin();
         i != m_headers.getHeaders().end(); ++i) {
      if (i->first == "content-length")
        continue;
      if (i->first == "transfer-encoding")
        continue;
      // RFC2616 8.1.2.1 says we "SHOULD" send a Connection: close
      // header if we intend to close the connection. For now we will
      // not do that - we will simply close when we wish to close and
      // that is allowed (SHOULD is not MUST).
      if (i->first == "connection")
        continue;
      out << i->first << ": " << i->second << "\r\n";
    }

    if (m_is_final) {
      // If this is a final reply, we do content-length transfer
      out << "content-length: " << m_body.size() << "\r\n";
    } else {
      // So this is not a final reply. We will do chunked transfer then.
      out << "transfer-encoding: chunked\r\n";
    }

    // End of headers
    out << "\r\n";
  }

  //
  // Now send body...
  //

  // The body may have zero bytes, even if it is not the final
  // chunk. In that case, we simply ignore the chunk (a zero byte
  // chunk ends the transmission).
  if (m_is_final || !m_body.empty()) {
    // If we have a status code, this was the first and last reply for
    // this request, and we therefore have sent a content-length
    // above.
    if (m_is_final && m_status) {
      out << m_body;
    } else {
      // Chunked... Send chunk with header.
      out << std::hex << m_body.size() << "\r\n"
          << m_body << "\r\n";

      // If we are final and we did not just send a zero chunk, a zero
      // chunk.
      if (m_is_final && !m_body.empty())
        out << std::hex << 0 << "\r\n\r\n";
    }
  }

  // Add data.
  //
  // We must not append to the front-most element in the queue because
  // we *may* be in the middle of an SSL_Write that needs retrying -
  // and OpenSSL cannot cope with the buffer changing. So, if we have
  // zero or one entry in the deque, we simply add a new entry. If we
  // have more than one entry we append data if the buffer is less
  // than some reasonable buffer size - say, 128k
  const std::string outbuf(out.str());
  if (q.empty() || q.size() == 1 || q.back().size() + outbuf.size() > 128*1024) {
    q.push_back(std::vector<uint8_t>(outbuf.begin(), outbuf.end()));
  } else {
    q.back().insert(q.back().end(), outbuf.begin(), outbuf.end());
  }
}

bool HTTPReply::isInitial() const
{
  return m_status;
}

bool HTTPReply::isFinal() const
{
  return m_is_final;
}

void HTTPReply::setFinal(bool f)
{
  m_is_final = f;
}

bool HTTPReply::consumeHeaders(std::vector<uint8_t> &d)
{
  // We simply attempt parsing the full request-line and header block
  // - if we fail, return false.
  //
  // First, simply scan for \r\n\r\n - this is the end of the header
  // block.
  const char *srch = "\r\n\r\n";
  std::vector<uint8_t>::iterator i = std::search(d.begin(), d.end(),
                                                 srch, srch + 4);
  // If we did not find this, fail.
  if (i == d.end())
    return false;

  MTrace(t_rep, trace::Debug, "Located end of HTTP reply header block");

  // Good, we have the full header block. Now parse them.
  std::string head(d.begin(), i + 4);
  std::string::const_iterator pos = head.begin();

  MTrace(t_rep, trace::Debug, "Header block: " << head);

  d.erase(d.begin(), i + 4);

  // Read the status line - rfc2616 says:
  //
  // Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
  //
  const std::string s_httpver = consumeUntil(" ", head, pos);
  m_status = string2AnyInt<uint16_t>(consumeUntil(" ", head, pos));
  const std::string s_reason = consumeUntil("\r\n", head, pos);

  if (s_httpver != "HTTP/1.1")
    throw error("Expected HTTP/1.1, got: " + s_httpver);

  MTrace(t_rep, trace::Debug, "Got status-line: code " << m_status
         << " (" << s_reason << ")");

  // Now parse headers
  while (true) {
    try {
      std::string field_name = consumeUntil(":", head, pos);
      std::string field_value = consumeUntil("\r\n", head, pos);

      // We may strip leading and trailing LWS from field_value
      while (!field_value.empty() && field_value[0] == ' ')
        field_value.erase(0, 1);
      while (!field_value.empty() && field_value[field_value.size() - 1] == ' ')
        field_value.erase(field_value.size() - 1, 1);

      m_headers.add(field_name, field_value);
      //      MTrace(t_rep, trace::Debug, "Parsed header '" << field_name
      //             << "' = '" << field_value << "'");

    } catch (error&) {
      // Cannot consume until ":" - possibly because there are no more
      // headers.
      std::string trailer = consumeUntil("\r\n", head, pos);
      if (!trailer.empty())
        throw error("Unexpected trailing data: " + trailer);
      // Ok success -parsed all headers
      return true;
    }
  }
}

bool HTTPReply::consumeBody(std::vector<uint8_t> &d)
{
  // Do we have a content-length header?
  { if (m_headers.hasKey("content-length")) {
      // Fine, simple content-length read
      size_t body_size = string2AnyInt<uint32_t>(m_headers.getValue("content-length"));

      // So, if we have the right number of body bytes, consume them
      if (d.size() >= body_size) {
        MTrace(t_rep, trace::Debug, "Consumed " << body_size
               << " bytes of reply body");

        m_body = std::string(d.begin(), d.begin() + body_size);
        d.erase(d.begin(), d.begin() + body_size);
        return true;
      }
      return false;
    }
  }

  // No content-length header. See if we have a transfer-encoding
  // header.
  { if (m_headers.hasKey("transfer-encoding")) {
      // The only transfer-encoding we support is chunked.
      if (m_headers.getValue("transfer-encoding") != "chunked")
        throw error("Unsupported transfer encoding");

      while (true) {
        // See if we have a chunk size. This is a hex number terminated
        // by CRLF
        const char *crlf = "\r\n";
        std::vector<uint8_t>::iterator csend = std::search(d.begin(), d.end(),
                                                           crlf, crlf + 2);
        if (csend == d.end()) {
          MTrace(t_rep, trace::Debug, "TE read: no chunk size yet");
          return false; // no chunk size yet
        }

        // Parse chunk size - it is a hex number
        const size_t csize = hex2AnyInt<uint32_t>(std::string(d.begin(), csend));

        // See if we have this much data left (remember CRLF after chunk data)
        std::vector<uint8_t>::iterator data_start = csend + 2;
        MAssert(data_start <= d.end(),
                "start of two-byte needle plus two past end of haystack");
        if (csize + 2 > size_t(d.end() - data_start)) {
          MTrace(t_rep, trace::Debug, "TE read: has " << (d.end() - data_start)
                 << " bytes, need " << csize << "+2 for chunk");
          return false; // we do not have the full chunk yet
        }

        // Good, full chunk is here.
        m_body.insert(m_body.end(), data_start, data_start + csize);
        d.erase(d.begin(), data_start + csize + 2);

        MTrace(t_rep, trace::Debug, "Consumed " << csize
               << "+2 bytes of chunk data. "
               << d.size() << " bytes left in buffer");

        // If this is the zero chunk, we're done
        if (!csize) {
          MTrace(t_rep, trace::Debug, "Received zero chunk. End of body.");
          return true;
        }

        // Read again...
      }
    }
  }

  throw error("No content-length and no transfer encoding");
}

std::string HTTPReply::consumeUntil(const std::string &needle,
                                     const std::string &haystack,
                                     std::string::const_iterator &pos)
{
  std::string::const_iterator i = std::search(pos, haystack.end(),
                                              needle.begin(), needle.end());
  if (i == haystack.end())
    throw error("Expected '" + needle + "' in '" + haystack + "'"
                + " after position '" + std::string(pos, haystack.end()) + "'");

  const std::string res = haystack.substr(pos - haystack.begin(), i - pos);
  pos = i;
  pos += needle.size();

  return res;
}


std::string HTTPReply::toString() const
{
  std::ostringstream os;
  os << "Code " << m_status;
  if (m_status >= 400)
    os << std::endl << " " << m_body;
  else
    os << " (" << m_body.size() << "b body)";
  return os.str();
}

const std::string &HTTPReply::refBody() const
{
  return m_body;
}

HTTPHeaders &HTTPReply::refHeaders()
{
  return m_headers;
}
