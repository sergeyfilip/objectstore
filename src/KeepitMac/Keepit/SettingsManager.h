//
//  SettingsManager.h
//  Keepit
//
//  Created by vg on 5/1/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface SettingsManager : NSObject {
  NSDictionary *_backupFoldersSet;
}

+ (SettingsManager*)sharedInstance;
- (id)init;
- (NSString *)configFolderPath;
- (void)synchronize;

- (void) setShouldStartAppAtLogin:(BOOL)flag;

- (NSDictionary*)getBackupFolders;
- (void)setBackupFolders:(NSDictionary*)folders;
- (void)saveBackupFolders;

@property (nonatomic, readonly) NSString *apiHost;
@property (nonatomic, retain) NSString *email;
@property (nonatomic, retain) NSString *token;
@property (nonatomic, retain) NSString *password;
@property (nonatomic, retain) NSString *device;
@property (nonatomic, retain) NSString *deviceTokenL;
@property (nonatomic, retain) NSString *deviceTokenP;
@property (nonatomic, readonly) NSString *cacheFile;

@property (nonatomic, retain) NSString* userID;
@property (nonatomic, retain) NSString* deviceID;

@property (nonatomic,retain) NSDate *latestBackupBegin;
@property (nonatomic, retain) NSDate *latestBackupEnd;

@end
