//
/// Configuration loader
//
// $Id: config.cc,v 1.10 2013/10/16 08:49:49 sf Exp $
//

#include "config.hh"

#include "xml/xmlio.hh"
#include "common/partial.hh"
#include "common/trace.hh"

#include <fstream>

namespace {
  trace::Path t_cfg("/config");
}

Config::Config(const std::string &fname)
  : m_workers(2)
  , m_file(fname)
{
  read();
}

Config &Config::read()
{
  return doch(op_r);
}

Config &Config::write()
{
  return doch(op_w);
}

namespace {
  struct ptrcall {
    ptrcall(const BindBase<bool> *p) : ptr(p) { }
    bool operator()() const { return ptr->operator()(); }
    const BindBase<bool> *ptr;
  };
}

Config &Config::doch(Config::op_t op)
{
  // Temporaries
  std::string skiptype;
  std::string skipdir;
  std::set<std::string>::iterator st_i(m_skiptypes.begin());
  std::set<std::string>::iterator sd_i(m_skipdirs.begin());

  // Reading and writing skiptypes require different handling
  const BindBase<bool> &gotType
    = papply<bool,Config,const std::string&>(this, &Config::gotSkipType, skiptype);

  const BindBase<bool> &getType
    = papply<bool,Config,std::set<std::string>::iterator&,std::string&>
    (this, &Config::getSkipType, st_i, skiptype);

  // Reading and writing skipdirs require different handling
  const BindBase<bool> &gotDir
    = papply<bool,Config,const std::string&>(this, &Config::gotSkipDir, skipdir);

  const BindBase<bool> &getDir
    = papply<bool,Config,std::set<std::string>::iterator&,std::string&>
    (this, &Config::getSkipDir, sd_i, skipdir);


  // Define configuration document schema
  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("kservd")
            (Element("cmdsocket")(CharData<std::string>(m_cmdsocket))
             & Element("apihost")(CharData<std::string>(m_apihost))
             & !Element("credentials")
             (Element("token")(CharData<Optional<std::string> >(m_token))
              & Element("password")(CharData<Optional<std::string> >(m_password)))
             & !Element("device")(CharData<Optional<std::string> >(m_device))
	     & !Element("device_id")(CharData<Optional<std::string> >(m_device_id))
             & Element("cache")(CharData<std::string>(m_cachename))
             & !Element("cdp")(CharData<Optional<DiffTime> >(m_cdp))
             & !Element("workers")(CharData<size_t>(m_workers))
             & *Element("skiptype")(CharData<std::string>(skiptype))
             [ ptrcall(op == op_r ? &gotType : &getType) ]
             & *Element("skipdir")(CharData<std::string>(skipdir))
             [ ptrcall(op == op_r ? &gotDir : &getDir) ]));

  switch (op) {
  case op_r: {
    // Load configuration
    std::ifstream file(m_file.c_str());
    if (!file)
      throw error("Cannot read configuration file: " + std::string(m_file));
    XMLexer lexer(file);
    confdoc.process(lexer);
    break;
  }
  case op_w: {
    // Write configuration
    std::ofstream file(m_file.c_str());
    if (!file)
      throw error("Cannot write configuration file: " + std::string(m_file));
    XMLWriter writer(file);
    confdoc.output(writer);
    break;
  }
  }
  return *this;
}

bool Config::gotSkipType(const std::string &s)
{
  m_skiptypes.insert(s);
  return true;
}

bool Config::getSkipType(std::set<std::string>::iterator &i, std::string &s)
{
  if (i == m_skiptypes.end())
    return false;
  s = *i++;
  return true;
}

bool Config::gotSkipDir(const std::string &s)
{
  m_skipdirs.insert(s);
  return true;
}

bool Config::getSkipDir(std::set<std::string>::iterator &i, std::string &s)
{
  if (i == m_skipdirs.end())
    return false;
  s = *i++;
  return true;
}
