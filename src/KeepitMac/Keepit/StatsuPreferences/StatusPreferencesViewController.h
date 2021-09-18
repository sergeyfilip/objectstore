//
//  StatusPreferencesViewController.h
//  Keepit
//
//  Created by Aleksei Antonovich on 6/6/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#define kProcentLoaded		@"kProcentLoaded"
#define kSizeLoading			@"kSizeLoading"
#define kAbsolutPath			@"kAbsolutPath"

@interface RoundedTextField : NSTextField

@end


@interface ProcessLoadingView : NSView {
@private
  RoundedTextField *_processLoadingTextField;
  RoundedTextField *_backgroundAndDescriptionTextField;
}

//@property(nonatomic, strong) NSString * absolutPath;
@property(nonatomic, retain) RoundedTextField * processLoadingTextField;
@property(nonatomic, retain) RoundedTextField * backgroundAndDescriptionTextField;
@end


@class BackupManagerListenerObjC;

@interface StatusPreferencesViewController : NSViewController {
@private
  BackupManagerListenerObjC *_backupManagerListener;
  BOOL _hasResizableWidth;
  BOOL _hasResizableHeight;
  
  NSView *_documentView;
  NSTextField *_stateInScrollView;
  NSArray *_processLoadingViews;
  NSScrollView *_scrollView;
  
  NSString *_statusText;
}
@end