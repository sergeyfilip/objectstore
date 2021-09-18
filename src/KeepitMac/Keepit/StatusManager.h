//
//  StatusManager.h
//  Keepit
//
//  Created by vg on 4/22/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Foundation/Foundation.h>

@class BackupManagerListenerObjC;
@interface StatusManager : NSObject {
@private
  NSString * const __strong * animNormImagesHead;
  NSString * const __strong * animNormImagesIt;
  
  NSString * const __strong * animAltImagesHead;
  NSString * const __strong * animAltImagesIt;
  
  BackupManagerListenerObjC *_backupManagerListener;
  NSStatusItem *_statusItem;
  NSMenu *_statusItemMenu;
  NSTimer *_animationTimer;
  NSArray *_progressMenuItems;
  NSMenuItem *_pauseResumeMenuItem;
  
  NSString *_statusText;
}
@property (nonatomic, retain) NSString *statusText;

+ (StatusManager*)sharedInstance;
- (void)updateState;
@end
