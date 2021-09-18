//
//  SettingsManager.m
//  Keepit
//
//  Created by vg on 5/1/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "SettingsManager.h"
#import "Definitions.h"
#import "Constants.h"
#import <ApplicationServices/ApplicationServices.h>
#import <CoreFoundation/CoreFoundation.h>

#define kInitialization		@"initialization"

NSString *const API_HOST_KEY = @"apiHost";
NSString *const EMAIL_KEY = @"email";
NSString *const TOKEN_KEY = @"token";
NSString *const PASSWORD_KEY = @"password";
NSString *const DEVICE_KEY = @"device";
NSString *const DEVICE_TOKENL_KEY = @"dtokenl";
NSString *const DEVICE_TOKENP_KEY = @"dtokenp";
NSString *const CACHE_FILE_KEY = @"cacheFile";
NSString *const LATEST_BACKUP_BEGIN = @"latestBackupBegin";
NSString *const LATEST_BACKUP_END = @"latestBackupEnd";
NSString *const USER_ID = @"user_id";
NSString *const DEVICE_ID = @"device_id";

@interface SettingsManager ()
@property (nonatomic, retain) NSDictionary *backupFoldersSet;
@end

@implementation SettingsManager

@synthesize backupFoldersSet = _backupFoldersSet;

+ (SettingsManager*)sharedInstance
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
  if (self = [super init]) {
    // Custom initialization
    [[NSFileManager defaultManager] createDirectoryAtPath:[self configFolderPath] withIntermediateDirectories:YES attributes:nil error:nil];
    
    NSMutableDictionary* appDefaults = [NSMutableDictionary dictionary];
    [appDefaults setObject:KEEPIT_BASE_URL forKey:API_HOST_KEY];
    [appDefaults setObject:[[self configFolderPath] stringByAppendingPathComponent:@"cache.db"] forKey:CACHE_FILE_KEY];
    [[NSUserDefaults standardUserDefaults] registerDefaults:appDefaults];
    
    if (![[NSUserDefaults standardUserDefaults] boolForKey:kInitialization]) {
      [[NSUserDefaults standardUserDefaults] setObject:KEEPIT_BASE_URL forKey:API_HOST_KEY];

      NSMutableDictionary *newIncludedItems = [NSMutableDictionary dictionary];
      [newIncludedItems setObject:[NSMutableArray array] forKey:NSHomeDirectory()];
      self.backupFoldersSet = newIncludedItems;
    	[[NSUserDefaults standardUserDefaults] setObject:self.backupFoldersSet forKey:IncludedItemsKey];
      
      [[NSUserDefaults standardUserDefaults] setBool:YES forKey:kInitialization];
      [[NSUserDefaults standardUserDefaults] synchronize];
    }
  }
  return self;
}

- (NSString *)configFolderPath
{
  static NSString* configFolder = @".keepit";
  return [NSHomeDirectory() stringByAppendingPathComponent:configFolder];
}

- (void)synchronize
{
  [[NSUserDefaults standardUserDefaults] synchronize];
}

- (NSDictionary*)getBackupFolders
{
  if (!self.backupFoldersSet) {
    
    NSDictionary * dict = [[NSUserDefaults standardUserDefaults] objectForKey:IncludedItemsKey];
    if (dict) {
      
      self.backupFoldersSet = dict;
    } else {
      
      self.backupFoldersSet = [NSDictionary dictionary];
    }
  }
    
    [self checkForUnexisingPahtes];
    
  return self.backupFoldersSet;
}

- (void)checkForUnexisingPahtes {
    
    NSMutableDictionary* checkFolders = [NSMutableDictionary dictionaryWithDictionary:self.backupFoldersSet];
    NSArray* keys = [NSArray arrayWithArray:checkFolders.allKeys];
    
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSWorkspace* sharedWs = [NSWorkspace sharedWorkspace];
    
    BOOL emptyPathesExist = NO;
    for (NSString* path in keys) {
        if ([path isKindOfClass:[NSString class]] && path.length) {
            NSError* error = nil;
            [sharedWs typeOfFile:path error:&error];
            if (![fileManager fileExistsAtPath:path] || error) {
                [checkFolders removeObjectForKey:path];
                emptyPathesExist = YES;
            }else {
                id something = [checkFolders objectForKey:path];
                if ([something isKindOfClass:[NSArray class]] && [(NSArray*) something count]) {
                    NSMutableArray* checkExcludPathes = [NSMutableArray arrayWithArray:(NSArray* )something];
                    NSMutableArray* arr = [NSMutableArray arrayWithArray:checkExcludPathes];
                    
                    for (NSUInteger index = 0; index < arr.count; index++) {
                        NSString* excludPath = [arr objectAtIndex:index];
                        if ([excludPath isKindOfClass:[NSString class]] && excludPath.length) {
                            NSError* error = nil;
                            [sharedWs typeOfFile:path error:&error];
                            if (![fileManager fileExistsAtPath:excludPath] || error) [checkExcludPathes removeObject:excludPath];
                        }
                    }
                    if (checkExcludPathes.count != arr.count) {
                        [checkFolders setObject:checkExcludPathes forKey:path];
                        emptyPathesExist = YES;
                    }
                }
            }
        }
    }
    
    if (emptyPathesExist) {
        [self setBackupFolders:nil];
        [self setBackupFolders:checkFolders];
    }
}

- (void)setBackupFolders:(NSDictionary*)folders
{
  if (!folders) {
    
    self.backupFoldersSet = [NSDictionary dictionary];
  }
  self.backupFoldersSet = folders;
}

- (void)saveBackupFolders
{
    [self checkForUnexisingPahtes];
    
  [[NSUserDefaults standardUserDefaults] setObject:self.backupFoldersSet forKey:IncludedItemsKey];
  [[NSUserDefaults standardUserDefaults] synchronize];
}

// apiHost
- (NSString *)apiHost
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:API_HOST_KEY];
}

//- (void)setApiHost:(NSString *)apiHost
//{
//  [[NSUserDefaults standardUserDefaults] setObject:apiHost forKey:API_HOST_KEY];
//}

// email
- (NSString *)email
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:EMAIL_KEY];
}

- (void)setEmail:(NSString *)email
{
  [[NSUserDefaults standardUserDefaults] setObject:email forKey:EMAIL_KEY];
}

// token
- (NSString *)token
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:TOKEN_KEY];
}

- (void)setToken:(NSString *)token
{
  [[NSUserDefaults standardUserDefaults] setObject:token forKey:TOKEN_KEY];
}

// password
- (NSString *)password
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:PASSWORD_KEY];
}

- (void)setPassword:(NSString *)password
{
  [[NSUserDefaults standardUserDefaults] setObject:password forKey:PASSWORD_KEY];
}

// device
- (NSString *)device
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:DEVICE_KEY];
}

- (void)setDevice:(NSString *)device
{
  [[NSUserDefaults standardUserDefaults] setObject:device forKey:DEVICE_KEY];
}

// token
- (NSString *)deviceTokenL
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:DEVICE_TOKENL_KEY];
}

- (void)setDeviceTokenL:(NSString *)token
{
  [[NSUserDefaults standardUserDefaults] setObject:token forKey:DEVICE_TOKENL_KEY];
}

// password
- (NSString *)deviceTokenP
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:DEVICE_TOKENP_KEY];
}

- (void)setDeviceTokenP:(NSString *)password
{
  [[NSUserDefaults standardUserDefaults] setObject:password forKey:DEVICE_TOKENP_KEY];
}

//userID
- (NSString* )userID
{
    return [[NSUserDefaults standardUserDefaults] objectForKey:USER_ID];
}

- (void)setUserID:(NSString* )userID
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    if (defaults) {
        if (userID) [defaults setObject:userID forKey:USER_ID];
        else [defaults removeObjectForKey:USER_ID];
        
        [defaults synchronize];
    }
}

//deviceID
- (NSString* )deviceID
{
    return [[NSUserDefaults standardUserDefaults] objectForKey:DEVICE_ID];
}

- (void)setDeviceID:(NSString* )deviceID
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    if (defaults) {
        if (deviceID) [defaults setObject:deviceID forKey:DEVICE_ID];
        else [defaults removeObjectForKey:DEVICE_ID];
        [defaults synchronize];
    }
}

// cacheFile
- (NSString *)cacheFile
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:CACHE_FILE_KEY];
}

- (void) setShouldStartAppAtLogin:(BOOL)enabled {
  
  NSString * path = [[NSBundle mainBundle] bundlePath];
  
  if (path) {
    
    OSStatus status;
    CFURLRef URLToToggle = (CFURLRef)[NSURL fileURLWithPath:path];
    LSSharedFileListItemRef existingItem = NULL;
    
    LSSharedFileListRef loginItems = LSSharedFileListCreate(kCFAllocatorDefault, kLSSharedFileListSessionLoginItems, /*options*/ NULL);
    
    UInt32 seed = 0U;
    NSArray *currentLoginItems = (NSArray *)(LSSharedFileListCopySnapshot(loginItems, &seed));
    
    for (id itemObject in currentLoginItems) {
      
      LSSharedFileListItemRef item = (LSSharedFileListItemRef)itemObject;
      
      UInt32 resolutionFlags = kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes;
      CFURLRef URL = NULL;
      OSStatus err = LSSharedFileListItemResolve(item, resolutionFlags, &URL, /*outRef*/ NULL);
      if (err == noErr) {
        Boolean foundIt = CFEqual(URL, URLToToggle);
        CFRelease(URL);
        
        if (foundIt) {
          existingItem = item;
          break;
        }
      }
    }
    
      [currentLoginItems release];
      
    if (enabled && (existingItem == NULL)) {
      
      NSString *displayName = [[NSFileManager defaultManager] displayNameAtPath:path];
      IconRef icon = NULL;
      FSRef ref;
      Boolean gotRef = CFURLGetFSRef(URLToToggle, &ref);
      if (gotRef) {
        status = GetIconRefFromFileInfo(&ref,
                                        /*fileNameLength*/ 0, /*fileName*/ NULL,
                                        kFSCatInfoNone, /*catalogInfo*/ NULL,
                                        kIconServicesNormalUsageFlag,
                                        &icon,
                                        /*outLabel*/ NULL);
        if (status != noErr)
          icon = NULL;
      }
      
      LSSharedFileListInsertItemURL(loginItems, kLSSharedFileListItemBeforeFirst, (CFStringRef)displayName, icon, URLToToggle, /*propertiesToSet*/ NULL, /*propertiesToClear*/ NULL);
    } else if (!enabled && (existingItem != NULL)) {
      
      LSSharedFileListItemRemove(loginItems, existingItem);
    }
  }
}


// latestBackupBegin
- (NSDate *)latestBackupBegin
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:LATEST_BACKUP_BEGIN];
}

- (void)setLatestBackupBegin:(NSDate *)date
{
  [[NSUserDefaults standardUserDefaults] setObject:date forKey:LATEST_BACKUP_BEGIN];
}

// latestBackupEnd
- (NSDate *)latestBackupEnd
{
  return [[NSUserDefaults standardUserDefaults] objectForKey:LATEST_BACKUP_END];
}

- (void)setLatestBackupEnd:(NSDate *)date
{
  [[NSUserDefaults standardUserDefaults] setObject:date forKey:LATEST_BACKUP_END];
}
@end
