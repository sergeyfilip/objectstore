///
/// Utility Optional container
///
/// Useful for optional XML elements, database NULL handling etc.
///
//
// $Id: optional.hh,v 1.2 2013/05/23 14:08:57 joe Exp $
//

#ifndef COMMON_OPTIONAL_HH
#define COMMON_OPTIONAL_HH

#include "error.hh"

//! Optional - a variable can be set or not - useful for parsing
//! optional XML entries
template <class T>
class Optional {
public:
  Optional();
  Optional(const T &);
  const T &get() const;
  bool isSet() const;
  Optional<T> &operator=(const T&);
private:
  //! Are we set?
  bool m_set;
  //! Our value, in case we are set
  T m_val;
};

template <class S>
std::ostream &operator<<(std::ostream &out, const Optional<S> &obj)
{
  out << obj.get();
  return out;
}

template <class S>
std::istream &operator>>(std::istream &in, Optional<S> &obj)
{
  S tmp; in >> tmp;
  obj = tmp;
  return in;
}

template <class S>
bool operator==(const Optional<S> &a, const Optional<S> &b)
{
  // If both are unset, they are equal
  if (!a.isSet() && !b.isSet())
    return true;
  // If both are set, compare values
  if (a.isSet() && b.isSet())
    return a.get() == b.get();
  // If one is set and the other not, they are not equal
  return false;
}

template <class S>
bool operator!=(const Optional<S> &a, const Optional<S> &b)
{
  return !(a == b);
}

template <class T>
Optional<T>::Optional()
  : m_set(false)
  , m_val(T())
{
}

template <class T>
Optional<T>::Optional(const T &v)
  : m_set(true)
  , m_val(v)
{
}

template <class T>
const T &Optional<T>::get() const
{
  if (!m_set)
    throw error("Unset Optional got");
  return m_val;
}

template <class T>
bool Optional<T>::isSet() const
{
  return m_set;
}

template <class T>
Optional<T> &Optional<T>::operator=(const T& v)
{
  m_set = true;
  m_val = v;
  return *this;
}



#endif
