//-*-C++-*-
//
//! \file common/notifier.hh
//! Header-implementation of Notifier pattern
//

#ifndef COMMON_CPP_NOTIFIER_HH
#define COMMON_CPP_NOTIFIER_HH

#include <list>
#include <algorithm>
#include "mutex.hh"

template<typename Listener>
class Notifier
{
private:
    typedef typename std::list<Listener*> ListenersList_t;
    ListenersList_t m_listeners;
    int m_lockCount;
    mutable Mutex m_lock;

public:
    Notifier()
        : m_lockCount(0)
    {}

    virtual ~Notifier()
    {}

    bool hasListener(Listener& listener) const
    {
        MutexLock l(m_lock);
        return m_listeners.end() != std::find(m_listeners.begin(), m_listeners.end(), &listener);
    }

    void addListener(Listener& listener)
    {
        MutexLock l(m_lock);
        m_listeners.push_back(&listener);
    }

    virtual void removeListener(Listener& listener)
    {
        MutexLock l(m_lock);
        if(m_lockCount) {
            std::replace(m_listeners.begin(), m_listeners.end(), &listener, (Listener*)0);
        } else {
            m_listeners.remove(&listener);
        }
    }

protected:
    template <class Functor>
    void notify(Functor functor, bool notifyOnce = false)
    {
        MutexLock l(m_lock);
        ++m_lockCount;
        for(typename ListenersList_t::iterator it = m_listeners.begin(); it != m_listeners.end(); ++it) {
            Listener* listener = *it;
            if (notifyOnce) {
                *it = (Listener*)0;
            }
            if(listener) {
                functor(*listener);
            }
        }
        if(--m_lockCount == 0) {
            m_listeners.remove((Listener*)0);
        }
    }
};

#endif