//
//  AboutViewController.m
//  Keepit
//
//  Created by vg on 5/2/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "AppDelegate.h"
#import "AboutViewController.h"
#import "MASPreferencesViewController.h"

@interface AboutViewController () <MASPreferencesViewController>

@property(nonatomic) BOOL hasResizableWidth;
@property(nonatomic) BOOL hasResizableHeight;
@property(nonatomic, retain) IBOutlet NSTextField *versionTextField;
@property(nonatomic, retain) IBOutlet NSTextField *copyrightTextField;

- (IBAction)helpPressed:(id)sender;

@end

@implementation AboutViewController

@synthesize hasResizableWidth = _hasResizableWidth;
@synthesize hasResizableHeight = _hasResizableHeight;
@synthesize versionTextField = _versionTextField;
@synthesize copyrightTextField = _copyrightTextField;

- (id)init
{
  return [self initWithNibName:@"AboutView" bundle:nil];
}

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
  self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
  if (self) {
    
    self.hasResizableWidth = NO;
    self.hasResizableHeight = NO;
  }
  
  return self;
}

- (void)dealloc
{
    [_copyrightTextField release];
    [_versionTextField release];
    [super dealloc];
}

#pragma mark -
#pragma mark MASPreferencesViewController

- (void)viewWillAppear {
  
  NSString  * bundleVesion = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleShortVersionString"];
  NSString  * buildNumber = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleVersion"];
  if ([bundleVesion isKindOfClass:[NSString class]] && [buildNumber isKindOfClass:[NSString class]]) {
		NSString * value =  [NSString stringWithFormat:NSLocalizedString(@"Version %@-%@", @""), bundleVesion, buildNumber];
    [self.versionTextField setStringValue:value];
  }
  
  NSDateFormatter * dateFormatter = [[NSDateFormatter alloc ] init];
	[dateFormatter setDateFormat:@"yyyy"];
  NSString * currentYear = [dateFormatter stringFromDate:[NSDate date]];
    [dateFormatter release];
  if ([currentYear isKindOfClass:[NSString class]]) {
  	NSString * value = [NSString stringWithFormat:NSLocalizedString(@"Copyright (c) 2007-%@ Keepit A/S", @""), currentYear];
    [self.copyrightTextField setStringValue:value];
  }
}

- (NSString *)identifier
{
  return @"About";
}

- (NSImage *)toolbarItemImage
{
  return [NSImage imageNamed:NSImageNameInfo];
}

- (NSString *)toolbarItemLabel
{
  return NSLocalizedString(@"About", @"Toolbar item name for the About pane");
}

- (IBAction)helpPressed:(id)sender
{
  AppDelegate* keepitAppDelegate = (AppDelegate*)[NSApplication sharedApplication].delegate;
  [keepitAppDelegate openWebSupport];
}

@end
