///
/// SQL database interface
///
// $Id: sql.hh,v 1.12 2013/06/21 09:25:44 joe Exp $
//

#ifndef SRC_SQL_SQL_HH
#define SRC_SQL_SQL_HH

#include "common/error.hh"
#include "common/string.hh"
#include "common/partial.hh"
#include "common/trace.hh"
#include "common/optional.hh"

#include <string>
#include <string.h>
#include <vector>
#include <list>

#include <libpq-fe.h>

namespace sql {

  //! Trace path for SQL operations
  extern trace::Path t_sql;

  class query;
  class exec;

  /// SQL Connection
  class Connection {
  public:
    /// Create a connection from a connection string - throws on error
    Connection(const std::string &connstr);

    /// Cleanup
    ~Connection();

    /// This is a low-cost operation that is called by the query and
    /// exec classes when they are about to execute a query. It
    /// ensures that a valid pgc pointer exists in the Connection
    /// object (and throws if this cannot be accomplished)
    void reconnect();

    /// Copy construction - we copy the connection string but not the
    /// connection.
    Connection(const Connection &);

  private:
    /// Protect from assignment
    Connection &operator=(const Connection&);

    /// psql connection object - it is accessed directly by the query
    /// and exec classes.
    PGconn *pgc;
    friend class query;
    friend class exec;

    /// Our connection string
    const std::string m_connstr;
  };

  /// An SQL query
  class query {
  public:
    /// Set up a query on a given connection, given a query text
    query(Connection &, const std::string &);

    /// Cleanup
    ~query();

    /// Add a variable - possibly null
    template <typename V>
    query &add(const Optional<V>& v);

    /// Add a variable (referenced by $1, $2, ... in the query text)
    template <typename V>
    query &add(const V& v);

    /// Fetch a result row. Returns false if no more results are
    /// available.
    bool fetch();

    /// Fetch one result row. Throws if zero or more than one rows are
    /// fetched.
    query &fetchone();

    /// Retrieve possibly null row result
    template <typename V>
    query &get(Optional<V> &);

    /// Retrieve row result
    template <typename V>
    query &get(V &);

    /// Set up a receiver for a result - instead of using get(.) after
    /// fetch(), it is possible to use receiver(.) *before*
    /// fetch(). This will set up the given container as a receptacle
    /// for the value on fetch().
    template <typename V>
    query &receiver(V &);

  private:
    /// Our connection
    Connection &m_conn;

    /// The query string
    const std::string m_qstr;

    /// The parameters
    std::vector<char*> m_parms;

    /// Our result data
    PGresult *m_pgres;

    /// Current result row
    int m_crow;

    /// Current result column
    int m_ccol;

    /// If the user set up recipient variables, we may have a list of
    /// those.
    typedef std::list<BindBase<query&>*> rxers_t;
    rxers_t m_rxers;
  };

  /// An SQL statement
  class exec {
  public:
    /// Set up statement for execution
    exec(Connection &, const std::string &);

    /// Cleanup
    ~exec();

    /// Add a variable - possibly null
    template <typename V>
    exec &add(const Optional<V>& v);

    /// Add a variable (referenced by $1, $2, ... in the statement text)
    template <typename V>
    exec &add(const V& v);

    /// Set up a sender of a result - instead of using add(.) before
    /// execute(), it is possible to use sender(.) to set up a
    /// variable for add(.) during the execute() call.
    template <typename V>
    exec &sender(const V &);

    /// Execute statement. Throws on error. If it returns, it always
    /// returns true (to make it easily integrable with the XML
    /// parsing framework)
    bool execute();

    /// This method can be called after execute() and will return how
    /// many rows were affected by the operation just performed.
    size_t affectedRows() const;

  private:
    /// Our connection
    Connection &m_conn;

    /// The statement string
    const std::string m_stmt;

    /// The parameters
    std::vector<char*> m_parms;

    /// If the user set up sender variables, we may have a list of
    /// those.
    typedef std::list<BindBase<exec&>*> txers_t;
    txers_t m_txers;

    /// Number of rows affected by last operation
    size_t m_affected_rows;
  };

  /// A transaction. Aborts on destruction if not committed.
  class transaction {
  public:
    /// Open transaction
    transaction(Connection &);
    /// Abort transaction if it was not committed
    ~transaction();

    /// Commit.
    void commit();

  private:
    /// Our connection
    Connection &m_conn;

    /// Have we committed?
    bool m_com;
  };

  template <typename V>
  exec &exec::add(const Optional<V>& v)
  {
    if (v.isSet())
      return add(v.get());
    m_parms.push_back(0);
    return *this;
  }

  template <typename V>
  exec &exec::add(const V& v)
  {
    std::ostringstream str;
    str << v;
    // Validate that this is UTF-8
    validateUTF8(str.str());
    // Copy the string to a zero-terminated char array
    const size_t s = str.str().size();
    char *nstr = new char[s + 1];
    memcpy(nstr, str.str().c_str(), s);
    nstr[s] = 0;
    m_parms.push_back(nstr);
    return *this;
  }

  template <typename V>
  exec &exec::sender(const V &v)
  {
    // Add converter to list of converters
    m_txers.push_back(papply<exec&,exec,const V&>(this, &exec::add<V>, v).clone());
    return *this;
  }

  template <typename V>
  query &query::add(const V& v)
  {
    std::ostringstream str;
    str << v;
    // Validate that this is UTF-8
    validateUTF8(str.str());
    // Copy the string to a zero-terminated char array
    const size_t s = str.str().size();
    char *nstr = new char[s + 1];
    memcpy(nstr, str.str().c_str(), s);
    nstr[s] = 0;
    m_parms.push_back(nstr);
    return *this;
  }

  template <typename V>
  query &query::add(const Optional<V>& v)
  {
    if (v.isSet())
      return add(v.get());
    m_parms.push_back(0);
    return *this;
  }

  template <typename V>
  query &query::get(Optional<V> &v)
  {
    // See that we're not past the last row
    if (PQntuples(m_pgres) <= m_crow)
      throw error("Optional get() row past last row");
    // See that we're not past the last column
    if (PQnfields(m_pgres) <= m_ccol)
      throw error("Optional get() column past last column");
    // Is this field null?
    if (PQgetisnull(m_pgres, m_crow, m_ccol)) {
      v = Optional<V>();
      m_ccol++;
      return *this;
    }
    // No, not null.
    V tmp;
    get<V>(tmp);
    v = tmp;
    return *this;
  }

  template <typename V>
  query &query::get(V &v)
  {
    // See that we're not past the last row
    if (PQntuples(m_pgres) <= m_crow)
      throw error("get() row past last row");
    // See that we're not past the last column
    if (PQnfields(m_pgres) <= m_ccol)
      throw error("get() column past last column");
    std::string field(PQgetvalue(m_pgres, m_crow, m_ccol++));
    try {
      // Fine, get the result data
      v = string2Any<V>(field);
      return *this;
    } catch (error &e) {
      MTrace(t_sql, trace::Warn, "Parse error (" + e.toString()
             + ") on db field: \"" + field + "\"");
      throw e;
    }
  }

  template <typename V>
  query &query::receiver(V &v)
  {
    // Add converter to list of converters
    m_rxers.push_back(papply<query&,query,V&>(this, &query::get, v).clone());
    return *this;
  }


}

#endif
