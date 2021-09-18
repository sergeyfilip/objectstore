//
// The XML Lexer implementation
//
// $Id: xmlexer.cc,v 1.2 2013/04/26 18:00:05 joe Exp $
//

#include "xmlio.hh"
#include "common/error.hh"
#include <cstdio>
#include <sstream>

xml::XMLexer::XMLexer(std::istream& input_)
  : input(input_), deepest_backup(0)
{
  if (!input.good())
    throw error("No input data for XML lexer");
}


std::string xml::XMLexer::getCurrentContext() const
{
  std::string con;
  // Now read some characters... If we read too much, an error will be
  // thrown from getRaw - we catch that and ignore it.
  try {
    while (con.size() < 256)
      con += getRaw();
  } catch (error&) {
    // Nothing.
  }
  return con;
}

char xml::XMLexer::getRaw() const
{
  // Read character
  char c;
  input.get(c);
  // Make sure stream is still ok
  if (!input.good())
    throw error("Unexpected end of XML input");
  if (deepest_backup)
    deepest_backup->addData(c);
  return c;
}

char xml::XMLexer::get() const
{
  char c = getRaw();
  if (c ==  '&') {
    // We need to un-escape this. It can either be a direct character
    // reference on the form &#38; or it can be an entity on the form
    // &amp;
    std::string ent;
    for (char nc = getRaw(); nc != ';'; nc = getRaw())
      ent.push_back(nc);
    // Good, we now have everything up until the ';' in ent, and we
    // have consumed the ';' and thrown it away.
    if (ent.size() > 1 && ent[0] == '#') {
      // Good, this is a character reference!
      unsigned codepoint;
      if (ent[1] == 'x') {
        // Hexadecimal
        std::istringstream rd(ent.substr(2));
        rd >> std::hex >> codepoint;
        if (rd.fail())
          throw error("Cannot parse hex codepoint: \"" + ent + "\"");
      } else if (ent[1] >= '0' && ent[1] <= '9') {
        // Decimal
        std::istringstream rd(ent.substr(1));
        rd >> codepoint;
        if (rd.fail())
          throw error("Cannot parse decimal codepoint: \"" + ent + "\"");
      } else {
        // Nothing!
        throw error("Unable to parse character reference: \"" + ent + "\"");
      }
      // Now we need to take this potentially multi-byte character and
      // push everything except from the first byte back onto the
      // input stream. That way subsequent get calls will read the
      // remaining bytes.
      if (codepoint < 0x80) {
        // One byte
        return codepoint;
      } else if (codepoint < 0x800) {
        // Two byte
        // First byte (to return) holds: 110yyyxx
        // Second byte (to push) holds:  10xxxxxx
        const uint8_t second = 0x80 | (codepoint & 0x3F);
        const uint8_t first = 0xC0 | (codepoint >> 6);
        input.putback(second);
        return first;
      } else if (codepoint < 0x10000) {
        // Three byte
        // First byte (to return) holds: 1110yyyy
        // Second byte (to push) holds:  10yyyyxx
        // Third byte (to push) holds:   10xxxxxx
        const uint8_t third = 0x80 | (codepoint & 0x3F);
        const uint8_t second = 0x80 | ((codepoint >> 6) & 0x3F);
        const uint8_t first = 0xE0 | (codepoint >> 12);
        input.putback(third);
        input.putback(second);
        return first;
      } else if (codepoint < 0x110000) {
        // Four byte
        // First byte (to return) holds: 11110zzz
        // Second byte (to push) holds:  10zzyyyy
        // Third byte (to push) holds:   10yyyyxx
        // Fourth byte (to push) holds:  10xxxxxx
        const uint8_t fourth = 0x80 | (codepoint & 0x3F);
        const uint8_t third = 0x80 | ((codepoint >> 6) & 0x3F);
        const uint8_t second = 0x80 | ((codepoint >> 12) & 0x3F);
        const uint8_t first = 0xE0 | (codepoint >> 18);
        input.putback(fourth);
        input.putback(third);
        input.putback(second);
        return first;
      } else {
        // Not a valid unicode codepoint
        throw error("Code point out of range");
      }

    }
    // See if we can match a known reference
    if (ent == "amp")
      return '&';
    if (ent == "lt")
      return '<';
    if (ent == "gt")
      return '>';
    if (ent == "apos")
      return '\'';
    if (ent == "quot")
      return '"';
    throw error("Unknown character reference (" + ent + ")");
  }
  // Ok, no escape needed then.
  return c;
}


bool xml::XMLexer::endOfStream() const
{
  return input.eof();
}


bool xml::XMLexer::matchRaw(const std::string& m) const
{
  Backup myback(*this);
  // Read and compare, terminate on first character mismatch
  for (std::string::const_iterator i = m.begin();
       i != m.end(); ++i) {
    try {
      if (*i != getRaw()) {
        myback.restore();
        return false;
      }
    } catch (error&) {
      // End of string while comparing.
      myback.restore();
      return false;
    }
  }
  // The full string matched...
  return true;
}


bool xml::XMLexer::match(const std::string& m) const
{
  Backup myback(*this);
  // Read and compare, terminate on first character mismatch
  for (std::string::const_iterator i = m.begin();
       i != m.end(); ++i)
    if (endOfStream() || *i != get()) {
      myback.restore();
      return false;
    }
  // The full string matched...
  return true;
}


xml::XMLexer::Backup::Backup(const xml::XMLexer& in)
  : lexer(in)
{
  // Our backup member must be set to the currently deepest backup
  // member of the lexer, as that will be our parent
  prev_backup = in.deepest_backup;
  // Now set the lexer deepest_backup to point to us, as we are now
  // the deepest.
  in.deepest_backup = this;
}

xml::XMLexer::Backup::~Backup()
{
  // Set the deepest backup member on the lexer to our parent, as our
  // parent will be the deepest when we are gone
  lexer.deepest_backup = prev_backup;
}

void xml::XMLexer::Backup::restore()
{
  // Add all data that was consumed while we existed back into the
  // lexer
  while (!data.empty()) {
    lexer.input.putback(*data.rbegin());
    dropData();
  }
}

void xml::XMLexer::Backup::dropData()
{
  data.erase(data.size() - 1);
  if (prev_backup)
    prev_backup->dropData();
}

void xml::XMLexer::Backup::addData(char c)
{
  data.push_back(c);
  if (prev_backup)
    prev_backup->addData(c);
}
