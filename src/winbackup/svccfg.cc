///
/// Implementation of our service configuration reader/writer
///
// $Id: svccfg.cc,v 1.6 2013/10/09 14:34:30 sf Exp $

#include "svccfg.hh"
#include "xml/xmlio.hh"

#include "common/trace.hh"
#include "common/error.hh"
#include "common/partial.hh"

#include <fstream>

namespace {
  trace::Path t_cfg("/config");
}

SvcCfg::SvcCfg()
{}

SvcCfg &SvcCfg::setFName(const std::string &fn)
{
  m_fname = fn;
  MTrace(t_cfg, trace::Debug, "Using configuration file in " << fn);
  return *this;
}

SvcCfg &SvcCfg::read()
{
  return doch(op_r);
}

SvcCfg &SvcCfg::write()
{
  return doch(op_w);
}

SvcCfg &SvcCfg::doch(op_t op)
{
  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("winbackup")
            (Element("ngserver")(CharData<std::string>(m_ngserver))
             & !Element("aname")(CharData<std::string>(m_aname))
             & !Element("apass")(CharData<std::string>(m_apass))
             & !Element("devname")(CharData<std::string>(m_devname))
             & !Element("uname")(CharData<std::string>(m_uname))
             & !Element("upass")(CharData<std::string>(m_upass))
             & !Element("user_id")(CharData<std::string>(m_user_id))
             & !Element("device_id")(CharData<std::string>(m_device_id))));

  if (op == op_r) {
    // Load configuration
    std::ifstream file(utf8_to_utf16(m_fname.c_str()));
    if (!file)
      throw error("Cannot read configuration file: " + m_fname);
    XMLexer lexer(file);
    confdoc.process(lexer);
  } else {
    // Write configuration
    std::ofstream file(utf8_to_utf16(m_fname.c_str()));
    if (!file)
      throw error("Cannot write configuration file: " + m_fname);
    XMLWriter writer(file);
    confdoc.output(writer);
  }
  return *this;
}


AppDefs::AppDefs(const std::string &f)
{
  std::string t_lad;

  using namespace xml;
  const IDocument &confdoc
    = mkDoc(Element("appdefs")
            (*Element("ladex")(CharData<std::string>(t_lad))
             [ papply<bool,AppDefs,const std::string&>(this, &AppDefs::gotLadEx, t_lad) ]));
  std::ifstream file(f.c_str());
  if (!file)
    throw error("Cannot read application support file: " + f);
  XMLexer lexer(file);
  confdoc.process(lexer);
}

bool AppDefs::gotLadEx(const std::string &e)
{
  m_lad_ex.push_back(e);
  return true;
}

