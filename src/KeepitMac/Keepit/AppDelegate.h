//
//  AppDelegate.h
//  Keepit
//
//  Created by vg on 4/22/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "StatusManager.h"

#include "common/trace.hh"
#include <iostream>
#include <fstream>

class BackupManager;
class Config;
@interface AppDelegate : NSObject <NSApplicationDelegate> {
@private
  std::ofstream *_logfile;
  trace::StreamDestination *_logstream;
  BackupManager *_backupManager;
  Config *_conf;
  BackupManagerListenerObjC *_backupManagerListener;
  NSWindowController *_preferencesWindowController;
  NSTimer *_backupScheduleTimer;
    BOOL _internetAvailable;
}

- (void)openPreferences;
- (void)loadBackupFolders;
- (void)openWebFiles;
- (void)openWebSupport;
- (void)updateConfigApiHost;
- (BOOL)chectInternetSate;
- (NSWindow* )currentWindow;
- (void)prepareBackupManager;
@end

//// Auto generated
//inline std::string g_getVersion() { return "0.1-manual"; }
//inline const char *g_getCAFile() { return "ca-bundle.crt"; }
//inline const char *g_getCAPath() { return "/Users/vg/"; }
