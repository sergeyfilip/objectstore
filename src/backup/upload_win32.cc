///
/// Win32 implementation of backup upload methods
///
// $Id: upload_win32.cc,v 1.20 2013/10/08 07:55:03 vg Exp $
//

// We use std::min in this file
#undef min

std::string Upload::dirstate_t::absPath() const
{
  // Note. Path for any directory ends with slash

  if (parent)
    return parent->absPath() + name + "\\";
  // We should now start with \\\\?\\ to allow for long names
  MAssert(name.find("\\\\?\\") == 0, "No \\\\?\\ prefix on abslute path");
  return name;
}

bool Upload::dirstate_t::scan(Processor &proc)
{
  // Generate absolute path
  const std::string path(absPath());

  MTrace(t_up, trace::Debug, "Handling children under " << path);

  // Traverse directory...
  WIN32_FIND_DATA dirbuf;
  HANDLE dirh = FindFirstFileEx(utf8_to_utf16(path + "*").c_str(),
                                FindExInfoStandard,
                                &dirbuf,
                                FindExSearchLimitToDirectories, 0,
                                0);
  if (dirh == INVALID_HANDLE_VALUE) {
    MTrace(t_up, trace::Info, "Skipping (due to FindFirstFileEx) " << path
           << " error code " << GetLastError());
    return false; // no-one will back track to us
  }
  ON_BLOCK_EXIT(FindClose,dirh);

  do {
    // Skip dummy entries
    const std::string name(utf16_to_utf8(dirbuf.cFileName));
    if (name == "." || name == "..")
      continue;

    // We pass this struct on to the filter later on
    objinfo_t objinfo;
    objinfo.abspath = path + name;

    // If this is not a directory then we do not care about it for now
    if (! (dirbuf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      MTrace(t_up, trace::Debug, "Scan skipping non-directory "
             << name);
      continue;
    }

    // Verify that we can actualy go into the directory and traverse
    // it - we don't want to include directories in our listing that
    // cannot be traversed
    { WIN32_FIND_DATA dirbuf;
      HANDLE dirh = FindFirstFileEx(utf8_to_utf16(objinfo.abspath + "\\*").c_str(),
                                    FindExInfoStandard,
                                    &dirbuf,
                                    FindExSearchLimitToDirectories, 0,
                                    0);
      if (dirh == INVALID_HANDLE_VALUE) {
        MTrace(t_up, trace::Info, "Skipping child " << objinfo.abspath
               << " because it cannot be traversed");
        continue;
      }
      FindClose(dirh);
    }

    // Read the id of the object
    { HANDLE fh;
      fh = CreateFile(utf8_to_utf16(objinfo.abspath).c_str(),
                      0,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, 0);
      if (fh == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        MTrace(t_up, trace::Debug, "Scan skipping due to CreateFile for GetFileInformation "
               << objinfo.abspath << " (error is " << err << ")");
        continue;
      }
      ON_BLOCK_EXIT(CloseHandle, fh);
      if (!GetFileInformationByHandle(fh, &objinfo.fi)) {
        MTrace(t_up, trace::Debug, "Scan skipping due to GetFileInformation "
               << objinfo.abspath);
        continue;
      }
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
    // in cache, the child_cobj gets its id info set.
    MTrace(t_cdp, trace::Debug, "Directory scanner will look for directory in cache");
    CObject child_cobj;
    const bool child_unchanged
      = proc.refUpload().m_cache.readObj(fsobjid_t(objinfo.fi), child_cobj);

    //
    // If watch.getChild() does not find the child it will create a
    // new node for it. If it does so, the constructor of wnode_t will
    // ensure that all parents are marked as m_cdp_children_queued and
    // that the newly created node is marked as m_cdp_queued.
    //
    // Since getChild may modify the wroot tree, we must perform this
    // while holding the wroot lock
    //
    dirstate_t *nc;
    { MutexLock l(proc.refUpload().m_wroot_lock);
      nc = new dirstate_t(name, this, watch.getChild(name), child_cobj);
    }

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

  } while (FindNextFile(dirh, &dirbuf));

  if (GetLastError() != ERROR_NO_MORE_FILES) {
    MTrace(t_up, trace::Info, "Ending traversal of " << path << " due to error");
    // We do not return - we might as well process whatever we got...
  }

  //
  // If we have no incomplete children and therefore will never be
  // back-tracked to, report that.
  //
  if (incomplete_children.empty())
    return false;

  return true; // We will be traversed back into
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

  // Traverse directory...
  WIN32_FIND_DATA dirbuf;
  HANDLE dirh = FindFirstFileEx(utf8_to_utf16(path + "*").c_str(),
                                FindExInfoStandard,
                                &dirbuf,
                                FindExSearchNameMatch, 0, 0);
  if (dirh == INVALID_HANDLE_VALUE) {
    MTrace(t_up, trace::Info, "Upload skipping (due to FindFirstFileEx) " << path);
    return;
  }
  ON_BLOCK_EXIT(FindClose,dirh);

  do {
    // We're scanning...
    proc.setStatus(threadstatus_t::OSScanning, name);

    // Skip dummy entries
    const std::string oname(utf16_to_utf8(dirbuf.cFileName));
    if (oname == "." || oname == "..")
      continue;

    // We pass this struct on to the filter later on
    objinfo_t objinfo;
    objinfo.abspath = path + oname;

    // Read the id of the object
    { HANDLE fh;
      fh = CreateFile(utf8_to_utf16(objinfo.abspath).c_str(),
                      0,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, 0);
      if (fh == INVALID_HANDLE_VALUE) {
        MTrace(t_up, trace::Debug, "Upload skipping due to CreateFile for GetFileInformation "
               << objinfo.abspath);
        continue;
      }
      ON_BLOCK_EXIT(CloseHandle, fh);
      if (!GetFileInformationByHandle(fh, &objinfo.fi)) {
        MTrace(t_up, trace::Debug, "Upload skipping due to GetFileInformation "
               << objinfo.abspath);
        continue;
      }
    }

    // If this is a directory, then we already should have it in our
    // complete_children container.
    if (dirbuf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;

    // See if file was previously backed up
    CObject child_cobj;
    const bool child_unchanged
      = proc.refUpload().m_cache.readObj(fsobjid_t(objinfo.fi), child_cobj);

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
      MTrace(t_up, trace::Debug, "Filter did not skip " << objinfo.abspath);
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

    if (!(objinfo.fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        && !(objinfo.fi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      MTrace(t_up, trace::Debug, "  Processing as regular file");
      //
      // Encode regular file meta-data in our LoM entry
      //
      ser(curr_meta, uint8_t(0x11)); // 0x11 => windows regular file
      // name
      ser(curr_meta, oname);
      // owner user
      ser(curr_meta, "nobody"); // XXX
      // file attributes
      ser(curr_meta, uint32_t(objinfo.fi.dwFileAttributes));
      // SDDL
      ser(curr_meta, ""); // XXX
      // mtime and btime
      ser(curr_meta, uint64_t(Time(objinfo.fi.ftLastWriteTime).to_timet()));
      ser(curr_meta, uint64_t(Time(objinfo.fi.ftCreationTime).to_timet()));
      // size of file
      const uint64_t current_file_size(uint64_t(objinfo.fi.nFileSizeHigh) << 32
                                       | objinfo.fi.nFileSizeLow);
      ser(curr_meta, current_file_size);

      //
      // Upload new regular file - set 0% status if file is multi-chunk.
      //
      proc.setStatus(threadstatus_t::OSUploading, oname,
                     current_file_size > ng_chunk_size
                     ? Optional<double>(0) : Optional<double>());

      //
      // If the mtime did not change then we do not bother going
      // through the file data.
      //
      if (child_unchanged) {
        MTrace(t_up, trace::Debug, "   File is known and contents unchanged");
      } else {
        MTrace(t_up, trace::Debug, "   File contents are new or changed");

        // The cobj is the actual file data. The hash list is the
        // list of all chunk hashes.
        //
        // Simply open the file, read chunks one at a time.
        HANDLE fh = CreateFile(utf8_to_utf16(objinfo.abspath).c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL
                               | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, 0);
        if (fh == INVALID_HANDLE_VALUE) {
          MTrace(t_up, trace::Warn, "Unable to open file \"" << objinfo.abspath
                 << "\" - skipping");
          continue;
        }
        ON_BLOCK_EXIT(CloseHandle, fh);

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
          DWORD rd;
          size_t ofs = 2;
          if (!ReadFile(fh, &chunk[ofs], uint32_t(chunk.size() - ofs), &rd, 0)) {
            MTrace(t_up, trace::Warn, "Cannot read from file \"" << objinfo.abspath
                   << "\" - skipping");
            goto next_object;
          }
          MTrace(t_up, trace::Debug, "   Read " << rd << " bytes of chunk data");
          ofs += rd;

          // Are we done?
          if (!rd)
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
          if (current_file_size > ng_chunk_size)
            proc.setStatus(threadstatus_t::OSUploading, oname,
                           std::min(1., 1. * treesize / current_file_size));


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

      MTrace(t_up, trace::Debug, "  Skipping unknown object type: "
             << std::hex << objinfo.fi.dwFileAttributes);
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

  next_object:;
  } while (FindNextFile(dirh, &dirbuf));

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
    ser(meta, uint8_t(0x12)); // 0x12 => windows directory
    // name
    ser(meta, std::string((*ci)->name));
    // owner user
    ser(meta, (*ci)->meta_owner);
    // file attributes
    ser(meta, (*ci)->meta_fattr);
    // SDDL
    ser(meta, (*ci)->meta_sddl);
    // mtime and btime
    ser(meta, uint64_t(Time((*ci)->cobj.m_id.write_time).to_timet()));
    ser(meta, uint64_t(Time((*ci)->cobj.m_id.creation_time).to_timet()));

    // Add LoR and LoM data
    dirobj_lorm.push_back(dirobj_t((*ci)->cobj.m_hash, meta,
                                   (*ci)->cobj.m_treesize));
  }

  // All objects in directory updated.
  MTrace(t_up, trace::Debug, "Done with all entries under " << path
         << " - will encode directory object(s)");

  //
  // We will build a new multiref and treesize. Store the old
  // multiref.
  //
  cobj.m_treesize = 0;
  objseq_t old_multiref;
  old_multiref.swap(cobj.m_hash);

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
    // Fine, we now have an object to upload.
    //
    cobj.m_treesize += treesize;
    cobj.m_hash.push_back(sha256::hash(object));
    //
    // First, we see if the computed hash is identical to the hash in
    // the old multiref. If it is, well there is no point in asking
    // the server then.
    //
    if (old_multiref.size() >= cobj.m_hash.size()
        && cobj.m_hash.back() == old_multiref[cobj.m_hash.size() - 1]) {
      MTrace(t_up, trace::Debug, " hash unchanged - done.");
    } else {
      if (!proc.refUpload().testObject(proc.refConn(), cobj.m_hash.back())) {
        MTrace(t_up, trace::Debug, " upload necessary");
        proc.refUpload().uploadObject(proc.refConn(), object);
      } else {
        MTrace(t_up, trace::Debug, " object already on servers");
      }
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
