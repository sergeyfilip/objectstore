//
//  BackupPreferencesViewController.m
//  Keepit
//
//  Created by vg on 4/25/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "BackupPreferencesViewController.h"
#import "MASPreferencesViewController.h"
#import "FileBrowserPanelController.h"
#import "Definitions.h"
#import "ImageAndTextCell.h"
#import "StatusManager.h"
#import "AppDelegate.h"

#import "StatusManager.h"
#import "BackupManager.hh"
#import "BackupManagerListener.h"

#import "HumanReadableDataSizeHelper.h"
#import "SettingsManager.h"
#import "BackupOutlineView.h"

@interface PathItem : NSObject {
@private
  NSString *_absolutePath;
  PathItem *_parent;
  NSMutableArray *_childrenList;
  BOOL _leafItem;
}
- (id)initWithParent:(PathItem *)parent andPath:(NSString*) path;
- (NSArray *)children;
- (NSImage *)iconImage;
@end

@interface PathItem ()

@property (nonatomic, retain) NSString *absolutePath;
@property (nonatomic, retain) PathItem *parent;
@property (nonatomic, retain) NSMutableArray *childrenList;
@property (nonatomic, assign) BOOL leafItem;

@end

@implementation PathItem

@synthesize absolutePath = _absolutePath;
@synthesize parent = _parent;
@synthesize childrenList = _childrenList;
@synthesize leafItem = _leafItem;

- (id)initWithParent:(PathItem *)parent andPath:(NSString*)path;
{
  self = [super init];
  if (self) {
    // Initialization code here.
    self.parent = parent;
    self.absolutePath = path;
    self.leafItem = false;
  }
  
  return self;
}

- (void)dealloc
{
    [_childrenList release];
    [_parent release];
    [_absolutePath release];
    [super dealloc];
}

- (NSArray *)children {
  if (self.leafItem) {
    // return if leaf node
    return nil;
  }
    
  if (!self.childrenList) {
    NSDictionary *includedItems = /*[[NSUserDefaults standardUserDefaults] objectForKey:IncludedItemsKey]*/[[SettingsManager sharedInstance] getBackupFolders];
    if (!self.parent) {
      // populate includes
      for (NSString* includePath in [includedItems allKeys]) {
        PathItem* includedItem = [[PathItem alloc] initWithParent:self andPath:includePath];
        if (!self.childrenList) {
            NSMutableArray* childrenList = [[NSMutableArray alloc] initWithCapacity:[[includedItems allKeys] count]];
            [self setChildrenList:childrenList];
            [childrenList release];
        }
        [self.childrenList addObject:includedItem];
          [includedItem release];
      }
    } else {
      // populate excludes as children of includes
      for (NSString* excludePath in [includedItems objectForKey:self.absolutePath]) {
        PathItem* excludedItem = [[PathItem alloc] initWithParent:self andPath:excludePath];
        // Leaf node mark
        excludedItem.leafItem = YES;
        if (!self.childrenList) {
            NSMutableArray* childrenList = [[NSMutableArray alloc] initWithCapacity:[[includedItems objectForKey:self.parent.absolutePath] count]];
            [self setChildrenList:childrenList];
            [childrenList release];
        }
        [self.childrenList addObject:excludedItem];
          [excludedItem release];
      }
    }
  }
  
  return self.childrenList;
}

- (NSImage *)iconImage {
	NSSize size = NSMakeSize(FSBIconWH, FSBIconWH);
	NSString *path = self.absolutePath;
	NSImage *iconImage = [[NSWorkspace sharedWorkspace] iconForFile:path];
	if (!iconImage)
		iconImage = [[NSWorkspace sharedWorkspace] iconForFileType:[path pathExtension]];
	if (!iconImage)
		iconImage = [[NSWorkspace sharedWorkspace] iconForFileType:NSFileTypeForHFSTypeCode(kGenericDocumentIcon)];
	else {
		[iconImage setScalesWhenResized:YES];
		[iconImage setSize:size];
	}
	return iconImage;
}
@end

@interface BackupPreferencesViewController () <MASPreferencesViewController, BackupManagerDelegate, NSOutlineViewDataSource, NSOutlineViewDelegate>

@property (nonatomic, retain) BackupManagerListenerObjC* backupManagerListener;
@property (nonatomic, assign) BOOL hasResizableWidth;
@property (nonatomic, assign) BOOL hasResizableHeight;
@property (nonatomic, assign) BOOL controlsEnabled;
@property (nonatomic, retain) IBOutlet BackupOutlineView *inclExclItemsList;
@property (nonatomic, retain) FileBrowserPanelController *fileBrowserPanelController;
@property (nonatomic, retain) PathItem* rootItem;
@property (nonatomic, retain) IBOutlet NSTableColumn *includesColumn;
@property (nonatomic, retain) IBOutlet NSTableColumn *sizeColumn;
@property (nonatomic, retain) IBOutlet NSTableColumn *excludesColumn;

@property (nonatomic, retain) IBOutlet NSTextField * estimateSizeOfFullBackup;
@property (nonatomic, retain) NSString *statusText;
@property (nonatomic, readwrite) NSUInteger pathesCount;

// Helper routines
- (void)updateGUI;
- (void)refreshFileTable;
@end

@implementation BackupPreferencesViewController

@synthesize backupManagerListener = _backupManagerListener;
@synthesize hasResizableWidth = _hasResizableWidth;
@synthesize hasResizableHeight = _hasResizableHeight;
@synthesize controlsEnabled = _controlsEnabled;
@synthesize inclExclItemsList = _inclExclItemsList;
@synthesize fileBrowserPanelController = _fileBrowserPanelController;
@synthesize rootItem = _rootItem;
@synthesize includesColumn = _includesColumn;
@synthesize sizeColumn = _sizeColumn;
@synthesize excludesColumn = _excludesColumn;

@synthesize estimateSizeOfFullBackup = _estimateSizeOfFullBackup;

@synthesize statusText = _statusText;
@synthesize pathesCount = _pathesCount;

- (id)init
{
  return [self initWithNibName:@"BackupPreferencesView" bundle:nil];
}

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
  self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
  if (self) {
    // Initialization code here.
    self.hasResizableWidth = YES;
    self.hasResizableHeight = YES;
    self.backupManagerListener = [BackupManagerListenerObjC listenerWithDelegate:self];
  }
  
  return self;
}

#pragma mark -
#pragma mark MASPreferencesViewController

- (NSString *)identifier
{
  return @"BackupPreferences";
}

- (NSImage *)toolbarItemImage
{
  return [NSImage imageNamed:NSImageNameFolder];
}

- (NSString *)toolbarItemLabel
{
  return NSLocalizedString(@"Backup", @"Toolbar item name for the Backup preference pane");
}

- (void)viewWillAppear
{
  DLog(@"AccountPreferencesView::viewWillAppear");
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(outlineViewItemWillCollapse:) name:NSOutlineViewItemWillCollapseNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(outlineViewItemWillCollapse:) name:NSOutlineViewSelectionDidChangeNotification object:nil];

    
    NSDictionary* folders = [NSDictionary dictionaryWithDictionary:[[SettingsManager sharedInstance] getBackupFolders]];
    if (folders.allKeys.count != _pathesCount) {
        self.rootItem = nil;
        
        if (BackupManager::getInst().isRegistered() && BackupManager::getInst().isMonitoring()) {
            BackupManager::getInst().cmdMonitor();
        }
    }else {
        if (folders.allKeys.count) _pathesCount = folders.allKeys.count;
    }
    
    
  [self.backupManagerListener subscribe];
  [self updateGUI];
  [self refreshFileTable];
}

- (void)viewDidDisappear
{
  DLog(@"AccountPreferencesView::viewDidDisappear");
  [[NSNotificationCenter defaultCenter] removeObserver:self name:NSOutlineViewItemWillCollapseNotification object:nil];
  [[NSNotificationCenter defaultCenter] removeObserver:self name:NSOutlineViewSelectionDidChangeNotification object:nil];

  [self.backupManagerListener unsubscribe];
}

- (void) awakeFromNib {
  ImageAndTextCell *cell = [[ImageAndTextCell alloc] init];
  [cell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [self.includesColumn setDataCell:cell];
    
  [self.inclExclItemsList setUsesAlternatingRowBackgroundColors:YES];
  [self.inclExclItemsList setAllowsColumnReordering:NO];
  [self.excludesColumn setDataCell:cell];
  [cell release];

  ImageAndTextCell *cell1 = [[ImageAndTextCell alloc] init];
  [cell1 setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [cell1 setAlignment:NSRightTextAlignment];
  [self.sizeColumn setDataCell:cell1];
  [cell1 release];
  
  [self bind:@"statusText" toObject:[StatusManager sharedInstance] withKeyPath:@"statusText" options:nil];
  
  BackupManager::getInst().cmdCalcFileSizes();
  [self refreshFileTable];
}

- (void)dealloc
{
    [_statusText release];
    [_estimateSizeOfFullBackup release];
    [_excludesColumn release];
    [_sizeColumn release];
    [_includesColumn release];
    [_rootItem release];
    [_fileBrowserPanelController release];
    [_inclExclItemsList release];
    [_backupManagerListener release];
    [self unbind:@"statusText"];
    self.backupManagerListener = nil;
    [super dealloc];
}

- (IBAction)settingsBtn:(id)sender
{
  if (self.fileBrowserPanelController == nil) {
    self.fileBrowserPanelController = [[[FileBrowserPanelController alloc] init] autorelease];
  }
  [NSApp beginSheet:self.fileBrowserPanelController.window
     modalForWindow:self.view.window
      modalDelegate:self
     didEndSelector:@selector(didEndSheet:returnCode:contextInfo:)
        contextInfo:nil];
}

- (IBAction)helpBtn:(id)sender
{
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openWebFiles];
}

- (void)didEndSheet:(NSWindow *)sheet returnCode:(NSInteger)returnCode contextInfo:(void *)contextInfo
{
  [sheet orderOut:self];
  self.fileBrowserPanelController = nil;
  
  // Update folder list in backup manager
  [(AppDelegate*)([NSApplication sharedApplication].delegate) loadBackupFolders];
  BackupManager::getInst().cmdCalcFileSizes();

  self.rootItem = nil;
  [self refreshFileTable];
  
  if (BackupManager::getInst().isRegistered() && BackupManager::getInst().isMonitoring()) {
    BackupManager::getInst().cmdMonitor();
  }
}

#pragma mark -
#pragma mark NSOutlineView DataSource and Delegate Methods

- (BOOL)tableView:(NSTableView *)aTableView shouldSelectRow:(NSInteger)rowIndex {
  
  return NO;
}

- (BOOL)outlineView:(NSOutlineView *)outlineView shouldSelectTableColumn:(NSTableColumn *)tableColumn {
  
  return NO;
}

- (BOOL)outlineView:(NSOutlineView *)outlineView shouldSelectItem:(id)item {
  
  return NO;
}

- (NSInteger)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
  if (item == nil) {
    // Return "include" path items count
    if (!self.rootItem) {
      self.rootItem = [[[PathItem alloc] initWithParent:nil andPath:nil] autorelease];
    }
    return [[self.rootItem children] count];
  } else {
    // Return "exclude" path items count
    return [[item children] count];
  }
  return 0;
}

- (id)outlineView:(NSOutlineView *)outlineView child:(NSInteger)index ofItem:(id)item
{
  if (item == nil) {
    // Return "include" path item
    return [[self.rootItem children] objectAtIndex:index];
  } else {
    // Return "exclude" path item
    return [[item children] objectAtIndex:index];
  }
  return nil;
}

- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
  return [[item children] count];
}

- (void)outlineView:(NSOutlineView *)outlineView willDisplayCell:(id)cell forTableColumn:(NSTableColumn *)tableColumn item:(id)item
{
	if (![item isKindOfClass:[PathItem class]]) return;
    
  ImageAndTextCell *pathCell = (ImageAndTextCell *)cell;
    
  [pathCell setImage:nil];
  [pathCell setTextColor:[NSColor blackColor]];
  
  // This is "included" path item
  if (![(PathItem* )item leafItem]) {
    if ([tableColumn isEqual:self.includesColumn]) {
      [pathCell setImage:[(PathItem* )item  iconImage]];
    }
  }

  // This is "excluded" path item
  if ([(PathItem* )item leafItem]) {
    if (tableColumn == self.includesColumn) {
      [pathCell setImage:[(PathItem* )item parent].iconImage];
      [pathCell setTextColor:[NSColor grayColor]];
    }
    if (tableColumn == self.excludesColumn) {
      [pathCell setImage:[(PathItem* )item iconImage]];
      [pathCell setTextColor:[NSColor grayColor]];
    }
  }
}

- (id)outlineView:(NSOutlineView *)outlineView objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item
{	  
  // This is "included" path item
  if (![(PathItem* )item leafItem]) {
    if ([tableColumn isEqual:self.includesColumn]) {
      return [(PathItem* )item absolutePath];
    }
    if ([tableColumn isEqual:self.sizeColumn]) {
        
        NSString* path = [(PathItem* ) item absolutePath];
        uint64_t size = -1;
        if (path) size = BackupManager::getInst().fileSizeForPath([[(PathItem* )item absolutePath] cStringUsingEncoding:NSUTF8StringEncoding]);
      
      if (size == -1) {
        return NSLocalizedString(@"Calculating...", @"");
      } else {
        return [HumanReadableDataSizeHelper humanReadableSizeFromBytes:[NSNumber numberWithUnsignedLongLong:size]
                                                                     useSiPrefixes:YES
                                                                   useSiMultiplier:YES];
      }
    }
    if ([tableColumn isEqual:self.excludesColumn]) {
      NSMutableString *exludedNames = [NSMutableString string];
      for (PathItem *excludeItem in [(PathItem* )item children]) {
        if ([exludedNames length] != 0) {
          [exludedNames appendString:@", "];
        }
        [exludedNames appendString:[excludeItem.absolutePath lastPathComponent]];
      }
      return exludedNames;
    }
  }
  
  // This is "excluded" path item
  if ([(PathItem* )item leafItem]) {
    if (tableColumn == self.includesColumn) {
      return [(PathItem* )item parent].absolutePath;
    }
    if (tableColumn == self.excludesColumn) {
      return [(PathItem* )item absolutePath];
    }
  }
	
	return nil;
}

- (void)outlineViewItemWillCollapse:(NSNotification *)notification
{
//  NSInteger items = [self outlineView:self.inclExclItemsList numberOfChildrenOfItem:[notification.userInfo objectForKey:@"NSObject"]];
//	[BackupOutlineView setCountRows:([self.inclExclItemsList numberOfRows] - items)];
//  [self.inclExclItemsList setNeedsLayout:YES];
}

#pragma mark -

- (void)updateGUI
{
  [[StatusManager sharedInstance] updateState];
}

//BackupManagerListenerObjC
- (void)didCreateSnapshot
{
  DLog(@"didCreateSnapshot");
  [self updateGUI];
}

//BackupManagerListenerObjC
- (void)didFailToCreateSnapshot:(NSString *)explanation
{
  DLog(@"didFailToCreateSnapshot:%@", explanation);
  [self updateGUI];
}

//BackupManagerListenerObjC
- (void)didCalculateFileSize:(NSString *)path
{
//    DLog(@"didCalculateFileSize:%@ =%llu", path, BackupManager::getInst().fileSizeForPath([path cStringUsingEncoding:NSUTF8StringEncoding]));
    dispatch_async(dispatch_get_main_queue(), ^{
      [self refreshFileTable];
    });
}

- (void)refreshFileTable
{
  DLog(@"refresh file table");
  
  [self.inclExclItemsList reloadData];
  if (BackupManager::getInst().isCalculating()) {
    DLog(@"BackupManager::getInst().isCalculating()");
    self.estimateSizeOfFullBackup.stringValue = NSLocalizedString(@"Calculating size of full backup...", @"Calculating size of full backup...");
  } else {
    uint64_t totalSize = 0;
    uint64_t &totalSizeRef = totalSize;
    NSDictionary *includedItems = [[SettingsManager sharedInstance] getBackupFolders];
      
      if (includedItems.allKeys.count) _pathesCount = includedItems.allKeys.count;
    
    [includedItems enumerateKeysAndObjectsUsingBlock: ^(id includePath, id arrayOfExcludePaths, BOOL *stop) {
      uint64_t size = BackupManager::getInst().fileSizeForPath([includePath cStringUsingEncoding:NSUTF8StringEncoding]);
      if (size != -1) {
        totalSizeRef += size;
      }
    }];
      
    NSString *text = NSLocalizedString(@"Estimated size of full backup", @"");
    self.estimateSizeOfFullBackup.stringValue = [NSString stringWithFormat:@"%@: %@",
                            text,
                            [HumanReadableDataSizeHelper humanReadableSizeFromBytes:[NSNumber numberWithUnsignedLongLong:totalSize]
                                                                      useSiPrefixes:YES
                                                                    useSiMultiplier:YES]];
  }
}

@end
