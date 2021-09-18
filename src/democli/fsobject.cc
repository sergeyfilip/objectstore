//
// Implementation of our FSObject instances
//

#include "common/trace.hh"
#include "common/error.hh"
#include "common/scopeguard.hh"
#include "fsobject.hh"

#ifdef __unix__
# include <sys/types.h>
# include <sys/stat.h>
# include <dirent.h>
# include <unistd.h>
#endif

namespace {
  //! Tracer for meta tree network operations
  trace::Path t_fsobj("/FSObject");
}


