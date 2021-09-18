//
//
// $Id: refcountptr.hh,v 1.1 2013/01/20 12:42:03 joe Exp $
//
//

#ifndef MODULES_COMMON_CPP_REFCOUNTPTR_HH
#define MODULES_COMMON_CPP_REFCOUNTPTR_HH

template <class T>
class refcount_ptr {
public:
  /// Default constructor - null pointer initialises
  refcount_ptr();

  /// Initialise as first reference to given pointer and assume
  /// ownership of this pointer
  refcount_ptr(T*);

  /// Initialise as copy of a refcounted pointer
  refcount_ptr(const refcount_ptr&);

  /// Unreference and potentially delete (if 0 refcount)
  ~refcount_ptr();

  /// Assignment; no longer reference old pointer, reference new.
  refcount_ptr& operator=(const refcount_ptr&);

  /// Assignment; no longer reference old pointer, become first
  /// reference to new.
  refcount_ptr& operator=(T*);

  /// Dereference pointer
  T* operator->() const;

  /// Address pointer
  T* ptr() const;

  /// Boolean conversion - is the pointer set or not?
  operator bool() const;

  /// Dereference. This is a plain dereference - it will segfault on
  /// null pointer dereference just like one would expect
  const T& operator*() const;

  /// Same as dereference above, just not const
  T& operator*();

private:

  /// Call this whenever we no longer wish to reference what we're
  /// referencing
  void unreference();

  /// Declaration of the pointer and its refcount
  struct container {
    T* pointer;
    size_t refcount;
  };

  /// Pointer to the data area
  container* data;

};


template <class T>
refcount_ptr<T>::refcount_ptr()
  : data(0)
{
}

template <class T>
refcount_ptr<T>::refcount_ptr(T* t)
  : data(0)
{
  // We keep null pointers cheap
  if (t) {
    data = new container;
    data->pointer = t;
    data->refcount = 1;
  }
}

template <class T>
refcount_ptr<T>::refcount_ptr(const refcount_ptr& o)
{
  // Reference the data area from the other pointer
  data = o.data;
  // Add a reference, if we reference anything
  if (data)
    data->refcount++;
}

template <class T>
refcount_ptr<T>::~refcount_ptr()
{
  unreference();
}

template <class T>
refcount_ptr<T>& refcount_ptr<T>::operator=(const refcount_ptr<T>& o)
{
  // Stop referencing whatever we're referencing
  unreference();
  // Reference whatever the other guy is referencing
  data = o.data;
  if (data)
    data->refcount++;
  return *this;
}

template <class T>
refcount_ptr<T>& refcount_ptr<T>::operator=(T* o)
{
  // Stop referencing whatever we're referencing
  unreference();
  // Become the first reference to 'o'
  data = new container;
  data->pointer = o;
  data->refcount = 1;
  return *this;
}

template <class T>
T* refcount_ptr<T>::operator->() const
{
  if (!data)
    return 0;
  return data->pointer;
}

template <class T>
T* refcount_ptr<T>::ptr() const
{
  if (!data)
    return 0;
  return data->pointer;
}

template <class T>
refcount_ptr<T>::operator bool() const
{
  return ptr();
}

template <class T>
const T& refcount_ptr<T>::operator*() const
{
  return *ptr();
}

template <class T>
T& refcount_ptr<T>::operator*()
{
  return *ptr();
}

template <class T>
void refcount_ptr<T>::unreference()
{
  // Do bookkeeping if we have anything to do
  if (data) {
    // We no longer reference
    data->refcount--;
    // If data is no longer referenced, delete it
    if (!data->refcount) {
      delete data->pointer;
      // Since we were the last reference, delete the bookkeeping too
      delete data;
      data = 0;
    }
  }
}

#endif
