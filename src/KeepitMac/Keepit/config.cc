//
/// Configuration loader
//
// $Id: config.cc,v 1.4 2013/03/20 15:14:22 joe Exp $
//

#include "config.hh"

#include "xml/xmlio.hh"

#include <fstream>

Config::Config(const std::string &fname)
  : m_file(fname)
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


Config &Config::doch(Config::op_t op)
{
  // Define configuration document schema
  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("kservd")
            (Element("cmdsocket")(CharData<std::string>(m_cmdsocket))
             & Element("apihost")(CharData<std::string>(m_apihost))
             & !Element("credentials")
             (Element("email")(CharData<Optional<std::string> >(m_email))
              & Element("token")(CharData<Optional<std::string> >(m_token))
              & Element("password")(CharData<Optional<std::string> >(m_password)))
             & !Element("device")(CharData<Optional<std::string> >(m_device))
             & Element("cache")(CharData<std::string>(m_cachename))
             & !Element("cdp")(CharData<Optional<DiffTime> >(m_cdp))));

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
