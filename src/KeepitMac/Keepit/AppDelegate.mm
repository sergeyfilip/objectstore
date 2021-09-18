//
//  AppDelegate.m
//  Keepit
//
//  Created by vg on 4/22/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "AppDelegate.h"
#import "MASPreferencesWindowController.h"
#import "AccountPreferencesViewController.h"
#import "BackupPreferencesViewController.h"
#import "StatusPreferencesViewController.h"
#import "AboutViewController.h"
#import <HockeySDK/HockeySDK.h>
#import <Sparkle/Sparkle.h>
#import <Sparkle/SUUpdater.h>
#import "Reachability.h"

#include "config.hh"
#include "Constants.h"
#include "SettingsManager.h"

#import "BackupManager.hh"
#import "BackupManagerListener.h"
#import <ServiceManagement/ServiceManagement.h>
#import "ScriptManager.h"

static std::string certificate = [[[NSBundle mainBundle] pathForResource:@"ca-bundle" ofType:@"crt"] cStringUsingEncoding:NSUTF8StringEncoding];

//extern std::string g_getVersion() { return "0.1-manual"; }

extern const char *g_getCAFile()
{
  return certificate.c_str();
}

extern const char *g_getCAPath() { return 0; }

@interface AppDelegate () <BITCrashReportManagerDelegate, BackupManagerDelegate>
@property (nonatomic, retain) BackupManagerListenerObjC* backupManagerListener;
@property (nonatomic, retain) NSWindowController *preferencesWindowController;
@property (nonatomic, retain) NSTimer *backupScheduleTimer;
@property (nonatomic, readwrite) BOOL internetAvailable;

- (void)prepareBackupManager;
@end

@implementation AppDelegate

@synthesize backupManagerListener = _backupManagerListener;
@synthesize preferencesWindowController = _preferencesWindowController;
@synthesize backupScheduleTimer = _backupScheduleTimer;
@synthesize internetAvailable = _internetAvailable;

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:[BITSystemProfile sharedSystemProfile] name:NSApplicationWillResignActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:[BITSystemProfile sharedSystemProfile] name:NSApplicationWillTerminateNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:[BITSystemProfile sharedSystemProfile] name:NSApplicationDidBecomeActiveNotification object:nil];
    self.preferencesWindowController = nil;
  [self.backupScheduleTimer invalidate];
  self.backupScheduleTimer = nil;
  [self.backupManagerListener unsubscribe];
  self.backupManagerListener = nil;
  delete _backupManager;
  delete _logstream;
  delete _logfile;
  delete _conf;
  [super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(windowWillClose:)
                                               name:NSWindowWillCloseNotification
                                             object:nil];
#ifndef DEBUG
    [[SettingsManager sharedInstance] setShouldStartAppAtLogin:NO];
    [self createRelauchPlist];
#endif
    Reachability * reach = [Reachability reachabilityWithHostname:[SettingsManager sharedInstance].apiHost];
    
    if (reach) {
        reach.reachableBlock = ^(Reachability * reachability)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self checkInternetConnection:reachability];
            });
        };
        
        reach.unreachableBlock = ^(Reachability * reachability)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self checkInternetConnection:reachability];
            });
        };
        
        [reach startNotifier];
    }
  
  [self prepareBackupManager];
  
  // Install icon into the menu bar
  [StatusManager sharedInstance];
  
  self.backupManagerListener = [BackupManagerListenerObjC listenerWithDelegate:self];
  [self.backupManagerListener subscribe];
  
  if (BackupManager::getInst().isRegistered()) {
      if (BackupManager::getInst().checkRegistration()) {
          BackupManager::getInst().cmdMonitor();
      }else {
          if (BackupManager::getInst().doReRegisterUser()) BackupManager::getInst().cmdMonitor();
      }
  } else {
    [StatusManager sharedInstance].statusText = @"Not logged in";
    [self openPreferences];
  }
  
  [[BITHockeyManager sharedHockeyManager] configureWithIdentifier:@"87416b127d824b6bfaa1474304426cad" companyName:@"Keepit" crashReportManagerDelegate:self];
  [[BITHockeyManager sharedHockeyManager] startManager];
  
  [[SUUpdater sharedUpdater] setDelegate:self];
  
  NSString *feedURL = [NSString stringWithFormat:@"%@://%@/files/download/updates/desktop/mac/keepitmac.xml", KEEPIT_URL_SCHEME, [SettingsManager sharedInstance].apiHost];
  [[SUUpdater sharedUpdater] setFeedURL:[NSURL URLWithString:feedURL]];

  NSNotificationCenter *dnc = [NSNotificationCenter defaultCenter];
  BITSystemProfile *bsp = [BITSystemProfile sharedSystemProfile];
  [dnc addObserver:bsp selector:@selector(startUsage) name:NSApplicationDidBecomeActiveNotification object:nil];
  [dnc addObserver:bsp selector:@selector(stopUsage) name:NSApplicationWillTerminateNotification object:nil];
  [dnc addObserver:bsp selector:@selector(stopUsage) name:NSApplicationWillResignActiveNotification object:nil];
    
    
    if ([[NSUserDefaults standardUserDefaults] objectForKey:@"SUAllowsAutomaticUpdates"] && [[NSUserDefaults standardUserDefaults] boolForKey:@"SUAllowsAutomaticUpdates"] == YES) {
        [[SUUpdater sharedUpdater] checkForUpdatesInBackground];
    }
    
  [[SUUpdater sharedUpdater] setAutomaticallyChecksForUpdates:YES];
    [[SUUpdater sharedUpdater] setAutomaticallyDownloadsUpdates:YES];
    [[SUUpdater sharedUpdater] setUpdateCheckInterval:60*15];
    

  [[BITHockeyManager sharedHockeyManager] setLoggingEnabled:YES];
  
}

-(BOOL)appIsPresentInLoginItems
{
    NSString *bundleID = [[NSBundle mainBundle] bundleIdentifier];
    NSArray * jobDicts = nil;
    jobDicts = (NSArray *)SMCopyAllJobDictionaries( kSMDomainUserLaunchd );
    // Note: Sandbox issue when using SMJobCopyDictionary()
    
    if ( (jobDicts != nil) && [jobDicts count] > 0 ) {
        
        BOOL bOnDemand = NO;
        
        for ( NSDictionary * job in jobDicts ) {
            
            if ( [bundleID isEqualToString:[job objectForKey:@"Label"]] ) {
//                bOnDemand = [[job objectForKey:@"OnDemand"] boolValue];
                bOnDemand = YES;
                break;
            }
        }
        
        CFRelease((CFDictionaryRef)jobDicts); jobDicts = nil;
        return bOnDemand;
        
    } 
    return NO;
}

- (void)createRelauchPlist
{
    
    NSMutableDictionary* plist = [NSMutableDictionary dictionary];
    NSString* bundlId = [[NSBundle mainBundle] bundleIdentifier];
    
    if (plist) {
        
        //programArguments
        NSString* argKey = @"ProgramArguments";
        NSMutableArray* arguments = [NSMutableArray array];
        
        if (arguments) {
            //app launch path
            NSString* appPath = [[NSBundle mainBundle] bundlePath];
            NSString* appName = [[[NSBundle mainBundle] infoDictionary] valueForKey:@"CFBundleName"];
            NSString* appFullPath = [NSString stringWithFormat:@"%@/Contents/MacOS/%@", appPath, appName];
            
            [arguments addObject:appFullPath];
            [arguments addObject:@"-k"];
            [arguments addObject:@"start"];
        }
        
        if (arguments.count) [plist setObject:arguments forKey:argKey];
        
        
        //Label
        NSString* labelKey = @"Label";
        
        if (bundlId.length) [plist setObject:bundlId forKey:labelKey];
        
        //OnDemand
        NSString* demandKey = @"OnDemand";
        [plist setObject:[NSNumber numberWithBool:NO] forKey:demandKey];
        
        
        //KeepAlive
        NSString* aliveKey = @"KeepAlive";
        NSMutableDictionary* aliveParams = [NSMutableDictionary dictionary];
        
        NSString* successfulKey = @"SuccessfulExit";
        [aliveParams setObject:[NSNumber numberWithBool:NO] forKey:successfulKey];
        
        /*NSString* otherJobsKey = @"OtherJobEnabled";
        NSMutableDictionary* otherJobs = [NSMutableDictionary dictionary];
        [otherJobs setObject:[NSNumber numberWithBool:NO] forKey:bundlId];
        
        if (otherJobs.allKeys.count) [aliveParams setObject:otherJobs forKey:otherJobsKey];*/
        
        if (aliveParams.allKeys.count) [plist setObject:aliveParams forKey:aliveKey];
        
        //RunAtLoad
        NSString* runKey = @"RunAtLoad";
        [plist setObject:[NSNumber numberWithBool:NO] forKey:runKey];
        
//        //Disabled
//        NSString* disabledKey = @"Disabled";
//        [plist setObject:[NSNumber numberWithBool:NO] forKey:disabledKey];
        
        //StartInterval
        NSString* startKey = @"StartInterval";
        [plist setObject:[NSNumber numberWithInt:5] forKey:startKey];
        
//        //StartOnMount
//        NSString* startOnMountKey = @"StartOnMount";
//        [plist setObject:[NSNumber numberWithBool:NO] forKey:startOnMountKey];
    }
    
    //try to write plist to the path "~/Library/LaunchDaemons"
    
    if (plist.allKeys.count && bundlId.length) {
        
        NSString* userDirr = NSHomeDirectory();
        
        NSString* path = [NSString stringWithFormat:@"%@/Library/LaunchAgents", userDirr];
        
        [self prepareRelaunchFolderWithPath:path];
        
        NSString* plistPath = [NSString stringWithFormat:@"%@/%@.plist", path, bundlId];
        
        if (![plist writeToFile:plistPath atomically:YES]) DLog(@"file wasn't save");
    }
    
    [self checkForRunningApplication];
}

-(void)checkForRunningApplication
{
    
    if (![self appIsPresentInLoginItems]) {
        [ScriptManager runLoadLaunchdScript];
        [[NSApplication sharedApplication] terminate:self];
    }
}

- (void)prepareRelaunchFolderWithPath:(NSString* )path
{
    NSFileManager* defaultManager = [NSFileManager defaultManager];
    
    if (![defaultManager fileExistsAtPath:path]) {
        NSError* error = nil;
        [defaultManager createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:&error];
        if (error) DLog(@"FolderCreated");
    }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
  return NSTerminateNow;
}

- (void)openPreferences
{
  if (self.preferencesWindowController == nil)
  {
    NSViewController *accountViewController = [[AccountPreferencesViewController alloc] init];
    NSViewController *backupViewController = [[BackupPreferencesViewController alloc] init];
    NSViewController *statusViewController = [[StatusPreferencesViewController alloc] init];
    NSViewController *aboutViewController = [[AboutViewController alloc] init];
    NSArray *controllers = [NSArray arrayWithObjects:accountViewController, backupViewController, statusViewController, aboutViewController, nil];
      [accountViewController release];
      [backupViewController release];
      [statusViewController release];
      [aboutViewController release];
    
    // To add a flexible space between General and Advanced preference panes insert [NSNull null]:
    //     NSArray *controllers = [[NSArray alloc] initWithObjects:generalViewController, [NSNull null], advancedViewController, nil];
    
    NSString *title = NSLocalizedString(@"Keepit", @"Common title for Preferences window");
    NSWindowController* windowController = [[MASPreferencesWindowController alloc] initWithViewControllers:controllers title:title];
      [self setPreferencesWindowController:windowController];
      [windowController release];
  }
  
  [self.preferencesWindowController showWindow:nil];
  [NSApp activateIgnoringOtherApps:YES];
    
    if (BackupManager::getInst().isRegistered()) [[NSNotificationCenter defaultCenter] postNotificationName:@"EnableTabs" object:[NSNumber numberWithBool:YES]];
    else [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"MASPreferences Selected Identifier View"];
    
}

- (void)windowWillClose:(NSNotification *)notification
{
  DLog(@"windowWillClose");
  //    DLog(@"%@", notification);
  //self.preferencesWindowController = nil;
}

- (NSWindow* )currentWindow
{
    if (self.preferencesWindowController) return self.preferencesWindowController.window;
    
    return nil;
}

- (void)prepareBackupManager
{
  try {
    _conf = new Config();

    NSString* logFilePath = [[SettingsManager sharedInstance].configFolderPath stringByAppendingPathComponent:@"keepit.log"];
    _logfile = new std::ofstream([logFilePath cStringUsingEncoding:NSUTF8StringEncoding], std::ios_base::out | std::ios_base::app);
    _logstream = new trace::StreamDestination(std::cerr/*m_logfile*/);
    
#ifdef DEBUG
    trace::Path::addDestination(trace::Debug, "/upload", *_logstream);
    trace::Path::addDestination(trace::Debug, "/upload/cdp", *_logstream);
#endif
    trace::Path::addDestination(trace::Warn, "*", *_logstream);
    trace::Path::addDestination(trace::Info, "*", *_logstream);

    _backupManager = new BackupManager(*_conf); // Instantiate singleton

    [self loadBackupFolders];
    BackupManager::getInst().start();
    
    //FIXME implement proper scheduler in Upload Manager
    self.backupScheduleTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(oneSecondTimer) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:self.backupScheduleTimer
                                 forMode:NSEventTrackingRunLoopMode];
  } catch (error &e) {
    std::cerr << e.toString() << std::endl;
  }
}

- (void)oneSecondTimer
{
  BackupManager::getInst().oneSecondTimer();
}

- (void)updateConfigApiHost
{
    BackupManager::getInst().updateHost();
}

- (void)loadBackupFolders
{
  NSDictionary *includedItems = [[SettingsManager sharedInstance] getBackupFolders];
  BackupManager::includeFolders_t backupFolders;
  BackupManager::includeFolders_t& backupFoldersRef = backupFolders;
  
  // prepare map with included/excluded folders
  [includedItems enumerateKeysAndObjectsUsingBlock: ^(id includePath, id arrayOfExcludePaths, BOOL *stop) {
    BackupManager::excludeFolders_t& excludeFolders = backupFoldersRef[[includePath cStringUsingEncoding:NSUTF8StringEncoding]];
    [arrayOfExcludePaths enumerateObjectsUsingBlock: ^(id excludePath, NSUInteger index, BOOL *stop) {
      excludeFolders.insert([excludePath cStringUsingEncoding:NSUTF8StringEncoding]);
    }];
  }];
  BackupManager::getInst().setBackupFolders(backupFolders);
}

#pragma mark - ReachabilityNotification
- (void)reachabilityStatusChanged:(NSNotification* )notification
{
    if (notification && [notification object]) {
        id something = [notification object];
        if ([something isKindOfClass:[Reachability class]]) {
            [self checkInternetConnection:(Reachability* )something];
        }
    }
}

- (void)checkInternetConnection:(Reachability* )reach
{
    _internetAvailable = NO;
    if ([reach isReachable]) _internetAvailable = YES;
}

- (BOOL)chectInternetSate
{
    return _internetAvailable;
}

#pragma mark - BITCrashReportManagerDelegate protocol
- (void) showMainApplicationWindow {
  // launch the main app window
  // remember not to automatically show the main window if using NIBs
  if (_preferencesWindowController && _preferencesWindowController.window) {
    [_preferencesWindowController.window makeFirstResponder: nil];
    [_preferencesWindowController.window makeKeyAndOrderFront:nil];
  }
}

#pragma mark - SUUpdaterDelegateInformal protocol

- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SUUpdater *)bundle
{
    return NO;
}

- (void)openWebFiles
{
  NSString* tokenl = [SettingsManager sharedInstance].token;
  NSString* tokenp = [SettingsManager sharedInstance].password;
  NSString* redirecturi = [SettingsManager sharedInstance].device;
  NSString* url;
  if (tokenl && tokenp && redirecturi) {
    // Open web base with our credentials(tokens)
    NSString* autologin = [NSString stringWithFormat:AUTOLOGIN_TEMPLATE, tokenl, tokenp, redirecturi];
    url = [NSString stringWithFormat:@"%@://%@/%@", KEEPIT_URL_SCHEME, [SettingsManager sharedInstance].apiHost, autologin];
  } else {
    // Open default keepit page
    url = [NSString stringWithFormat:@"%@://%@", KEEPIT_URL_SCHEME, [SettingsManager sharedInstance].apiHost];
  }
    NSURL* aUrl = [NSURL URLWithString:[url stringByAddingPercentEscapesUsingEncoding:
                                        NSUTF8StringEncoding]];
    
    if (aUrl) [[NSWorkspace sharedWorkspace] openURL:aUrl];
}

- (void)openWebSupport
{
  NSString* tokenl = [SettingsManager sharedInstance].token;
  NSString* tokenp = [SettingsManager sharedInstance].password;
  NSString* url;
  if (tokenl && tokenp) {
    // Open web base with our credentials(tokens)
    NSString* autologin = [NSString stringWithFormat:AUTOLOGIN_TEMPLATE, tokenl, tokenp, SUPPORT_PAGE];
    url = [NSString stringWithFormat:@"%@://%@/%@", KEEPIT_URL_SCHEME, [SettingsManager sharedInstance].apiHost, autologin];
  } else {
    // Open default keepit page
    url = [NSString stringWithFormat:@"%@://%@/%@", KEEPIT_URL_SCHEME, [SettingsManager sharedInstance].apiHost, SUPPORT_PAGE];
  }
    NSURL* aUrl = [NSURL URLWithString:[url stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding]];
    
    if (aUrl)[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}

@end
