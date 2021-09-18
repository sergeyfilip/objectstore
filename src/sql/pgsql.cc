///
/// Paradoxical driver file
///
// $Id: pgsql.cc,v 1.11 2013/06/21 09:25:44 joe Exp $
//

#include "sql.hh"
#include "common/error.hh"
#include "common/scopeguard.hh"

#include <algorithm>

#include <libpq-fe.h>

trace::Path sql::t_sql("/sql");

sql::Connection::Connection(const std::string &cstr)
  : pgc(0)
  , m_connstr(cstr)
{
  reconnect();
}

sql::Connection::~Connection()
{
  if (pgc)
    PQfinish(pgc);
}

sql::Connection::Connection(const Connection &o)
  : pgc(0)
  , m_connstr(o.m_connstr)
{
}

void sql::Connection::reconnect()
{
  MTrace(t_sql, trace::Debug, "Validating connetion");
  // If we are already connected and the connection is good, don't
  // bother to reconnect
  if (pgc) {
    if (PQstatus(pgc) == CONNECTION_OK)
      return;
    // Connection not ok. Kill it.
    MTrace(t_sql, trace::Debug, "Connection not ok, finishing it up");
    PQfinish(pgc);
  }

  MTrace(t_sql, trace::Debug, "Connecting: " + m_connstr);

  // Attempt connection
  pgc = PQconnectdb(m_connstr.c_str());
  if (PQstatus(pgc) != CONNECTION_OK)
    throw error("Connecting to database failed:" + std::string(PQerrorMessage(pgc)));

  // Internally we always work in UTC. Tell the database.
  exec(*this, "SET TIMEZONE TO \"UTC\"").execute();
  exec(*this, "SET DATESTYLE TO ISO, YMD").execute();
  exec(*this, "SET INTERVALSTYLE TO ISO_8601").execute();
  exec(*this, "SET CLIENT_ENCODING TO \"UTF8\"").execute();
}


sql::exec::exec(sql::Connection &conn, const std::string &s)
  : m_conn(conn)
  , m_stmt(s)
  , m_affected_rows(0)
{
}

sql::exec::~exec()
{
  // Free senders
  while (!m_txers.empty()) {
    delete m_txers.front();
    m_txers.pop_front();
  }

  // Free allocated parameter strings
  for (std::vector<char*>::iterator i = m_parms.begin();
       i != m_parms.end(); ++i)
    delete[] *i;
}


bool sql::exec::execute()
{
  // We need a valid pgc pointer
  m_conn.reconnect();

  // If we have senders, run them
  for (txers_t::const_iterator i = m_txers.begin(); i != m_txers.end(); ++i)
    (**i)();

  MTrace(t_sql, trace::Debug, "Executing \"" + m_stmt + "\"");

  PGresult *pres = PQexecParams(m_conn.pgc,
                                m_stmt.c_str(),
                                m_parms.size(),
                                0,
                                m_parms.empty() ? 0 : &m_parms[0],
                                0,
                                0,
                                0);
  if (!pres)
    throw error("Out of memory during db exec");

  ON_BLOCK_EXIT(PQclear, pres);
  if (PQresultStatus(pres) != PGRES_COMMAND_OK)
    throw error("Exec: \"" + m_stmt + "\" failed: "
                + PQresultErrorMessage(pres));

  { std::string nr = PQcmdTuples(pres);
    if (nr.empty())
      m_affected_rows = 0;
    else
      m_affected_rows = string2Any<size_t>(nr);
  }

  // Fine, we succeeded.
  return true;
}

size_t sql::exec::affectedRows() const
{
  return m_affected_rows;
}

sql::query::query(Connection &conn, const std::string &query)
  : m_conn(conn)
  , m_qstr(query)
  , m_pgres(0)
  , m_crow(0)
  , m_ccol(0)
{
}

sql::query::~query()
{
  // Free receivers
  while (!m_rxers.empty()) {
    delete m_rxers.front();
    m_rxers.pop_front();
  }

  // Free allocated parameter strings
  for (std::vector<char*>::iterator i = m_parms.begin();
       i != m_parms.end(); ++i)
    delete[] *i;

  // Free result structure
  if (m_pgres)
    PQclear(m_pgres);
}

bool sql::query::fetch()
{
  // We need a valid pgc pointer
  m_conn.reconnect();

  MTrace(t_sql, trace::Debug, "Querying \"" + m_qstr + "\"");

  // If the query has not yet been executed, do so
  if (!m_pgres) {
    m_pgres = PQexecParams(m_conn.pgc,
                           m_qstr.c_str(),
                           m_parms.size(),
                           0,
                           m_parms.empty() ? 0 : &m_parms[0],
                           0,
                           0,
                           0);

    if (PQresultStatus(m_pgres) != PGRES_TUPLES_OK)
      throw error("Query: \"" + m_qstr + "\" failed: "
                  + PQresultErrorMessage(m_pgres));

    // Good, we have the initial result.
    m_crow = 0;
    m_ccol = 0;
  } else {
    // Proceed to next row
    ++m_crow;
    m_ccol = 0;
  }

  const bool res = PQntuples(m_pgres) > m_crow;

  // If we have value receivers and we succeeded in fetching a row,
  // run them now.
  if (res && !m_rxers.empty())
    for (rxers_t::const_iterator i = m_rxers.begin(); i != m_rxers.end(); ++i)
      (**i)();

  return res;
}

sql::query &sql::query::fetchone()
{
  if (!fetch())
    throw error("No rows returned on " + m_qstr
                + " where exactly one was expected");
  if (PQntuples(m_pgres) > 1)
    throw error("More than one row returned on " + m_qstr
                + " where exactly one was expected");
  return *this;
}


sql::transaction::transaction(Connection &c)
  : m_conn(c)
  , m_com(false)
{
  sql::exec(m_conn, "BEGIN TRANSACTION").execute();
}

sql::transaction::~transaction()
{
  if (!m_com)
    sql::exec(m_conn, "ABORT TRANSACTION").execute();
}

void sql::transaction::commit()
{
  sql::exec(m_conn, "COMMIT TRANSACTION").execute();
  m_com = true;
}
