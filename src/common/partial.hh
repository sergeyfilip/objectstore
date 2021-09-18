///
/// Partial application
///
// $Id: partial.hh,v 1.7 2013/07/23 15:07:46 vg Exp $
///

#ifndef COMMON_PARTIAL_HH
#define COMMON_PARTIAL_HH

template <typename R>
class BindBase {
public:
  virtual R operator()() const = 0;
  virtual ~BindBase() { }
  virtual BindBase* clone() const = 0;
};

template <typename R, typename Obj>
class BindBaseM {
public:
  virtual R operator()(Obj&) const = 0;
  virtual ~BindBaseM() { }
  virtual BindBaseM* clone() const = 0;
};

template <typename R, typename F1>
class BindF1Base {
public:
  virtual R operator()(F1) const = 0;
  virtual ~BindF1Base() { }
  virtual BindF1Base* clone() const = 0;
};

// Bind0 for static function
template <typename R>
class Bind0 : public BindBase<R> {
public:
  Bind0(R (*f)()) : f(f) { }
  virtual ~Bind0() { }
  R operator()() const { return f(); }
  R (*function() const)() { return f; }
  Bind0<R>* clone() const { return new Bind0<R>(*this); }
private:
  R (*f)();
};

// Bind0M for method of a class
template <typename R, typename Obj>
class Bind0M : public BindBaseM<R, Obj> {
public:
  Bind0M(R (Obj::*f)()) : f(f) { }
  virtual ~Bind0M() { }
  R operator()(Obj& obj) const { return (obj.*f)(); }
  R (Obj::*function() const)() { return f; }
  Bind0M<R, Obj>* clone() const { return new Bind0M<R, Obj>(*this); }
private:
  R (Obj::*f)();
};
// END of Bind0

template <typename R, typename F1>
class Bind0F1 : public BindF1Base<R,F1> {
public:
  Bind0F1(R (*f)(F1)) : f(f) { }
  virtual ~Bind0F1() { }
  R operator()(F1 arg) const { return f(arg); }
  R (*function(F1) const)() { return f; }
  Bind0F1<R,F1>* clone() const { return new Bind0F1<R,F1>(*this); }
private:
  R (*f)(F1);
};

// Bind1 for static function
template <typename R, typename A1>
class Bind1 : public BindBase<R> {
public:
  Bind1(R (*f)(A1), A1 a1)
    : f(f), a1(a1) { }
  virtual ~Bind1() { }
  R operator()() const { return f(a1); }
  R (*function() const)(A1) { return f; }
  Bind1<R,A1>* clone() const { return new Bind1<R,A1>(*this); }
private:
  R (*f)(A1);
  A1 a1;
};

// Bind1M for method of a class
template <typename R, typename Obj, typename A1>
class Bind1M : public BindBaseM<R, Obj> {
public:
  Bind1M(R (Obj::*f)(A1), A1 a1)
    : f(f), a1(a1) { }
  virtual ~Bind1M() { }
  R operator()(Obj& obj) const { return (obj.*f)(a1); }
  R (Obj::*function() const)(A1) { return f; }
  Bind1M<R, Obj, A1>* clone() const { return new Bind1M<R, Obj, A1>(*this); }
private:
  R (Obj::*f)(A1);
  A1 a1;
};
// END of Bind1

// Bind2 for static function
template <typename R, typename A1, typename A2>
class Bind2 : public BindBase<R> {
public:
  Bind2(R (*f)(const A1, const A2), A1 a1, A2 a2)
    : f(f), a1(a1), a2(a2) { }
  virtual ~Bind2() { }
  R operator()() const { return f(a1,a2); }
  R (*function() const)(A1, A2) { return f; }
  Bind2<R,A1,A2>* clone() const { return new Bind2<R,A1,A2>(*this); }
private:
  R (*f)(A1, A2);
  A1 a1;
  A2 a2;
};

// Bind2M for method of a class
template <typename R, typename Obj, typename A1, typename A2>
class Bind2M : public BindBaseM<R, Obj> {
public:
  Bind2M(R (Obj::*f)(const A1, const A2), A1 a1, A2 a2)
    : f(f), a1(a1), a2(a2) { }
  virtual ~Bind2M() { }
  R operator()(Obj& obj) const { return (obj.*f)(a1,a2); }
  R (Obj::*function() const)(A1, A2) { return f; }
  Bind2M<R,Obj,A1,A2>* clone() const { return new Bind2M<R,Obj,A1,A2>(*this); }
private:
  R (Obj::*f)(A1, A2);
  A1 a1;
  A2 a2;
};
// END of Bind2

// Bind3 for static function
template <typename R, typename A1, typename A2, typename A3>
class Bind3 : public BindBase<R> {
public:
  Bind3(R (*f)(const A1, const A2, const A3), A1 a1, A2 a2, A3 a3)
    : f(f), a1(a1), a2(a2), a3(a3) { }
  virtual ~Bind3() { }
  R operator()() const { return f(a1,a2,a3); }
  R (*function() const)(A1, A2, A3) { return f; }
  Bind3<R,A1,A2,A3>* clone() const { return new Bind3<R,A1,A2,A3>(*this); }
private:
  R (*f)(A1, A2, A3);
  A1 a1;
  A2 a2;
  A3 a3;
};

//! Function object that binds an object and a member function with
//! zero parameters
template <class InstanceT, class Out>
class Closure0 : public BindBase<Out> {
public:
  inline Closure0(InstanceT *i, Out (InstanceT::*f)()):
    instance(i),
    fun(f) {}
  virtual ~Closure0() { }
  Out operator() () const { return (instance->*fun)(); }
  Closure0<InstanceT,Out>* clone() const {
    return new Closure0<InstanceT,Out>(*this);
  }
private:
  InstanceT *instance;
  Out (InstanceT::*fun)();
};


//! Create a closure with zero free parameters from a function with
//! zero parameters.
template <class Out>
inline Bind0<Out> papply(Out (*f)())
{
  return Bind0<Out>(f);
}

//! Create a closure with zero free parameters from an object member function with
//! zero parameters.
template <class Out, class Obj>
inline Bind0M<Out, Obj> papply(Out (Obj::*f)())
{
  return Bind0M<Out, Obj>(f);
}

//! Create a closure with one free parameter from a function with one
//! parameter.
template <class Out, class In>
inline Bind0F1<Out,In> papply(Out (*f)(In))
{
  return Bind0F1<Out,In>(f);
}

//! Create a closure with zero free parameters from a function with
//! one parameter.
template <class Out, class In>
inline Bind1<Out,In> papply(Out (*f)(In), In in)
{
  return Bind1<Out,In>(f, in);
}

//! Create a closure with zero free parameters from an object member function with
//! one parameter.
template <class Out, class Obj, class In>
inline Bind1M<Out, Obj, In> papply(Out (Obj::*f)(In), In in)
{
  return Bind1M<Out, Obj, In>(f, in);
}

//! Create a closure with zero free parameters from a function with
//! two parameters.
template <class Out, class In1, class In2>
inline Bind2<Out,In1,In2> papply(Out (*f)(In1,In2), In1 in1, In2 in2)
{
  return Bind2<Out,In1,In2>(f, in1, in2);
}

//! Create a closure with zero free parameters from an object member function with
//! two parameters.
template <class Out, class Obj, class In1, class In2>
inline Bind2M<Out, Obj, In1, In2> papply(Out (Obj::*f)(In1,In2), In1 in1, In2 in2)
{
  return Bind2M<Out, Obj, In1, In2>(f, in1, in2);
}

//! Create a closure with zero free parameters from a function with
//! three parameters.
template <class Out, class In1, class In2, class In3>
inline Bind3<Out,In1,In2,In3> papply(Out (*f)(In1,In2,In3), In1 in1, In2 in2, In3 in3)
{
  return Bind3<Out,In1,In2,In3>(f, in1, in2, in3);
}

//! Function object that binds a constant object and a const member
//! function with zero parameters
template <class InstanceT, class Out>
class Closure0c : public BindBase<Out> {
public:
  inline Closure0c(const InstanceT *i, Out (InstanceT::*f)() const):
    instance(i),
    fun(f) {}
  virtual ~Closure0c() { }
  Out operator() () const { return (instance->*fun)(); }
  Closure0c<InstanceT,Out>* clone() const {
    return new Closure0c<InstanceT,Out>(*this);
  }
private:
  const InstanceT *instance;
  Out (InstanceT::*fun)() const;
};

template <class InstanceT, class Out, class In>
class Closure1 : public BindF1Base<Out,In> {
public:
  inline Closure1(InstanceT *i, Out (InstanceT::*f)(In)):
    instance(i),
    fun(f) {}
  virtual ~Closure1() { }
  Out operator() (In in) const { return (instance->*fun)(in); }
  Closure1<InstanceT,Out,In>* clone() const {
    return new Closure1<InstanceT,Out,In>(*this);
  }
private:
  InstanceT *instance;
  Out (InstanceT::*fun)(In);
};


//! Function object that binds a constant object and a const member
//! function with one parameter
template <class InstanceT, class Out, class In>
class Closure1c : public BindF1Base<Out,In> {
public:
  inline Closure1c(const InstanceT *i, Out (InstanceT::*f)(In) const):
    instance(i),
    fun(f) {}
  virtual ~Closure1c() { }
  Out operator() (In in) const { return (instance->*fun)(in); }
  Closure1c<InstanceT,Out,In>* clone() const {
    return new Closure1c<InstanceT,Out,In>(*this);
  }
private:
  const InstanceT *instance;
  Out (InstanceT::*fun)(In) const;
};


//! Closure with one bound parameter and zero free parameters
template <class InstanceT, class Out, class Bound>
class Closure0B1 : public BindBase<Out> {
public:
  template <class C>
  inline Closure0B1(C *i, Out (C::*f)(Bound), Bound b):
    instance(i), fun(f), bound(b) { }
  virtual ~Closure0B1() { }
  Out operator() () const { return (instance->*fun)(bound); }
  Closure0B1<InstanceT,Out,Bound>* clone() const {
    return new Closure0B1<InstanceT,Out,Bound>(*this);
  }
private:
  InstanceT *instance;
  Out (InstanceT::*fun)(Bound);
  Bound bound;
};

//! Const closure with one bound parameter and zero free parameters
template <class InstanceT, class Out, class Bound>
class Closure0B1c : public BindBase<Out> {
public:
  template <class C>
  inline Closure0B1c(const C *i, Out (C::*f)(Bound) const, Bound b):
    instance(i), fun(f), bound(b) { }
  virtual ~Closure0B1c() { }
  Out operator() () const { return (instance->*fun)(bound); }
  Closure0B1c<InstanceT,Out,Bound>* clone() const {
    return new Closure0B1c<InstanceT,Out,Bound>(*this);
  }
private:
  const InstanceT *instance;
  Out (InstanceT::*fun)(Bound) const;
  Bound bound;
};

//! Closure with two bound parameters and zero free parameters
template <class InstanceT, class Out, class Bound1, class Bound2>
class Closure0B2 : public BindBase<Out> {
public:
  template <class C>
  inline Closure0B2(C *i, Out (C::*f)(Bound1,Bound2), Bound1 b1, Bound2 b2):
    instance(i), fun(f), bound1(b1), bound2(b2) { }
  virtual ~Closure0B2() { }
  Out operator() () const { return (instance->*fun)(bound1,bound2); }
  Closure0B2<InstanceT,Out,Bound1,Bound2>* clone() const {
    return new Closure0B2<InstanceT,Out,Bound1,Bound2>(*this);
  }
private:
  InstanceT *instance;
  Out (InstanceT::*fun)(Bound1,Bound2);
  Bound1 bound1;
  Bound2 bound2;
};

//! Const closure with two bound parameters and zero free parameters
template <class InstanceT, class Out, class Bound1, class Bound2>
class Closure0B2c : public BindBase<Out> {
public:
  template <class C>
  inline Closure0B2c(const C *i, Out (C::*f)(Bound1,Bound2) const, Bound1 b1, Bound2 b2)
    : instance(i), fun(f), bound1(b1), bound2(b2) { }
  virtual ~Closure0B2c() { }
  Out operator() () const { return (instance->*fun)(bound1,bound2); }
  Closure0B2c<InstanceT,Out,Bound1,Bound2>* clone() const {
    return new Closure0B2c<InstanceT,Out,Bound1,Bound2>(*this);
  }
private:
  const InstanceT *instance;
  Out (InstanceT::*fun)(Bound1,Bound2) const;
  Bound1 bound1;
  Bound2 bound2;
};

//! Const closure with three bound parameters and zero free parameters
template <class InstanceT, class Out, class Bound1, class Bound2, class Bound3>
class Closure0B3c : public BindBase<Out> {
public:
  template <class C>
  inline Closure0B3c(const C *i, Out (C::*f)(Bound1,Bound2,Bound3) const,
                     Bound1 b1, Bound2 b2, Bound3 b3)
    : instance(i), fun(f), bound1(b1), bound2(b2), bound3(b3) { }
  virtual ~Closure0B3c() { }
  Out operator() () const { return (instance->*fun)(bound1,bound2,bound3); }
  Closure0B3c<InstanceT,Out,Bound1,Bound2,Bound3>* clone() const {
    return new Closure0B3c<InstanceT,Out,Bound1,Bound2,Bound3>(*this);
  }
private:
  const InstanceT *instance;
  Out (InstanceT::*fun)(Bound1,Bound2,Bound3) const;
  Bound1 bound1;
  Bound2 bound2;
  Bound3 bound3;
};

//! Create a const closure with zero free parameters from an object and a
//! member function with zero parameters.
template <class C, class Out>
inline Closure0c<const C,Out> papply(const C *i, Out (C::*f)() const)
{
  return Closure0c<const C, Out>(i, f);
}

//! Create a closure with zero free parameters from an object and a
//! member function with zero parameters.
template <class C, class Out>
inline Closure0<C,Out> papply(C *i, Out (C::*f)())
{
  return Closure0<C, Out>(i, f);
}

//! Create a const closure with one free parameter from an object and
//! a member function with one parameter.
template <class Out, class C, class In>
inline Closure1c<C,Out,In> papply(const C *i, Out (C::*f)(In) const)
{
  return Closure1c<C, Out, In>(i, f);
}

//! Create a closure with one free parameter from an object and a
//! member function with one parameter.
template <class Out, class C, class In>
inline Closure1<C,Out,In> papply(C *i, Out (C::*f)(In))
{
  return Closure1<C, Out, In>(i, f);
}

//! Create a const closure with zero free parameters and one bound
//! parameter from an object, a member function with one parameter and
//! one parameter.
template <class Out, class C, class In>
inline Closure0B1c<C,Out,In> papply(const C *i, Out (C::*f)(In) const, In in)
{
  return Closure0B1c<C, Out, In>(i, f, in);
}

//! Create a closure with zero free parameters and one bound parameter
//! from an object, a member function with one parameter and one
//! parameter.
template <class Out, class C, class In>
inline Closure0B1<C,Out,In> papply(C *i, Out (C::*f)(In), In in)
{
  return Closure0B1<C, Out, In>(i, f, in);
}

//! Create a const closure with zero free parameters and two bound
//! parameters from an object, a member function with one parameter
//! and one parameter.
template <class Out, class C, class In1, class In2>
inline Closure0B2c<C,Out,In1,In2> papply(const C *i, Out (C::*f)(In1,In2) const,
                                         In1 in1, In2 in2)
{
  return Closure0B2c<C, Out, In1,In2>(i, f, in1, in2);
}

//! Create a closure with zero free parameters and two bound
//! parameters from an object, a member function with one parameter
//! and one parameter.
template <class Out, class C, class In1, class In2>
inline Closure0B2<C,Out,In1,In2> papply(C *i, Out (C::*f)(In1,In2), In1 in1, In2 in2)
{
  return Closure0B2<C, Out, In1,In2>(i, f, in1, in2);
}

//! Create a const closure with zero free parameters and three bound
//! parameters from an object, a member function with one parameter
//! and one parameter.
template <class Out, class C, class In1, class In2, class In3>
inline Closure0B3c<C,Out,In1,In2,In3> papply(const C *i, Out (C::*f)(In1,In2,In3) const,
                                             In1 in1, In2 in2, In3 in3)
{
  return Closure0B3c<C, Out, In1,In2,In3>(i, f, in1, in2, in3);
}


#endif
