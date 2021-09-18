//
//  StatusPreferencesViewController.m
//  Keepit
//
//  Created by Aleksei Antonovich on 6/6/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "StatusPreferencesViewController.h"
#import "MASPreferencesViewController.h"
#import "Definitions.h"
#import "StatusManager.h"
#import "AppDelegate.h"
#import <QuartzCore/QuartzCore.h>
#import "BackupManager.hh"
#import "BackupManagerListener.h"

#define kSizeBetweenCells			20.f
#define kHeightCell						20.f
#define kHeightWorkInProgress	40.f


@implementation RoundedTextField

- (id)initWithFrame:(NSRect)frame
{
  self = [super initWithFrame:frame];
  if (self) {
    self.wantsLayer = YES;
    self.layer.frame = self.frame;
  }
  
  return self;
}
- (void)drawRect:(NSRect)dirtyRect {
  
  [NSGraphicsContext saveGraphicsState];
  NSRect frame = NSMakeRect(0.0f, 0.0f, [self bounds].size.width, [self bounds].size.height);
  [[NSBezierPath bezierPathWithRoundedRect:frame xRadius:7 yRadius:7] addClip];
  [super drawRect:dirtyRect];
  [NSGraphicsContext restoreGraphicsState];
}

@end

@interface ProcessLoadingView ()

@end

@implementation ProcessLoadingView

@synthesize processLoadingTextField = _processLoadingTextField;
@synthesize backgroundAndDescriptionTextField = _backgroundAndDescriptionTextField;

- (void)dealloc
{
  self.processLoadingTextField = nil;
  self.backgroundAndDescriptionTextField = nil;
  [super dealloc];
}

@end


@interface StatusPreferencesViewController () <MASPreferencesViewController, BackupManagerDelegate>

@property (nonatomic, retain) BackupManagerListenerObjC* backupManagerListener;
@property (nonatomic, assign) BOOL hasResizableWidth;
@property (nonatomic, assign) BOOL hasResizableHeight;

@property (nonatomic, retain) NSView * documentView;
@property (nonatomic, retain) NSTextField * stateInScrollView;
@property (nonatomic, retain) NSArray * processLoadingViews;
@property (nonatomic, retain) IBOutlet NSScrollView * scrollView;

@property (nonatomic, retain) NSString *statusText;

- (IBAction)helpPressed:(id)sender;
- (void)updateGUI;
@end

@implementation StatusPreferencesViewController

@synthesize backupManagerListener = _backupManagerListener;
@synthesize hasResizableWidth = _hasResizableWidth;
@synthesize hasResizableHeight = _hasResizableHeight;

@synthesize documentView = _documentView;
@synthesize stateInScrollView = _stateInScrollView;
@synthesize processLoadingViews = _processLoadingViews;
@synthesize scrollView = _scrollView;

@synthesize statusText = _statusText;

- (id)init
{
  return [self initWithNibName:@"StatusPreferencesViewController" bundle:nil];
}

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
  self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
  if (self) {
    self.hasResizableWidth = NO;
    self.hasResizableHeight = NO;
    self.backupManagerListener = [BackupManagerListenerObjC listenerWithDelegate:self];
  }
  
  return self;
}

- (void)dealloc
{
  [self.backupManagerListener unsubscribe];
  self.backupManagerListener = nil;
  [super dealloc];
}

#pragma mark -
#pragma mark MASPreferencesViewController

- (NSString *)identifier
{
  return @"StatusPreferences";
}

- (NSImage *)toolbarItemImage
{
  return [NSImage imageNamed:NSImageNameBonjour];
}

- (NSString *)toolbarItemLabel
{
  return NSLocalizedString(@"Status", @"Toolbar item name for the Status preference pane");
}

- (void)viewWillAppear
{
  DLog(@"viewWillAppear");
  [self bind:@"statusText" toObject:[StatusManager sharedInstance] withKeyPath:@"statusText" options:nil];
  [self.backupManagerListener subscribe];
  
  [self updateGUI];
}

- (void)viewDidDisappear
{
  DLog(@"viewDidDisappear");
  [self unbind:@"statusText"];
  [self.backupManagerListener unsubscribe];
}

- (void)updateGUIWithDictionaries:(NSArray *)dictionaries
{
  if (!self.documentView) {
    
    CGRect frameScrollView = self.scrollView.frame;
    frameScrollView.origin = CGPointMake(0.0f, frameScrollView.size.height);
    self.documentView = [[[NSView alloc] initWithFrame:frameScrollView] autorelease];
    [self.documentView setAutoresizingMask:(NSViewMinXMargin | NSViewMaxXMargin | NSViewMinYMargin | NSViewMaxYMargin | NSViewWidthSizable)];
    [self.scrollView setDocumentView:self.documentView];
  }
  
  if (!self.stateInScrollView) {
    
    self.stateInScrollView = [[[NSTextField alloc ] init] autorelease];
    [self.stateInScrollView setEditable:NO];
    [self.stateInScrollView setBordered:NO];
    [self.stateInScrollView setBackgroundColor:[NSColor clearColor]];
    [self.documentView addSubview:self.stateInScrollView];
  }
  
  if (!dictionaries || [dictionaries count] == 0) {
    
    for (ProcessLoadingView * processLoadingView in self.processLoadingViews) {
      
      [processLoadingView removeFromSuperview];
    }
    self.processLoadingViews = nil;
    self.documentView.frame = self.scrollView.frame;
    
    CGRect frameDocumentView = self.documentView.frame;
    self.stateInScrollView.font = [NSFont systemFontOfSize:23];
    self.stateInScrollView.alignment = NSCenterTextAlignment;
    self.stateInScrollView.stringValue = NSLocalizedString(@"No active uploads", @"");
    self.stateInScrollView.frame = CGRectMake(0.0f, (frameDocumentView.size.height - 32.f) / 2, frameDocumentView.size.width, 32.f);
    [self.scrollView setHasVerticalScroller:NO];
    frameDocumentView.size.height -= 10;
    frameDocumentView.size.width -= 10;
    self.documentView.frame = frameDocumentView;
    
  } else {
    
    CGRect frameScrollVeiw = self.scrollView.frame;
    CGRect frameDocumentView = self.documentView.frame;
    
    CGFloat heightOfDocumentView = (kHeightWorkInProgress + (kSizeBetweenCells + kHeightCell) * dictionaries.count + kSizeBetweenCells);
    
    if ((heightOfDocumentView + 10.f) < frameScrollVeiw.size.height) {
      
      frameDocumentView.size = CGSizeMake(frameDocumentView.size.width, frameScrollVeiw.size.height - 10.0f);
      frameDocumentView.origin = CGPointMake(0.0f, frameScrollVeiw.size.height - frameDocumentView.size.height);
    } else {
      
      frameDocumentView.size = CGSizeMake(frameDocumentView.size.width, heightOfDocumentView);
      frameDocumentView.origin = CGPointMake(0.0f, heightOfDocumentView);//frameScrollVeiw.size.height - frameDocumentView.size.height);
    }
    
    [self.documentView setFrame:frameDocumentView];
    
    [self.scrollView setHasVerticalScroller:YES];
    self.stateInScrollView.font = [NSFont systemFontOfSize:23];
    self.stateInScrollView.stringValue = NSLocalizedString(@"Work in progress", @"");
    self.stateInScrollView.frame = CGRectMake(10.0f, frameDocumentView.size.height - kHeightWorkInProgress, frameDocumentView.size.width, kHeightWorkInProgress);
    self.stateInScrollView.alignment = NSLeftTextAlignment;
    
  	
    NSMutableArray * updatedProcessLoadingViews = [NSMutableArray array];
    
    for (int i = 0; i < dictionaries.count; i++) {
      
      NSDictionary * dict = [dictionaries objectAtIndex:i];
      
      ProcessLoadingView * processLoadingView = nil;
      if (self.processLoadingViews.count > i) {
        
        processLoadingView = (ProcessLoadingView *)[self.processLoadingViews objectAtIndex:i];
      } else {
        
        processLoadingView = [[[ProcessLoadingView alloc] initWithFrame:CGRectMake(0.0f, frameDocumentView.size.height + kSizeBetweenCells - (kHeightWorkInProgress + (kSizeBetweenCells + kHeightCell) * (i + 1)), frameDocumentView.size.width - 10.0f, kHeightCell)] autorelease];
        [processLoadingView setAutoresizingMask:(NSViewWidthSizable)];
        
        processLoadingView.backgroundAndDescriptionTextField = [[[RoundedTextField alloc] initWithFrame:CGRectMake(10.0f, 0.0f, frameDocumentView.size.width - 10.0f - 20.f, kHeightCell)] autorelease];
        processLoadingView.backgroundAndDescriptionTextField.font = [NSFont systemFontOfSize:14];
        [processLoadingView.backgroundAndDescriptionTextField setAutoresizingMask:(NSViewMinXMargin | NSViewMaxXMargin | NSViewMinYMargin | NSViewMaxYMargin | NSViewWidthSizable)];
        [processLoadingView.backgroundAndDescriptionTextField setTextColor:[NSColor blackColor]];
        [processLoadingView.backgroundAndDescriptionTextField setAlignment:NSCenterTextAlignment];
        [processLoadingView.backgroundAndDescriptionTextField setBackgroundColor:[NSColor clearColor]];
        [processLoadingView.backgroundAndDescriptionTextField setDrawsBackground:YES];
        [processLoadingView.backgroundAndDescriptionTextField setEditable:NO];
      	[processLoadingView.backgroundAndDescriptionTextField setBezeled:YES];
        [processLoadingView.backgroundAndDescriptionTextField setBezelStyle:NSTextFieldRoundedBezel];
        [processLoadingView.backgroundAndDescriptionTextField setBordered:YES];
        [processLoadingView addSubview:processLoadingView.backgroundAndDescriptionTextField];
        
        processLoadingView.processLoadingTextField = [[[RoundedTextField alloc] initWithFrame:CGRectMake(10.0f, 0.0f, 0.0f, kHeightCell)] autorelease];
        [processLoadingView.processLoadingTextField setStringValue:[NSString string]];
        [processLoadingView.processLoadingTextField setAutoresizingMask:(NSViewWidthSizable)];
        [processLoadingView.processLoadingTextField setBackgroundColor:[NSColor colorWithCalibratedRed:0.1f green:0.65f blue:0.1f alpha:0.5f]];
        [processLoadingView.processLoadingTextField setDrawsBackground:YES];
        [processLoadingView.processLoadingTextField setEditable:NO];
      	[processLoadingView.processLoadingTextField setBezeled:NO];
        [processLoadingView.processLoadingTextField setBordered:NO];
        [processLoadingView addSubview:processLoadingView.processLoadingTextField];
      }
      
      NSNumber * procentLoaded = [dict objectForKey:kProcentLoaded];
      NSString * sizeLoading = [dict objectForKey:kSizeLoading];
      NSString * absolutPath = [dict objectForKey:kAbsolutPath];
      
      if ([procentLoaded isKindOfClass:[NSNumber class]]
          && [sizeLoading isKindOfClass:[NSString class]]
          && [absolutPath isKindOfClass:[NSString class]]) {
                
        processLoadingView.frame = CGRectMake(0.0f, frameDocumentView.size.height + kSizeBetweenCells - (kHeightWorkInProgress + (kSizeBetweenCells + kHeightCell) * (i + 1)), frameDocumentView.size.width - 10.0f, kHeightCell);
        
        CGRect frameBackgroundProcessLoading = processLoadingView.backgroundAndDescriptionTextField.frame;
        frameBackgroundProcessLoading.size.width = frameBackgroundProcessLoading.size.width * [procentLoaded floatValue];
        processLoadingView.processLoadingTextField.frame = frameBackgroundProcessLoading;
        NSString * descriptionProgress = [NSString stringWithFormat:@"%ld%% %@ - %@", (long)([procentLoaded  floatValue] * 100), NSLocalizedString(@"of", @""), absolutPath];
        processLoadingView.backgroundAndDescriptionTextField.stringValue = descriptionProgress;
      }
      
      if (!processLoadingView.superview) {
        
        [self.documentView addSubview:processLoadingView];
      }
      
      [updatedProcessLoadingViews addObject:processLoadingView];
      //[self.documentView setFrame:frameDocumentView];
    }
    self.processLoadingViews = updatedProcessLoadingViews;
  }
}

- (void)updateGUI
{
  NSMutableArray* threadsStatuses = [NSMutableArray array];
  const std::vector<Upload::threadstatus_t> progressInfo = BackupManager::getInst().getProgressInfo();
  
  bool bIdle = true;
  for (std::vector<Upload::threadstatus_t>::const_iterator i
       = progressInfo.begin(); i != progressInfo.end(); ++i) {
		NSDictionary* threadStatus = [NSDictionary dictionaryWithObjectsAndKeys:
																	[NSString stringWithUTF8String:i->object.c_str()], kAbsolutPath
																	, [NSNumber numberWithDouble:i->object_progress.isSet()?i->object_progress.get():1.0f], kProcentLoaded
																	, @"", kSizeLoading
																	, nil];

    [threadsStatuses addObject:threadStatus];
    if (i->state != Upload::threadstatus_t::OSIdle)
      bIdle = false;
  }
  
  [self updateGUIWithDictionaries:bIdle?nil:threadsStatuses];
}

//BackupManagerDelegate
- (void)didGetProgressInfo
{
  [self updateGUI];
}

// BackupManagerDelegate
- (void)didStopMonitoring
{
  DLog(@"didStopMonitoring");
  [self updateGUI];
}

- (IBAction)helpPressed:(id)sender
{
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openWebFiles];
}
@end
