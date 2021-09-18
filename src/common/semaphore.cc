///
/// Semaphore implementation
///
//
// $Id: semaphore.cc,v 1.2 2013/05/30 21:22:45 vg Exp $
//

#include "semaphore.hh"

#if defined(__APPLE__)
Mutex Semaphore::m_sequence_mutex;
size_t Semaphore::m_sequence(0);
#endif


