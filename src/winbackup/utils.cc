///
/// Different helper routines
///
//
// $Id: $
//

#include "utils.hh"
#include "common/error.hh"
#include "common/trace.hh"
#include "common/string.hh"
#include "common/scopeguard.hh"

std::string Utils::getFolderPath(REFKNOWNFOLDERID id, const std::string &name, bool create)
{
  PIDLIST_ABSOLUTE pidl;
  if (S_OK != SHGetKnownFolderIDList(id, (create ? KF_FLAG_CREATE : 0), 0, &pidl)
      || !pidl)
    throw error("Cannot look up IDList " + name);
  ON_BLOCK_EXIT(ILFree, pidl);
  // Set the file name if we got one
  wchar_t path[MAX_PATH];
  if (!SHGetPathFromIDList(pidl, path))
    throw error("Cannot get path from IDList " + name);
  // Ensure all paths start with \\\\?\\ - so that we support long names
  std::string out(utf16_to_utf8(path));
  if (out.find("\\\\?\\") != 0)
    out = "\\\\?\\" + out;
  return out;
}
