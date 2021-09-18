//
//  StatusManager.m
//  Keepit
//
//  Created by vg on 4/22/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "StatusManager.h"
#import "AppDelegate.h"

#import "BackupManager.hh"
#import "BackupManagerListener.h"

#import <Sparkle/Sparkle.h>
#import <Sparkle/SUUpdater.h>
#import "ScriptManager.h"

#define STATUS_ITEM_VIEW_WIDTH 24.0f

static NSString * const busyNormIcons[] = {
  @"TrayIcon_Upload00",
  @"TrayIcon_Upload01",
  @"TrayIcon_Upload02",
  @"TrayIcon_Upload03",
  @"TrayIcon_Upload04",
  @"TrayIcon_Upload05",
  @"TrayIcon_Upload06",
  @"TrayIcon_Upload07",
  nil};

static NSString * const busyAltIcons[] = {
  @"TrayIcon_Upload00",
  @"TrayIcon_Upload01",
  @"TrayIcon_Upload02",
  @"TrayIcon_Upload03",
  @"TrayIcon_Upload04",
  @"TrayIcon_Upload05",
  @"TrayIcon_Upload06",
  @"TrayIcon_Upload07",
  nil};

static NSString * const idleNormIcons[] = {@"TrayIcon_Normal", nil};
static NSString * const idleAltIcons[] = {@"TrayIcon_Normal", nil};


static NSString * const pauseNormIcons[] = {@"TrayIcon_Pause", nil};
static NSString * const pauseAltIcons[] = {@"TrayIcon_Pause", nil};

static NSString * const offlineNormIcons[] = {@"TrayIcon_NotLoggedIn", nil};
static NSString * const offlineAltIcons[] = {@"TrayIcon_NotLoggedIn", nil};


@interface StatusManager () <NSMenuDelegate, BackupManagerDelegate>
@property (nonatomic, retain) BackupManagerListenerObjC* backupManagerListener;
@property (nonatomic, retain) NSStatusItem *statusItem;
@property (nonatomic, retain) NSMenu *statusItemMenu;
@property (nonatomic, retain) NSTimer *animationTimer;
@property (nonatomic, retain) NSArray *progressMenuItems;
@property (nonatomic, retain) NSMenuItem *pauseResumeMenuItem;

- (void)showIdle;
- (void)showOffline;
- (void)showPause;
- (void)showBusy;
- (void)updateStatusIcon:(NSTimer*)timer;
- (NSString *)normalizeString:(NSString *)original;
@end

@implementation StatusManager

@synthesize backupManagerListener = _backupManagerListener;
@synthesize statusItem = _statusItem;
@synthesize statusItemMenu = _statusItemMenu;
@synthesize animationTimer = _animationTimer;
@synthesize progressMenuItems = _progressMenuItems;
@synthesize pauseResumeMenuItem = _pauseResumeMenuItem;

@synthesize statusText = _statusText;

+ (StatusManager*)sharedInstance
{
  static dispatch_once_t pred = 0;
  __strong static id _sharedObject = nil;
  dispatch_once(&pred, ^{
    _sharedObject = [[self alloc] init]; // or some other init method
  });
  return _sharedObject;
}

- (id)init
{
  self = [super init];
  if (self != nil) {
    // Install status item into the menu bar
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:STATUS_ITEM_VIEW_WIDTH];
    [self.statusItem setHighlightMode:YES];
    [self showIdle];
    
    self.statusItemMenu = [[[NSMenu alloc] init] autorelease];
    self.statusItemMenu.delegate = self;
    self.statusItemMenu.autoenablesItems = NO;
    self.statusItem.menu = self.statusItemMenu;
    
    NSMenuItem *menuItem1 = [[[NSMenuItem alloc] initWithTitle:@"Idle" action:nil keyEquivalent:@""] autorelease];
    [menuItem1 setEnabled:NO];
    [self.statusItemMenu addItem:menuItem1];
    [menuItem1 bind:@"title" toObject:self withKeyPath:@"statusText" options:nil];
    
    self.pauseResumeMenuItem = [[[NSMenuItem alloc] initWithTitle:@"Pause Backup" action:@selector(pauseResumeAction:) keyEquivalent:@""] autorelease];
    self.pauseResumeMenuItem.target = self;
    [self.pauseResumeMenuItem setEnabled:YES];
    [self.statusItemMenu addItem:self.pauseResumeMenuItem];
    
    [self.statusItemMenu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *progressItem1 = [[NSMenuItem alloc] initWithTitle:@"Idle" action:nil keyEquivalent:@""];
    [progressItem1 setEnabled:NO];
    [self.statusItemMenu addItem:progressItem1];
    [progressItem1 release];
    
    NSMenuItem *progressItem2 = [[NSMenuItem alloc] initWithTitle:@"Idle" action:nil keyEquivalent:@""];
    [progressItem2 setEnabled:NO];
    [self.statusItemMenu addItem:progressItem2];
    [progressItem2 release];
    
    self.progressMenuItems = [NSArray arrayWithObjects:progressItem1, progressItem2, nil];
    
    [self.statusItemMenu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *menuItem2 = [[NSMenuItem alloc] initWithTitle:@"Preferences..." action:@selector(preferencesAction:) keyEquivalent:@""];
    menuItem2.target = self;
    [menuItem2 setEnabled:YES];
    [self.statusItemMenu addItem:menuItem2];
    [menuItem2 release];
    
    NSMenuItem *updateItem = [[NSMenuItem alloc] initWithTitle:@"Check for update..." action:@selector(updateAction:) keyEquivalent:@""];
    updateItem.target = self;
    [updateItem setEnabled:YES];
    [self.statusItemMenu addItem:updateItem];
    [updateItem release];
    
    NSMenuItem *menuItemWebClient = [[NSMenuItem alloc] initWithTitle:@"Keepit Web Client" action:@selector(webClientAction:) keyEquivalent:@""];
    menuItemWebClient.target = self;
    [menuItemWebClient setEnabled:YES];
    [self.statusItemMenu addItem:menuItemWebClient];
    [menuItemWebClient release];
    
    [self.statusItemMenu addItem:[NSMenuItem separatorItem]];
    
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(quitMenuItemAction:) keyEquivalent:@""];
    quitItem.target = self;
    [quitItem setEnabled:YES];
    [self.statusItemMenu addItem:quitItem];
    [quitItem release];
    
    self.backupManagerListener = [BackupManagerListenerObjC listenerWithDelegate:self];
    [self.backupManagerListener subscribe];

    BackupManager::getInst().cmdGetUserDetails();
    [self updateState];
  }
  return self;
}

- (void)dealloc
{
  [self.backupManagerListener unsubscribe];
  self.backupManagerListener = nil;
  [[NSStatusBar systemStatusBar] removeStatusItem:self.statusItem];
  self.statusItem = nil;
  self.statusItemMenu = nil;
  [self.animationTimer invalidate];
  self.animationTimer = nil;
  self.progressMenuItems = nil;
  self.pauseResumeMenuItem = nil;
  [super dealloc];
}

- (void)updateState
{
  self.pauseResumeMenuItem.title = BackupManager::getInst().isMonitoring()?@"Pause Backup":@"Resume Backup";
  [self.pauseResumeMenuItem setHidden:!BackupManager::getInst().isRegistered()];
  
  const std::vector<Upload::threadstatus_t> &progressInfo = BackupManager::getInst().getProgressInfo();
    
  for (int i = 0; i < self.progressMenuItems.count ; i++) {
    NSMenuItem *progressItem = [self.progressMenuItems objectAtIndex:i];
    if (i < progressInfo.size()) {
      NSString* stateText = @"";
      switch (progressInfo[i].state) {
        case Upload::threadstatus_t::OSIdle:
          stateText = @"Idle";
          break;
        
        case Upload::threadstatus_t::OSScanning:
          stateText = @"Scanning:";
              [[NSNotificationCenter defaultCenter] postNotificationName:@"CouldSignOut" object:[NSNumber numberWithBool:NO]];
          break;
        
        case Upload::threadstatus_t::OSUploading:
          stateText = @"Uploading:";
              [[NSNotificationCenter defaultCenter] postNotificationName:@"CouldSignOut" object:[NSNumber numberWithBool:YES]];
          break;

        case Upload::threadstatus_t::OSFinishing:
          stateText = @"Finishing:";
          break;

        default:
          break;
      }
      NSString* title = [NSString stringWithFormat:@"%@ %@", stateText, [NSString stringWithUTF8String:progressInfo[i].object.c_str()]];
      progressItem.title = [self normalizeString:title];
    } else {
      progressItem.title = @"Idle";
    }
  }
  
  // See if anything is running
  bool bIdle = true;
  for (std::vector<Upload::threadstatus_t>::const_iterator i
       = progressInfo.begin(); bIdle && i != progressInfo.end(); ++i)
    if (i->state != Upload::threadstatus_t::OSIdle)
      bIdle = false;

  if (!BackupManager::getInst().isRegistered()) {
    [self showOffline];
  } else if (BackupManager::getInst().isMonitoring()) {
    if (!bIdle) {
      self.statusText = @"Synchronizing";
      [self showBusy];
    } else {
      self.statusText = @"Monitoring";
      [self showIdle];
    }
  } else {
    self.statusText = @"Paused";
    [self showPause];
  }
}

- (void)pauseResumeAction:(id)sender
{
  if (BackupManager::getInst().isMonitoring()) {
    BackupManager::getInst().cmdStopMonitor();
  } else {
    BackupManager::getInst().cmdMonitor();
  }
    [self.pauseResumeMenuItem setEnabled:NO];
}

- (void)webClientAction:(id)sender
{
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openWebFiles];
}

- (void)preferencesAction:(id)sender
{
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openPreferences];
}

- (void)updateAction:(id)sender
{
   [[SUUpdater sharedUpdater] setAutomaticallyDownloadsUpdates:NO];
  [[SUUpdater sharedUpdater] checkForUpdates:nil];
}

- (void)quitMenuItemAction:(id)sender
{
    [ScriptManager runUnloadLaunchdScript];
  [[NSApplication sharedApplication] terminate:self];
}

#pragma mark - NSMenuDelegate

- (void)menuDidClose:(NSMenu *)menu
{
  //    [self setActive:NO];
}

- (void)showBusy
{
  if (animNormImagesHead == &busyNormIcons[0])
    return;
  [self.animationTimer invalidate];
  animNormImagesIt = animNormImagesHead = &busyNormIcons[0];
  animAltImagesIt = animAltImagesHead = &busyAltIcons[0];
  self.animationTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/10.0 target:self selector:@selector(updateStatusIcon:) userInfo:nil repeats:YES];
  [[NSRunLoop currentRunLoop] addTimer:self.animationTimer
                               forMode:NSEventTrackingRunLoopMode];
}

- (void)showIdle
{
  [self.animationTimer invalidate];
  animNormImagesIt = animNormImagesHead = &idleNormIcons[0];
  animAltImagesIt = animAltImagesHead = &idleAltIcons[0];
  [self updateStatusIcon:nil];
}

- (void)showOffline
{
  [self.animationTimer invalidate];
  animNormImagesIt = animNormImagesHead = &offlineNormIcons[0];
  animAltImagesIt = animAltImagesHead = &offlineAltIcons[0];
  [self updateStatusIcon:nil];
}

- (void)showPause
{
  [self.animationTimer invalidate];
  animNormImagesIt = animNormImagesHead = &pauseNormIcons[0];
  animAltImagesIt = animAltImagesHead = &pauseAltIcons[0];
  [self updateStatusIcon:nil];
}

- (void)updateStatusIcon:(NSTimer*)timer
{
  //get the image for the current frame
  if (*animNormImagesIt == nil) {
    animNormImagesIt = animNormImagesHead;
  }
  
  if (*animAltImagesIt == nil) {
    animAltImagesIt = animAltImagesHead;
  }
  
//  self.statusText = *animNormImagesIt;
  
  [self.statusItem setImage:[NSImage imageNamed:*animNormImagesIt++]];
  [self.statusItem setAlternateImage:[NSImage imageNamed:*animAltImagesIt++]];
}

//BackupManagerDelegate
- (void)didStartMonitoring
{
  [self updateState];
    [self.pauseResumeMenuItem setEnabled:YES];
}

- (void)didFailToStartMonitoring:(NSString *)explanation
{
  DLog(@"didFailToStartMonitoring:%@", explanation);
  [self updateState];
    [self.pauseResumeMenuItem setEnabled:YES];
}

// BackupManagerDelegate
- (void)didStopMonitoring
{
  DLog(@"didStopMonitoring");
  [self updateState];
    [self.pauseResumeMenuItem setEnabled:YES];
}

- (void)didFailToStopMonitoring:(NSString *)explanation
{
  DLog(@"didFailToStopMonitoring:%@", explanation);
  [self updateState];
    [self.pauseResumeMenuItem setEnabled:YES];
}

//BackupManagerDelegate
- (void)didGetProgressInfo
{
  [self updateState];
}

//BackupManagerDelegate
- (void)didGetChangeInfo:(NSString *)path
{
  [self updateState];
}

//BackupManagerDelegate
- (void)didStartUpload
{
  [self updateState];
}

- (void)didFailToStartUpload:(NSString *)explanation
{
  [self updateState];
}

- (NSString *)normalizeString:(NSString *)original
{
  int maxLen = 50;
  if ([original length] < maxLen-3) {
    return original;
  }

  // define the range you're interested in
  NSRange stringRange = {0, MIN([original length], maxLen)};

  // adjust the range to include dependent chars
  stringRange = [original rangeOfComposedCharacterSequencesForRange:stringRange];
  
  // Now you can create the short string
  NSString *shortString = [original substringWithRange:stringRange];
  
  return [NSString stringWithFormat:@"%@...", shortString];
}
@end
