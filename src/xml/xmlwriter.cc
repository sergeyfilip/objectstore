//
// Implementation of the XML writer
//
// $Id: xmlwriter.cc,v 1.1 2013/01/15 10:14:36 joe Exp $
//

#include "xmlio.hh"
#include "common/error.hh"

xml::XMLWriter::XMLWriter(std::ostream& output_)
  : output(output_)
{
}


void xml::XMLWriter::writeVerbatim(const std::string& data)
{
  // Nothing to do; simply push out the bytes
  output << data;
}



void xml::XMLWriter::writeName(const std::string& name)
{
  // XXX We ought to filter out name characters according to Section
  // 2.3 of the XML 1.0 spec. For now we pass everything through...
  output << name;
}


void xml::XMLWriter::writeCharData(const std::string& chardata)
{
  // Section 2.4 states that '&' and '<' MUST be escaped. The '>' can
  // be left in its literal form unless it participates in the
  // sequence ']]>' in which case it must be escaped - so we always
  // just escape it. We further escape ''' and '"'.
  for (std::string::const_iterator i = chardata.begin();
       i != chardata.end(); ++i) {
    switch (*i) {
    case '<': output << "&lt;";
      break;
    case '&': output << "&amp;";
      break;
    case '>': output << "&gt;";
      break;
    case '\'': output << "&apos;";
      break;
    case '"': output << "&quot;";
      break;
    default: output << *i;
  }
  }
}


std::string xml::XMLWriter::utf8GetFirst(std::string& in)
{
  // Return nothing on nothing
  if (in.empty())
    return std::string();
  // So we need one byte at least
  // If this is an ASCII, it is all there is
  char one = *in.begin();
  size_t consume;
  if ((one & 0x7F) == one) {
    // single-byte
    consume = 1;
  } else if (one >> 5 == 6) {
    // Two-byte sequence (high three bits are 110)
    consume = 2;
  } else if (one >> 4 == 14) {
    // Three-byte sequence (high four bits are 1110)
    consume = 3;
  } else if (one >> 3 == 30) {
    // Four-byte sequence (high five bits are 11110)
    consume = 4;
  } else {
    // Not a valid first byte of a sequence
    throw error("Invalid UTF-8 first-byte");
  }
  // Ensure that we have enough input to consume...
  if (in.size() < consume)
    throw error("UTF-8 string ends mid-character");
  // Consume and return
  const std::string result(in, 0, consume);
  in.erase(0, consume);
  return result;
}

