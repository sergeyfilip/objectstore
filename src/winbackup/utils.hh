///
/// Different helper routines
///
// $Id: $
//

#ifndef WINBACKUP_UTILS_HH
#define WINBACKUP_UTILS_HH

#include <string>
#include <shlobj.h>

namespace Utils {
  /// Extract the path of a KNOWNFOLDERID folder. Third argument true
  /// means we will attempt to create the location if it does not
  /// exist.
  std::string getFolderPath(REFKNOWNFOLDERID id, const std::string &name,
			    bool create = false);
}
#endif
