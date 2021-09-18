///
/// VisitWeb
///
// $Id: visitweb.hh,v 1.0 2013/07/18 13:36:21 sf Exp $
///

#ifndef WINBACKUP_VISITWEB_HH
#define WINBACKUP_VISITWEB_HH

#include <QLabel>
#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QGroupBox>
#include <QObject>

#include "svccfg.hh"

/// We have a sign-in window for the sign-in process
class VisitWeb: public QObject {
  Q_OBJECT

public:
  VisitWeb(SvcCfg &cfg);

public slots:
  void doVisit();

signals:
  void visited();

private:
  SvcCfg &m_cfg;

};
#endif
