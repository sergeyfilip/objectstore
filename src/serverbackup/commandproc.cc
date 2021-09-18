///
/// Command line processor for the restore utility
///
//
// $Id: commandproc.cc,v 1.11 2013/08/06 10:59:41 joe Exp $
//

#include "common/error.hh"
#include "common/trace.hh"
#include "common/partial.hh"
#include "common/string.hh"
#include "common/scopeguard.hh"

#include "objparser/objparser.hh"

#include "xml/xmlio.hh"

#include "commandproc.hh"

#include <cctype>
#include <iomanip>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

namespace {
  trace::Path t_cmd("/commandproc");
}

CommProc::CommProc(ServerConnection &conn)
  : m_conn(conn)
{
}

std::vector<uint8_t> CommProc::getObject(const sha256 &hash)
{
  while (true) {
    ServerConnection::Reply rep;
    try {
      ServerConnection::Request req(ServerConnection::mGET,
                                    "/object/" + hash.m_hex);
      req.setBasicAuth(m_conn);
      rep = m_conn.execute(req);
    } catch (error &e) {
      MTrace(t_cmd, trace::Warn, e.toString());
      sleep(5);
      continue;
    }
    if (rep.getCode() == 200) {
      return rep.refBody();
    }
    // 500 errors are retry-able
    if (rep.getCode() >= 500) {
      MTrace(t_cmd, trace::Warn, "Server gave " << rep.getCode()
             << " error: " << rep.toString());
      sleep(5);
      continue;
    }
    // If we get a 404 there is no point in retrying
    if (rep.getCode() == 404)
      throw error("Object not found on server");
    // Other errors should not be retried either
    throw error("Non-retry-able error: " + rep.toString());
  }
}

void CommProc::run()
{
  std::cout << "Type \"help\" for help" << std::endl << std::endl;
  while (true) {
    char cmdline[4096];
    // Build up prompt
    std::cout << std::string(60, '-') << std::endl
              << "{ " << (m_device.empty()
                          ? std::string("no device selected")
                          : m_device ) << " }" << std::endl
              << "{ " << m_snaptime << " }" << std::endl
              << "{ " << (m_path.empty()
                          ? std::string("-")
                          : printPath(m_path)) << " }" << std::endl
              << "> " << std::flush;
    // Read command
    std::cin.getline(cmdline, sizeof cmdline);
    if (!std::cin.good())
      throw error("Input stream no longer good");
    MTrace(t_cmd, trace::Debug, "Got command: " << cmdline);

    std::vector<std::string> tokens;
    // Tokenize
    try {
      enum { IN_WORD, IN_QUOTE, IN_SPACE } ps(IN_SPACE);
      for (char *c = cmdline; *c; ++c) {
        switch (ps) {
        case IN_WORD:
          // If we are in a word and got another letter
          if (*c != '"' && !std::isspace(*c)) {
            tokens.back() += *c;
            continue;
          }
          // If we are in a word and a quote started...
          if (*c == '"') {
            ps = IN_QUOTE;
            continue;
          }
          // Must have gotten a space then
          ps = IN_SPACE;
          continue;
        case IN_QUOTE:
          // If we are in a quote and got another non-quote
          if (*c != '"') {
            tokens.back() += *c;
            continue;
          }
          // Quote ended then
          ps = IN_WORD;
          continue;
        case IN_SPACE:
          // If we got a quote, start processing that
          if (*c == '"') {
            ps = IN_QUOTE;
            continue;
          }
          // If we got a letter, add a new token
          if (!std::isspace(*c)) {
            tokens.push_back(std::string(c, c+1));
            ps = IN_WORD;
            continue;
          }
          // Yet another space then
          continue;
        }
      }

      // Done. Make sure we did not end in a quote
      if (ps == IN_QUOTE)
        throw error("Open quote at end");

    } catch (error &e) {
      MTrace(t_cmd, trace::Warn, "Bad line: " << e.toString());
      continue;
    }

    // Fine, if no command was given, read again
    if (tokens.empty())
      continue;

    try {
      // Now deal with command
      if (tokens[0] == "help")
        cmd_help(tokens);
      else if (tokens[0] == "ls")
        cmd_ls(tokens);
      else if (tokens[0] == "cd")
        cmd_cd(tokens);
      else if (tokens[0] == "get")
        cmd_get(tokens);
      else if (tokens[0] == "rawget")
        cmd_rawget(tokens);
      else if (tokens[0] == "quit")
        return;
      else throw error("Unrecognized command: " + tokens[0]);
    } catch (error &e) {
      MTrace(t_cmd, trace::Warn, "Error: " << e.toString());
      continue;
    }
  }
}

void CommProc::cmd_help(const std::vector<std::string> &t)
{
  if (t.size() == 1) {
    std::cerr << "Commands:" << std::endl
              << "help [command]    : prints this help" << std::endl
              << "ls                : list hosts, snapshots or current" << std::endl
              << "                    directory" << std::endl
              << "cd   {destination}: enter host, snapshot or directory" << std::endl
              << "get  {directory}  : restore named directory" << std::endl
              << "rawget {hash}     : restore hierarchy under given hash" << std::endl
              << std::endl;
    return;
  }
  if (t.size() != 2)
    throw error("help takes zero or one arguments");

  if (t[1] == "cd") {
    std::cerr << "cd {destination}" << std::endl
              << "The \"cd\" command is used for host selection, for" << std::endl
              << "snapshot selection and for directory selection." << std::endl
              << "Use the \"ls\" command to see what you can \"cd\" " << std::endl
              << "into." << std::endl
              << std::endl;
    return;
  }

  if (t[1] == "ls") {
    std::cerr << "ls" << std::endl
              << "The \"ls\" command will list the children of the" << std::endl
              << "current location. It will list available devices" << std::endl
              << "when no device is selected. It will list available" << std::endl
              << "snapshots when no snapshot is selected. It will list" << std::endl
              << "the contents of the currently selected directory when" << std::endl
              << "a snapshot is being browsed." << std::endl << std::endl
              << "Any host, snapshot or directory listed by \"ls\" can" << std::endl
              << "be used as an argument to \"cd\". Any directory" << std::endl
              << "listed by \"ls\" can be used as an argument to \"get\"" << std::endl
              << "for restore." << std::endl
              << std::endl;
    return;
  }

  if (t[1] == "get") {
    std::cerr << "get {directory}" << std::endl
              << "The \"get\" command will restore the named directory" << std::endl
              << "and all of its contents recursively. It will write the" << std::endl
              << "data in the current working directory." << std::endl
              << std::endl;
    return;
  }

  if (t[1] == "rawget") {
    std::cerr << "rawget {hash}" << std::endl
              << "This command is used in very special situations only," << std::endl
              << "such as restoring elements of an incomplete backup," << std::endl
              << "or backups that have been expired from the snapshot" << std::endl
              << "list but not yet expired in the back end storage." << std::endl
              << "The hash of directory objects is not generally known" << std::endl
              << "and therefore the usefulness of this command is limited." << std::endl
              << std::endl;
    return;
  }
  std::cerr << "No help available for \"" << t[1] << "\"" << std::endl
            << std::endl;
}

void CommProc::cmd_ls(const std::vector<std::string>&)
{
  // If no device selected, list devices
  if (m_device.empty())
    return cmd_ls_devices();

  // If no backup set selected, list backup sets
  if (m_snaptime == Time::BEGINNING_OF_TIME)
    return cmd_ls_snapshots();

  // Ok fine, list the directory then
  return cmd_ls_directory();
}

void CommProc::cmd_cd(const std::vector<std::string> &t)
{
  // Require precisely one argument
  if (t.size() != 2)
    throw error("cd requires precisely one argument");

  // If "/" is given, go back to device selection
  if (t[1] == "/") {
    m_path.clear();
    m_snaptime = Time::BEGINNING_OF_TIME;
    m_device.clear();
    return;
  }

  // If ".." is given, go back one level
  if (t[1] == "..") {
    if (!m_path.empty()) {
      m_path.pop_back();
      return;
    }
    if (m_snaptime != Time::BEGINNING_OF_TIME) {
      m_snaptime = Time::BEGINNING_OF_TIME;
      return;
    }
    m_device.clear();
    return;
  }

  // Ok, then the user must have selected something to step into.
  if (m_device.empty()) {
    // Select device
    m_device = t[1];
    return;
  }

  if (m_snaptime == Time::BEGINNING_OF_TIME) {
    std::istringstream istr(t[1]);
    istr >> m_snaptime;

    // Now locate the snapshot with this time stamp and extract the
    // hash. This must be inserted as the first nameless element in
    // our path.
    init_root_path();

    return;
  }

  // Fine, locate child with given name
  FSDir fsd(papply(this, &CommProc::getObject), m_path.back().hash);
  bool found = false;
  for (FSDir::dirents_t::const_iterator i = fsd.dirents.begin();
       i != fsd.dirents.end(); ++i) {
    if (i->name == t[1]) {
      found = true;
      m_path.push_back(*i);
    }
  }
  if (!found)
    throw error("No such element");
}

void CommProc::cmd_get(const std::vector<std::string>& t)
{
  // Check that precisely one argument was given
  if (t.size() != 2)
    throw error("get takes precisely one argument");

  // Check that we have a current directory
  if (m_path.empty())
    throw error("Cannot get until we enter a directory");

  // We want to find the entry given - so load the current directory
  objseq_t restore_hash;
  bool found = false;
  FSDir cdir(papply(this, &CommProc::getObject), m_path.back().hash);
  for (FSDir::dirents_t::const_iterator i = cdir.dirents.begin();
       i != cdir.dirents.end(); ++i) {
    if (i->name == t[1]) {
      if (i->type == FSDir::dirent_t::UNIXDIR
          || i->type == FSDir::dirent_t::WINDIR) {
        restore_hash = i->hash;
        found = true;
      } else {
        throw error("Can only restore a full directory (for now)");
      }
    }
  }
  if (!found)
    throw error("No such child object (file or directory) found");
  // Create restore directory
  if (mkdir(t[1].c_str(), 0700))
    throw syserror("mkdir", "creating restore destination");
  else
    MTrace(t_cmd, trace::Info, "Restoring to ./" << t[1] << "/");
  // Restore then!
  restoreDirectory(restore_hash, t[1]);
}

void CommProc::cmd_rawget(const std::vector<std::string>& t)
{
  // Check that precisely one argument was given
  if (t.size() != 2)
    throw error("rawget takes precisely one argument");

  // Attempt loading the given hash
  objseq_t restore_hash;
  restore_hash.push_back(sha256::parse(t[1]));
  FSDir cdir(papply(this, &CommProc::getObject), restore_hash);

  // Create restore directory
  if (mkdir(t[1].c_str(), 0700))
    throw syserror("mkdir", "creating restore destination");
  else
    MTrace(t_cmd, trace::Info, "Restoring to ./" << t[1] << "/");
  // Restore then!
  restoreDirectory(restore_hash, t[1]);
}


bool CommProc::printLine(const std::string &devname)
{
  std::cout << "-> " << devname << std::endl;
  return true;
}


void CommProc::cmd_ls_devices()
{
  // Fetch list of devices on account
  ServerConnection::Request req(ServerConnection::mGET, "/devices/");
  req.setBasicAuth(m_conn);
  ServerConnection::Reply rep = m_conn.execute(req);
  if (rep.getCode() != 200)
    throw error(rep.toString());

  std::string devname;
  std::string devtype;
  std::string devuri;
  std::string devlogin;
  std::string devpass;

  // Fine, parse response
  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("devices")
            (*Element("pc")
             (Element("name")(CharData<std::string>(devname)))
             [ papply<bool,CommProc,const std::string&>
               (this, &CommProc::printLine, devname) ])
            & (*Element("cloud")
               (Element("name")(CharData<std::string>(devname))
                & Element("uri")(CharData<std::string>(devuri))
                & Element("type")(CharData<std::string>(devtype))
                & Element("login")(CharData<std::string>(devlogin))
                & Element("password")(CharData<std::string>(devpass)))
               [ papply<bool,CommProc,const std::string&>
                 (this, &CommProc::printLine, devname) ]));

  std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
  XMLexer lexer(s);
  ddoc.process(lexer);
}

void CommProc::cmd_ls_snapshots()
{
  // Ask the server for the backup history on this device
  ServerConnection::Request req(ServerConnection::mGET, "/devices/"
                                + str2url(m_device) + "/history");
  req.setBasicAuth(m_conn);
  ServerConnection::Reply rep = m_conn.execute(req);
  if (rep.getCode() != 200)
    throw error(rep.toString());

  // Fine, parse response
  std::string tstamp;
  std::string hash;
  std::string type;
  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("history")
            (*Element("backup")
             (Element("tstamp")(CharData<std::string>(tstamp))
              & Element("type")(CharData<std::string>(type))
              & Element("root")(CharData<std::string>(hash)))
             [ papply<bool,CommProc,const std::string&>
               (this, &CommProc::printLine, tstamp) ]));

  std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
  XMLexer lexer(s);
  ddoc.process(lexer);
}

void CommProc::cmd_ls_directory()
{
  if (m_path.empty())
    throw error("root folder does not exist (no such snapshot?)");

  // Take the top of the stack and download it.
  FSDir dir(papply(this, &CommProc::getObject), m_path.back().hash);

  // Print contents
  for (FSDir::dirents_t::const_iterator i = dir.dirents.begin();
       i != dir.dirents.end(); ++i)
    std::cout << i->toListStr() << std::endl;
}


void CommProc::init_root_path()
{
  // Ask the server for the backup history on this device
  ServerConnection::Request req(ServerConnection::mGET, "/devices/"
                                + str2url(m_device) + "/history");
  req.setBasicAuth(m_conn);
  ServerConnection::Reply rep = m_conn.execute(req);
  if (rep.getCode() != 200)
    throw error(rep.toString());

  // Fine, parse response
  Time tstamp;
  std::string hash;
  std::string type;
  using namespace xml;
  const IDocument &ddoc
    = mkDoc(Element("history")
            (*Element("backup")
             (Element("tstamp")(CharData<Time>(tstamp))
              & Element("type")(CharData<std::string>(type))
              & Element("root")(CharData<std::string>(hash)))
             [ papply<bool,CommProc,const Time&,const std::string&>
               (this, &CommProc::filterSnapHash, tstamp, hash) ]));

  std::istringstream s(std::string(rep.refBody().begin(), rep.refBody().end()));
  XMLexer lexer(s);
  ddoc.process(lexer);

  if (m_path.empty())
    throw error("No such snapshot found");
}

void CommProc::restoreDirectory(const objseq_t &obj, const std::string &ldir)
{
  // Load the directory
  FSDir dir(papply(this, &CommProc::getObject), obj);

  // Restore every entry
  for (FSDir::dirents_t::const_iterator i = dir.dirents.begin();
       i != dir.dirents.end(); ++i) {

    const std::string fname = ldir + "/" + i->name;

    if (i->type == FSDir::dirent_t::UNIXFILE
        || i->type == FSDir::dirent_t::WINFILE) {
      // Create file with proper mode
      int fd = open(fname.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL,
                    i->mode);
      if (fd == -1)
        throw syserror("open", "creating file for restore");
      ON_BLOCK_EXIT(close, fd);
      // Set file ownership
      if (fchown(fd, str2user(i->user), str2group(i->group)))
        MTrace(t_cmd, trace::Warn, "Failed setting owner on " << fname);
      //
      // Restore data
      std::cerr << "Preparing " << fname << "..." << std::flush;
      for (objseq_t::const_iterator c = i->hash.begin(); c != i->hash.end(); ++c) {
        // Load chunk
        std::vector<uint8_t> chunk(getObject(*c));
        size_t ofs = 0;
        // Verify object version
        if (des<uint8_t>(chunk, ofs))
          throw error("File data object is not version 0");
        // Verify that it is file data
        if (des<uint8_t>(chunk, ofs) != 0xfd)
          throw error("Not a file data object");
        // Fine, write data then
        while (ofs != chunk.size()) {
          int wrc = write(fd, &chunk[ofs], chunk.size() - ofs);
          if (wrc == -1 && errno == EINTR)
            continue;
          if (wrc == -1)
            throw syserror("write", "writing chunk data");
          if (wrc == 0)
            throw error("Writing chunk data wrote no data");
          MAssert(wrc > 0, "Non -1 negative write response");
          ofs += wrc;
        }
        size_t oldprec(std::cerr.precision());
        std::cerr << "\rRestoring " << fname << ": "
                  << std::setprecision(3)
                  << (c - i->hash.begin() + 1) * 100.0 / i->hash.size()
                  << std::setprecision(oldprec)
                  << "%" << std::flush;
      }
      std::cerr << " -> done" << std::endl;
      continue;
    }
    if (i->type == FSDir::dirent_t::UNIXDIR
        || i->type == FSDir::dirent_t::WINDIR) {
      // Create directory with proper mode
      if (mkdir(fname.c_str(), i->mode))
        throw syserror("mkdir", "creating directory " + fname + " for restore");

      // Set ownership...
      if (lchown(fname.c_str(), str2user(i->user), str2group(i->group)))
        MTrace(t_cmd, trace::Warn, "Failed setting owner on " << fname);

      // Descend
      restoreDirectory(i->hash, fname);
      continue;
    }
  }
}

bool CommProc::filterSnapHash(const Time &t, const std::string &h)
{
  if (m_snaptime == t) {
    FSDir::dirent_t de;
    de.name = "/";
    de.hash.push_back(sha256::parse(h));
    de.type = FSDir::dirent_t::UNIXDIR;
    m_path.push_back(de);
  }
  return true;
}



uid_t CommProc::str2user(const std::string &usr)
{
  struct passwd *res = getpwnam(usr.c_str());
  if (res)
    return res->pw_uid;
  // Failure... See if usr is a numeric string
  try { return string2Any<uint32_t>(usr); }
  catch (...) { }
  // No, not numeric either.
  MTrace(t_cmd, trace::Warn, "User \"" << usr << "\" not found. Using uid 0");
  return 0;
}

gid_t CommProc::str2group(const std::string &grp)
{
  struct group *res = getgrnam(grp.c_str());
  if (res)
    return res->gr_gid;
  // Failure... See if grp is a numeric string
  try { return string2Any<uint32_t>(grp); }
  catch (...) { }
  // No, not numeric either.
  MTrace(t_cmd, trace::Warn, "Group \"" << grp << "\" not found. Using gid 0");
  return 0;
}


std::string CommProc::printPath(const path_t &p)
{
  std::ostringstream out;
  for (path_t::const_iterator pi = p.begin(); pi != p.end(); ++pi)
    out << (pi == p.begin() ? "" : "/") << pi->name;
  return out.str();
}

