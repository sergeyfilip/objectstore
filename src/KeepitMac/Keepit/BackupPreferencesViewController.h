//
//  BackupPreferencesViewController.h
//  Keepit
//
//  Created by vg on 4/25/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class BackupManagerListenerObjC;
@class BackupOutlineView;
@class FileBrowserPanelController;
@class PathItem;

@interface BackupPreferencesViewController : NSViewController {
@private
  BackupManagerListenerObjC *_backupManagerListener;
  BOOL _hasResizableWidth;
  BOOL _hasResizableHeight;
  BOOL _controlsEnabled;
  BackupOutlineView *_inclExclItemsList;
  FileBrowserPanelController *_fileBrowserPanelController;
  PathItem *_rootItem;
  NSTableColumn *_includesColumn;
  NSTableColumn *_sizeColumn;
  NSTableColumn *_excludesColumn;
  
  NSButton *_changeButton;
  NSButton *_backupNowButton;
  NSButton *_cancelButton;
  NSTextField *_estimateSizeOfFullBackup;
  NSString *_statusText;
    
    NSUInteger _pathesCount;
}

- (IBAction)settingsBtn:(id)sender;
- (IBAction)helpBtn:(id)sender;

@end
