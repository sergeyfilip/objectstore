//
// $Id: singleton.hh,v 1.2 2013/03/13 09:30:05 vv Exp $
//
// A simple way of creating a singleton class.
// Simply inherit from Singleton<MyClass> and you can use
// getInst() and getInstPtr() to get a reference or pointer to
// the object from anywhere.
//
// Inspired by Game Programming Gems::"An Automatic Singleton Utility"
// by Scott Bilas
//

#ifndef COMMON_CPP_SINGLETON_HH
#define COMMON_CPP_SINGLETON_HH

#include "error.hh"

template <typename T>
class Singleton
{
public:
  Singleton()
  {
    MAssert(!my_singleton,
            "Double instantiation of singleton class");
    my_singleton = static_cast<T*>(this);
  }
  ~Singleton()
  {
    MAssert(my_singleton,
            "Whoa, destroying null singleton...");
    my_singleton = 0;
  }
  static T &getInst()
  {
    MAssert(my_singleton,
            "Asking for instance of non-existing singleton");
    return *my_singleton;
  }
  static T *getInstPtr()
  {
    // no assert since a 0 return can be used to test for existance of
    // object.
    return my_singleton;
  }

private:
  static T *my_singleton;
};

template <typename T> T *Singleton<T>::my_singleton = 0;

#endif

