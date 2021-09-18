///
/// File and directory streaming download implementations
///
//
///
/// $Id: downloads.cc,v 1.3 2013/09/12 12:54:31 joe Exp $
///


#include "main.hh"

void MyWorker::generateFileDownloadReply(const HTTPRequest &req,
                                         const std::string &filename,
                                         const objseq_t &filedata,
                                         bool data_constant_uri)
{
  // Treat cache headers if this is a data_constant_uri
  if (data_constant_uri && cacheBypassRequest(req))
    return;

  // We simply start a chunked transfer - first we send the header
  // with the content disposition for the file name, and then the
  // transfer starts.
  //
  // We do not send content-disposition if the "nocd" option was
  // passed on the request.
  //
  { HTTPHeaders head;
    head.add("content-type", mimeTypeFromExt(filename));
    if (!req.hasOption("nocd")) {
      // We have an arbitrarily long UTF8 encoded file name.
      //
      // As per RFC2231 we will
      // 1: set encoding to UTF8
      // 2: set language to US English
      // 3: URL-encode the file name
      std::string val("attachment; filename*=utf8'en-us'" + str2url(filename));
      head.add("content-disposition", val);
    }

    // Build up reply
    HTTPReply rep(req.getId(), false, 200, head, std::string());

    // Add cache headers if data content is constant for URI
    if (data_constant_uri)
      cacheTagReply(req, rep);

    // Send reply
    m_httpd.postReply(rep);
  }

  //
  // For diagnostics... Pay attention to range headers!
  //
  if (req.hasHeader("range")) {
    MTrace(t_api, trace::Warn, "Range header ignored: "
           << req.toString());
  }

  //
  // Now start transferring data. We keep an eye on the HTTP server
  // queue - as long as it has one unserialized request in the
  // queue, we do not bother retrieving any more data. That way we
  // limit our memory consumption while the client is downloading.
  //
  for (objseq_t::const_iterator obj = filedata.begin(); obj != filedata.end(); ++obj) {
    // Fetch the next chunk
    std::vector<uint8_t> data(fetchObject(*obj));

    // Wait until our outbound queue is empty to prevent us from
    // filling up our own memory by fetching data from the object
    // store too quickly
    while (m_httpd.outqueued(req.getId())) {
      MTrace(t_api, trace::Info, "Data queued for request "
             << req.getId() << " - awaiting client...");
      sleep(1);
    }

    // The chunk we fetched should be a version zero file data object
    size_t ofs = 0;
    if (0 != des<uint8_t>(data, ofs))
      throw error("Referenced object not a version zero object");
    if (0xfd != des<uint8_t>(data, ofs))
      throw error("Referenced object not a file data object");

    // Fine, now post response
    m_httpd.postReply(HTTPReply(req.getId(), false,
                                std::string(data.begin() + ofs, data.end())));
  }

  // End!
  m_httpd.postReply(HTTPReply(req.getId(), true, std::string()));
}

std::vector<uint8_t> MyWorker::fetchObject(const sha256 &hash)
{
  size_t retries = 3;

  while (retries) {

    HTTPRequest fwd;
    fwd.m_method = HTTPRequest::mGET;
    fwd.m_uri = "/object/" + hash.m_hex;
    MTrace(t_api, trace::Info, "Fetching chunk " << hash.m_hex);

    HTTPReply rep = m_osapi.execute(fwd);

    // Retry on 500 errors too
    if (rep.getStatus() >= 500) {
      MTrace(t_api, trace::Info, "Retrying fetch due to 500 error: "
             << rep.toString());
      sleep(1);
      retries--;
      continue;
    }

    // On error, abort what we are doing...
    if (rep.getStatus() != 200)
      throw error("Got error from storage: " + rep.toString());

    // Fine, we got a 200 - return data
    return std::vector<uint8_t>(rep.refBody().begin(), rep.refBody().end());
  }

  // No more retries
  throw error("No more retries - giving up on fetch of "
              + hash.m_hex);
}

