///
/// POSIX implementation of backup upload methods
///
//
// $Id: upload_posix.cc,v 1.28 2013/10/06 11:48:08 vg Exp $
//

#include "objparser/objparser.hh"

#if defined(__linux__)
bool Upload::touchPathWD(int wd)
{
  // Treat event - we want to locate the directory that was change
  inotify2wnode_t::iterator di = m_inotify2wnode.find(wd);
  // We may have gotten a watch event for something that doesn't
  // exist any more - just ignore that.
  if (di == m_inotify2wnode.end()) {
    MTrace(t_up, trace::Info,
           "Got watch event on non-existing directory - ignoring");
  } else {
    MTrace(t_up, trace::Debug, "directory "
           << di->second->name << " was touched");
    // See if we are excluded by a filter
    if (m_filter && !(*m_filter)(di->second->absName())) {
      MTrace(t_up, trace::Debug, "Touched directory filtered - ignoring");
    } else {
      // Fine, mark dirstate as touched then
      di->second->markTouched();
      return true;
    }
  }
  return false;
}
#endif

bool Upload::dirstate_t::scan(Processor &proc)
{
  // Generate absolute path
  const std::string path(absPath());

  MTrace(t_up, trace::Debug, "Handling children under " << path);

#if defined(__linux__)
  //
  // Watch, if we're not being watched already
  //
  if (watch.m_wd == -1 && proc.refUpload().m_addWatchDelegate) {
    watch.m_wd = (*proc.refUpload().m_addWatchDelegate)(path);
    // If we got -1 back, it means the entry disappeared under us and
    // we just skip it
    if (watch.m_wd == -1)
      return false;
    // Set up mapping - with proper locking
    MutexLock l(proc.refUpload().m_dirstate_lock);
    proc.refUpload().m_inotify2wnode[watch.m_wd] = &watch;
  }
#endif

  // Get file system info
  objinfo_t objinfo;
  { int statrc;
    while ((statrc = statfs(path.c_str(), &objinfo.fs)) && errno == EINTR);
    if (statrc) {
      // If we fail for any reason, skip
      MTrace(t_up, trace::Info, "Skipping (due to statfs error: "
             << strerror(errno) << ") " << path);
      return false;
    }
  }

  // Traverse directory...
  DIR *dir = opendir(path.c_str());
  if (!dir) {
    // If we fail for any reason, skip
    MTrace(t_up, trace::Info, "Skipping (due to opendir error: "
           << strerror(errno) << ") " << path);
    return false;
  }
  ON_BLOCK_EXIT(closedir, dir);

  struct dirent *de;
  while ((de = readdir(dir))) {
    // Skip dummy entries
    { const std::string entry(de->d_name);
      if (entry == "." || entry == "..")
        continue;
    }

    // We pass this struct on to the filter later on
    objinfo.abspath = (*path.rbegin() == '/' ? path : (path + "/")) + de->d_name;

    // Stat this to see what it is
    if (-1 == lstat(objinfo.abspath.c_str(), &objinfo.st)) {
      // If lstat fails on this object for any reason, we log it and
      // skip it
      MTrace(t_up, trace::Info, "Scan skipping (due to lstat error: "
             << strerror(errno) << ") " << objinfo.abspath);
      continue;
    }

    // If this is not a directory then we do not care about it for now
    if (!S_ISDIR(objinfo.st.st_mode)) {
      MTrace(t_up, trace::Debug, "Scan skipping non-directory "
             << de->d_name);
      continue;
    }

    //
    // Check with filter...
    //
    if (proc.refUpload().m_filter) {
      if (!(*proc.refUpload().m_filter)(objinfo.abspath)) {
        MTrace(t_up, trace::Debug, "Filter skipping " << objinfo.abspath);
        continue;
      }
    }

    // See if we have dealt with this object before. Even if not found
    // in cache, the child_cobj gets its device/inode info set.
    CObject child_cobj;
    const bool child_unchanged
      = proc.refUpload().m_cache.readObj(fsobjid_t(objinfo.st), child_cobj);

    //
    // If watch.getChild() does not find the child it will create a
    // new node for it. If it does so, the constructor of wnode_t will
    // ensure that all parents are marked as m_cdp_children_queued and
    // that the newly created node is marked as m_cdp_queued.
    //
    // getChild can change the m_wroot tree and must be called holding
    // the wroot_lock
    //
    dirstate_t *nc;
    { MutexLock l(proc.refUpload().m_wroot_lock);
      nc = new dirstate_t(de->d_name, this, watch.getChild(de->d_name), child_cobj);
    }
    nc->meta_uid = objinfo.st.st_uid;
    nc->meta_gid = objinfo.st.st_gid;
    nc->meta_mode = objinfo.st.st_mode;

    // If we are allowed to skip this directory AND we know its
    // hash/treesize, well, take the shortcut...
    if (!nc->watch.m_cdp_queued && child_unchanged) {
      MTrace(t_cdp, trace::Debug, " Optimised out traversal of "
             << objinfo.abspath);

      // Insert directly as a complete child.
      complete_children.push_back(nc);
    } else {
      MTrace(t_cdp, trace::Debug, " Need to CDP traverse "
             << objinfo.abspath);
      // We need to have this child processed before we can know
      // its hash.
      //
      // We do not need to take the dirstate lock for adding the nc to
      // our incomplete children, because we add the new child directory
      // to our incomplete children *before* adding it to the work
      // queue. Thus, no-one can possibly process this child and no-one
      // can possibly back-track (via the childs parent pointer) to us,
      // until after we're done adding it to our incomplete_children
      // list.
      //
      incomplete_children.insert(nc);
    }

  }

  //
  // If we have no incomplete children and therefore will never be
  // back-tracked to, report that.
  //
  if (incomplete_children.empty())
    return false;

  return true; // we will be back-tracked to
}

void Upload::dirstate_t::upload(Processor &proc)
{
  const std::string path(absPath());
  MTrace(t_up, trace::Debug, "Uploading directory " << path);

  //
  // We will reconstruct the directory object(s) as it will be on the
  // server, during the traversal here. This allows us to discover
  // metadata changes, new child objects and such.
  //
  // Our LoR is an integer (the length of the list) and a byte
  // sequence (32 bytes per hash in the list).  We will construct our
  // LoR as we traverse the children.
  //
  // Our list of child meta data (LoM) holds our own encoding of file
  // system child object meta data for each child object.
  //
  // This is a list of LoR and LoM entries which we use to later
  // encode our object(s) (plural if split due to size restrictions).
  std::list<dirobj_t> dirobj_lorm;

  // We pass this struct on to the filter later on
  objinfo_t objinfo;

  // Get file system info
  { int statrc;
    while ((statrc = statfs(path.c_str(), &objinfo.fs)) && errno == EINTR);
    if (statrc) {
      // If we fail for any reason, skip
      MTrace(t_up, trace::Info, "Skipping (due to statfs error: "
             << strerror(errno) << ") " << path);
      return;
    }
  }

  // Traverse directory...
  DIR *dir = opendir(path.c_str());
  if (!dir) {
    // If directory disappeared, handle gracefully
    if (errno == EACCES || errno == ENOENT) {
      MTrace(t_up, trace::Info, "Skipping (due to opendir) " << path);
      return;
    }
    throw syserror("opendir", "upload opening directory " + path);
  }
  ON_BLOCK_EXIT(closedir, dir);

  struct dirent *de;
  while ((de = readdir(dir))) {
    // We're scanning...
    proc.setStatus(threadstatus_t::OSScanning, name);

    // Skip dummy entries
    { const std::string entry(de->d_name);
      if (entry == "." || entry == "..")
        continue;
    }

    objinfo.abspath = (*path.rbegin() == '/' ? path : (path + "/")) + de->d_name;

    // Stat this to see what it is
    if (-1 == lstat(objinfo.abspath.c_str(), &objinfo.st)) {
      // If lstat fails on this object for any reason, we log it and
      // skip it
      MTrace(t_up, trace::Info, "Skipping (due to lstat error: "
             << strerror(errno) << ") " << objinfo.abspath);
      continue;
    }

    // If this is a directory, then we already should have it in our
    // complete_children container.
    if (S_ISDIR(objinfo.st.st_mode))
      continue;

    // See if file was previously backed up
    CObject child_cobj;
    const bool child_unchanged
      = proc.refUpload().m_cache.readObj(fsobjid_t(objinfo.st), child_cobj);

    // We track if the mtime/ctime was updated - because only then
    // will we need to update the cache. In case this object wasn't
    // known, this value is unused (we will then insert the cobj in
    // the cache)
    bool meta_changed = false;

    //
    // Check with filter...
    //
    // Note, the filter may modify fields in the objinfo structure so
    // we must update the cache with it
    //
    if (proc.refUpload().m_filter) {
      if (!(*proc.refUpload().m_filter)(objinfo.abspath)) {
        MTrace(t_up, trace::Debug, "Filter skipping " << objinfo.abspath);
        continue;
      }
    }

    // We encode the meta data about this object as we treat it in
    // more detail further down.
    std::vector<uint8_t> curr_meta;
    curr_meta.reserve(8192); // initial reservation of space. A few
                             // kilobytes won't hurt at every level in
                             // the file tree structure (a few k times
                             // the depth of the tree) - and the
                             // vector will quickly ramp it up if need
                             // be.

    if (S_ISREG(objinfo.st.st_mode)) {
      MTrace(t_up, trace::Debug, "  Processing as regular file");
      //
      // Encode regular file meta-data in our LoM entry
      //
      ser(curr_meta, uint8_t(0x01)); // 0x01 => regular file
      // name
      ser(curr_meta, std::string(de->d_name));
      // owner user and group
      ser(curr_meta, proc.username(objinfo.st.st_uid)); // owner user
      ser(curr_meta, proc.groupname(objinfo.st.st_gid)); // owner group
      // permissions
      ser(curr_meta, uint32_t(objinfo.st.st_mode & 07777));
      // mtime and ctime - we do not back up atime
      ser(curr_meta, uint64_t(objinfo.st.st_mtime));
      ser(curr_meta, uint64_t(objinfo.st.st_ctime));
      // size of file
      ser(curr_meta, uint64_t(objinfo.st.st_size));

      //
      // Upload new regular file - set 0% status if file is multi-chunk.
      //
      proc.setStatus(threadstatus_t::OSUploading, de->d_name,
                     (size_t)objinfo.st.st_size > ng_chunk_size
                     ? Optional<double>(0) : Optional<double>());
  
      //
      // If only the ctime changed and not the mtime, then we do
      // not bother going through the file data.
      //
      if (child_unchanged) {
        MTrace(t_up, trace::Debug, "   File is known and contents unchanged");
      } else {
        MTrace(t_up, trace::Debug, "   File contents are new or changed");

        // The cobj is the actual file data. The hash list is the
        // list of all chunk hashes.
        //
        // Simply open the file, read chunks one at a time.
        int fd;
        while (-1 == (fd = open(objinfo.abspath.c_str(), O_RDONLY, 0))
               && errno == EINTR);
        if (fd == -1) {
          MTrace(t_up, trace::Warn, "Unable to open file \"" << objinfo.abspath
                 << "\" (" << strerror(errno) << ") - skipping");
          continue;
        }
        ON_BLOCK_EXIT(close, fd);

        // We construct a new hash sequence for the new chunks. We
        // should only upload hashes that we cannot find in our
        // local cache - however, this is an optimisation for
        // later... Since we do not write this to the database until
        // the upload of the full file data is complete, we are
        // guaranteed that whatever we have in our db also exists on
        // the back end.
        objseq_t newhash;
        uint64_t treesize = 0;

        // Now read a chunk at a time
        while (true) {
          std::vector<uint8_t> chunk;
          ser(chunk, uint8_t(0x00)); // Version 0 object
          ser(chunk, uint8_t(0xfd)); // object type = file data
          chunk.resize(ng_chunk_size);
          int rrc;
          size_t ofs = 2;
          do {
            rrc = read(fd, &chunk[ofs], chunk.size() - ofs);
            if (rrc == -1 && errno == EINTR)
              continue;
            if (rrc == -1)
              throw syserror("read", "reading file data from "
                             + objinfo.abspath + " for backup");
            ofs += rrc;
          } while (rrc && ofs < chunk.size());

          MTrace(t_up, trace::Debug, "   Read " << ofs - 2 << " bytes of chunk data");
          if (!(ofs - 2))
            break;

          // Fine, we have a chunk.
          chunk.resize(ofs);
          newhash.push_back(sha256::hash(chunk));
          treesize += chunk.size();

          // Verify with server that it is there
          if (proc.refUpload().testObject(proc.refConn(), newhash.back())) {
            MTrace(t_up, trace::Debug, "    Chunk " << newhash.back().m_hex
                   << " already exists on server");
          } else {
            MTrace(t_up, trace::Debug, "    Chunk " << newhash.back().m_hex
                   << " needs upload!");
            proc.refUpload().uploadObject(proc.refConn(), chunk);
          }

          // Update status for large uploads
          if (objinfo.st.st_size > off_t(ng_chunk_size))
            proc.setStatus(threadstatus_t::OSUploading, de->d_name,
                           std::min(1., 1. * treesize / objinfo.st.st_size));

          // If this chunk was smaller than max, then we are done. We
          // do not want to continue reading small blocks from a log
          // file for example
          if (chunk.size() < ng_chunk_size)
            break;
        }

        // We have now read all chunks. Our newhash contains the new
        // list of hashes for the metadata entry in our containing
        // directory.
        child_cobj.m_hash = newhash;
        child_cobj.m_treesize = treesize;
        meta_changed = true;
      }

    } else {

      MTrace(t_up, trace::Debug, "  Skipping unknown object type");
      continue;
    }

    //
    // Our child cobj is now updated with current information.
    //
    if (child_cobj.m_dbid != -1) {
      if (meta_changed)
        proc.refUpload().m_cache.update(child_cobj);
    } else {
      proc.refUpload().m_cache.insert(child_cobj);
    }
    MTrace(t_up, trace::Debug, "Object treesize is: " << child_cobj.m_treesize);

    //
    // Add the list of hashes to our directory LoR
    // Add our child object meta-data to our directory object
    //
    dirobj_lorm.push_back(dirobj_t(child_cobj.m_hash, curr_meta,
                                   child_cobj.m_treesize));

    continue;
  }

  MTrace(t_up, trace::Debug, " Directory scan processing done. "
         << "Now process complete children.");

  //
  // Our child directories should already be processed and available
  // under complete_children. We do not need to hold any lock to
  // access them because they are not accessed by anyone but us (their
  // processing is done)
  //
  // We really want to sort these by any fs-specific key - name, id,
  // whatever. Since our processing is parallel, the children may be
  // in any order but we really want them to be in fixed order so that
  // unchanged directories de-duplicate.
  //
  std::vector<dirstate_t*> all_childen_tmp(complete_children.begin(), complete_children.end());
  all_childen_tmp.insert(all_childen_tmp.end(), incomplete_children.begin(), incomplete_children.end());
  
  std::sort(all_childen_tmp.begin(), all_childen_tmp.end(),
            dirstate_t::sortkey);

  for (std::vector<dirstate_t*>::iterator ci = all_childen_tmp.begin();
       ci != all_childen_tmp.end(); ++ci) {

    //
    // Encode directory meta-data LoM entry and store for later
    // (until upload)
    //
    std::vector<uint8_t> meta;
    ser(meta, uint8_t(0x02)); // 0x02 => directory
    // name
    ser(meta, std::string((*ci)->name));
    // owner user and group
    ser(meta, proc.username((*ci)->meta_uid)); // owner user
    ser(meta, proc.groupname((*ci)->meta_gid)); // owner group
    // permissions
    ser(meta, uint32_t((*ci)->meta_mode & 07777));
    // mtime and ctime - we do not back up atime
    ser(meta, uint64_t((*ci)->cobj.m_id.mtime_s));
    ser(meta, uint64_t((*ci)->cobj.m_id.ctime_s));

    // Add LoR and LoM data
    dirobj_lorm.push_back(dirobj_t((*ci)->cobj.m_hash, meta,
                                   (*ci)->cobj.m_treesize));
  }

  // All objects in directory updated.
  MTrace(t_up, trace::Debug, "Done with all entries under " << path
         << " - will encode directory object(s)");

  //
  // We will build a new multiref and treesize.
  //
  cobj.m_treesize = 0;
  objseq_t existing_hash;
  existing_hash.swap(cobj.m_hash);

  //
  // Now create a directory object holding our LoR and LoM.
  // - split as necessary to stay within ng_chunk_size.
  //
  proc.setStatus(threadstatus_t::OSUploading, name);
  while (!dirobj_lorm.empty()) {

    std::vector<uint8_t> object;
    uint64_t treesize;
    size_t n = encodeDirObjs(object, treesize, dirobj_lorm, !incomplete_children.empty());

    // Remove the entries we serialised
    for (size_t i = 0; i < n; i++) {
      dirobj_lorm.pop_front();
    }

    MTrace(t_up, trace::Debug, "Encoded object of size "
           << object.size());

    //
    // Compute child object hash and add it to the cobj hash list
    //
    const sha256 object_hash(sha256::hash(object));
    cobj.m_treesize += treesize;
    cobj.m_hash.push_back(object_hash);

    //
    // Before we ask the server if we need to upload the object, let's
    // see if our cache already had a match on the dev/ino. If it did,
    // then maybe the hash is unchanged and we don't need to ask the
    // server...
    //
    if (cobj.m_dbid != -1) {
      const size_t current_hash_index = cobj.m_hash.size() - 1;
      if (existing_hash.size() >= cobj.m_hash.size()
          && existing_hash[current_hash_index] == cobj.m_hash[current_hash_index]) {
        MTrace(t_up, trace::Debug, " object previously uploaded.");
        continue;
      } else {
        MTrace(t_up, trace::Debug, " object changed from cached version.");
      }
    } else {
      MTrace(t_up, trace::Debug, " object not previously cached.");
    }


    //
    // Fine, we now have an object to upload.
    //
    if (!proc.refUpload().testObject(proc.refConn(), object_hash)) {
      MTrace(t_up, trace::Debug, " upload necessary");
      proc.refUpload().uploadObject(proc.refConn(), object);
    } else {
      MTrace(t_up, trace::Debug, " object already on servers");
    }
    // Continue until nothing left
  }

  // Done. sizehash set. Update cache with both our sizehash and the
  // mtime/ctime that was set during scan.
  if (parent) {
    if (cobj.m_dbid == -1) {
      proc.refUpload().m_cache.insert(cobj);
    } else {
      proc.refUpload().m_cache.update(cobj);
    }
  }
}

namespace {
  Mutex g_pwuid_lock;
  Mutex g_grgid_lock;
}

std::string Upload::Processor::username(uid_t uid)
{
  MutexLock l(m_uid_lock);

  // Most of the time, files are owned by the same user. Let's
  // optimise out the common case here.
  if (m_last_uid.isSet() && m_last_uid.get().uid == uid)
    return m_last_uid.get().name;

  // Fine, we must do lookup then.
  MutexLock l2(g_pwuid_lock);
  struct passwd *pw = getpwuid(uid);
  if (pw) {
    m_last_uid = l_uid_t(uid, pw->pw_name);
    return pw->pw_name;
  }

  // Return failed lookup
  std::ostringstream numeric;
  numeric << uid;
  m_last_uid = l_uid_t(uid, numeric.str());
  return numeric.str();
}

/// Find textual name for group id
std::string Upload::Processor::groupname(gid_t gid)
{
  MutexLock l(m_gid_lock);

  // Most of the time, files are owned by the same user. Let's
  // optimise out the common case here.
  if (m_last_gid.isSet() && m_last_gid.get().gid == gid)
    return m_last_gid.get().name;

  // Fine, we must do lookup then.
  MutexLock l2(g_grgid_lock);
  struct group *gr = getgrgid(gid);
  if (gr) {
    m_last_gid = l_gid_t(gid, gr->gr_name);
    return gr->gr_name;
  }

  // Return failed lookup
  std::ostringstream numeric;
  numeric << gid;
  m_last_gid = l_gid_t(gid, numeric.str());
  return numeric.str();
}


std::string Upload::dirstate_t::absPath() const
{
  if (parent) {
    std::string pp = parent->absPath();
    // Add slash if pp is not the slash
    if (*pp.rbegin() != '/') pp += "/";
    // Add name and return
    return pp + name;
  }
  // No parent. We're the root. We are then either named "/" or we are
  // named after a directory (possibly ".") in the current working
  // directory.
  return name;
}
