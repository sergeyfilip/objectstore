//
// XML non-template members
//
// $Id: xmlio.cc,v 1.4 2013/09/02 14:04:53 joe Exp $
//

#include "xmlio.hh"
#include "common/error.hh"
#include <stack>

bool xml::SymSpace::process(XMLexer& in) const
{
  bool ok = false;
  while (true) {
    try {
      XMLexer::Backup back(in);
      const uint8_t c = in.getRaw();
      switch (c) {
      case 0x09:
      case 0x0D:
      case 0x0A:
      case 0x20:
        ok = true; // we've matched at least once
        continue;
      default:
        // Ok this didn't match! Restore state and stop reading.
        back.restore();
        break;
      }
      break;
    } catch (error&) {
      // End of stream. Just stop.
      break;
    }
  }

  // If we're here, there's no more spaces to read. Just return
  // whether or not we matched any
  return ok;
}


bool xml::SymComment::process(XMLexer& in) const
{
  XMLexer::Backup iback(in);
  // Clear our destination variable
  dest.clear();
  // Match the start-tag
  if (!in.matchRaw("<!--"))
    return false;
  // Now read while we match
  //  ((Char - '-') | ('-' (Char - '-')))*
  //
  // We can accept dashes in the comment, but we cannot accept two
  // dashes after one another (as that marks the beginning of the end
  // tag).
  //
  enum { S_Regular, S_GotDash } state = S_Regular;
  while (true) {
    if (in.endOfStream())
      throw error("End of stream while parsing comment");
    const uint8_t c = in.get();
    if (state == S_Regular) {
      // If this is a dash, switch state and do not add it yet to the
      // result.
      if (c == '-') {
        state = S_GotDash;
        continue;
      }
      // A non-dash is simply added...
      dest.push_back(c);
      continue;
    }
    // Ok, state is S_GotDash. If the next character is a dash we exit
    // the parsing and expect to match a '>'. If it is not, then it
    // was a single dash which is a perfectly normal part of a
    // comment.
    if (c == '-') {
      break;
    }
    state = S_Regular;
    dest.push_back('-');
    dest.push_back(c);
  }
  // If we're here, we got two dashes. The next character must be a
  // '>'.
  if (!in.matchRaw(">")) {
    iback.restore();
    return false;
  }
  // Success!
  return true;
}


void xml::SymComment::output(XMLWriter& out) const
{
  // XXX we should do something about double dashes...
  out.writeVerbatim(dest);
}


bool xml::SymComment::seqTrymatch(XMLexer& in) const
{
  // If we didn't match before, re-try matching
  if (!seqmatch) {
    seqmatch = process(in);
    // Return progress if we matched
    return seqmatch;
  }
  return false;
}


bool xml::SymComment::seqGetmatch() const
{
  return seqmatch;
}


void xml::SymComment::seqClearmatch() const
{
  seqmatch = false;
}


bool xml::SubDocument::process(XMLexer& in) const
{
  XMLexer::Backup iback(in);

  std::stack<std::string> elmstack;
  while (!in.endOfStream()) {
    // Skip space and comments
    std::string temp;
    while (SymSpace().process(in)
           || SymComment(temp).process(in))
      ;
    // See if we can parse a closing tag
    { XMLexer::Backup eeback(in);
      if (!elmstack.empty()
          && in.matchRaw("</" + elmstack.top())
          && (SymSpace().process(in) || true)
          && in.matchRaw(">")) {
        // Closing tag parsed and matched successfully
        dest += "</" + elmstack.top() + ">";
        elmstack.pop();
        continue;
      }
      // No, restore
      eeback.restore();
    }

    // See if we can parse a start tag or eetag
    { XMLexer::Backup eeback(in);
      std::string tagname;
      if (in.matchRaw("<")) {
      getchar:
        // If there's no more data, fail parsing completely
        if (in.endOfStream()) {
          eeback.restore();
          return false;
        }
        // Skip spacing...
        (void)SymSpace().process(in);
        // Skip until "/>" or ">"
        if (in.matchRaw("/>")) {
          // Tag closed. Don't push.
          dest += "<" + tagname + "/>";
          continue;
        }
        if (in.matchRaw(">")) {
          // Start tag parsed.
          dest += "<" + tagname + ">";
          elmstack.push(tagname);
          continue;
        }
        // Fine, read a character then
        tagname += in.getRaw();
        goto getchar;
      }
    }

    // So this is not a start or end tag...
    try { dest += in.getRaw(); }
    catch (error &) {
      // this means EOF
      break;
    }
  }

  if (elmstack.empty()) {
    // Success! Sub-document is balanced (closed)
    return true;
  }

  // Failure...
  iback.restore();
  return false;
}

void xml::SubDocument::output(XMLWriter& out) const
{
  // Simply output our subdocument data
  out.writeVerbatim(dest);
}

std::string xml::SubDocument::schemaRNC(unsigned) const
{
  return "sub-document";
}

bool xml::SubDocument::seqTrymatch(XMLexer& in) const
{
  throw error("no seqTrymatch for SubDocument");
}

bool xml::SubDocument::seqGetmatch() const
{
  return seqmatch;
}

void xml::SubDocument::seqClearmatch() const
{
  seqmatch = false;
}


bool xml::Element::process(XMLexer& in) const
{
  // Either we have an EETag
  //  or
  // we have an STag and an ETag
  if (processEETag(in))
    return true;
  // Ok we did not have an EETag. See if we have a STag and an ETag
  XMLexer::Backup iback(in);
  if (processSTag(in) && processETag(in))
    return true;
  // We did not. Restore state and report failure
  iback.restore();
  return false;
}


void xml::Element::output(XMLWriter& out) const
{
  // A plain element can only be output as an empty tag
  outputEETag(out);
}


bool xml::Element::processEETag(XMLexer& in) const
{
  XMLexer::Backup iback(in);
  // Remove comments and spacing
  std::string temp;
  while (SymSpace().process(in)
         || SymComment(temp).process(in));
  // See if we match an empty-element tag
  if (in.matchRaw("<" + name)
      && (SymSpace().process(in) || true)
      // XXX We do not support attributes
      && in.matchRaw("/>")) {
    // Also kill cosmetic spacing after the start tag
    while (SymSpace().process(in)
           || SymComment(temp).process(in));
    return true;
  }
  // No match; restore and report failure
  iback.restore();
  return false;
}


bool xml::Element::processSTag(XMLexer& in) const
{
  XMLexer::Backup iback(in);
  // Remove comments and spacing
  std::string temp;
  while (SymSpace().process(in)
         || SymComment(temp).process(in));
  // See if we match a start tag
  if (in.matchRaw("<" + name)
      && (SymSpace().process(in) || true)
      // XXX We do not support attributes
      && in.matchRaw(">")) {
    // Also kill cosmetic spacing after the start tag
    while (SymSpace().process(in)
           || SymComment(temp).process(in));
    return true;
  }
  // No match; restore and report failure
  iback.restore();
  return false;
}


bool xml::Element::processETag(XMLexer& in) const
{
  XMLexer::Backup iback(in);
  // Remove comments and spacing
  std::string temp;
  while (SymSpace().process(in)
         || SymComment(temp).process(in));
  // See if we match an end tag
  if (in.matchRaw("</" + name)
      && (SymSpace().process(in) || true)
      && in.matchRaw(">")) {
    // Kill spacing/comments after the end tag
    while (SymSpace().process(in)
           || SymComment(temp).process(in));
    return true;
  }
  // No match; restore and report failure
  iback.restore();
  return false;
}


void xml::Element::outputSTag(XMLWriter& out) const
{
  out.writeVerbatim("<");
  out.writeName(name);
  out.writeVerbatim(">");
}


void xml::Element::outputETag(XMLWriter& out) const
{
  out.writeVerbatim("</");
  out.writeName(name);
  out.writeVerbatim(">");
}

void xml::Element::outputEETag(XMLWriter& out) const
{
  out.writeVerbatim("<");
  out.writeName(name);
  out.writeVerbatim("/>");
}

std::string xml::Element::schemaRNC(unsigned indent) const
{
  return "element " + name;
}

bool xml::Element::seqTrymatch(XMLexer& in) const
{
  // If we didn't match before, re-try matching
  if (!seqmatch) {
    seqmatch = process(in);
    // Return progress if we matched
    return seqmatch;
  }
  return false;
}

bool xml::Element::seqGetmatch() const
{
  return seqmatch;
}

void xml::Element::seqClearmatch() const
{
  seqmatch = false;
}

