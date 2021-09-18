//
//  FileBrowserPanelController.m
//  Keepit
//
//  Created by vg on 4/25/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "FileBrowserPanelController.h"
#import "SettingsManager.h"
#import "DeviceListController.h"

@interface FileBrowserPanelController ()

@property(nonatomic, retain) IBOutlet DeviceListController * deviceListController;

@end

@implementation FileBrowserPanelController

@synthesize deviceListController = _deviceListController;

- (void)dealloc {
  
  self.deviceListController = nil;
  [super dealloc];
}

- (id)init
{
  return [super initWithWindowNibName:@"FileBrowserPanel"];
}

- (id)initWithWindow:(NSWindow *)window
{
  self = [super initWithWindow:window];
  if (self) {
  }
  
  return self;
}

- (void)windowDidLoad
{
  [super windowDidLoad];
}

- (IBAction)cancelBtn:(id)sender
{
  [self.deviceListController cleanDelegateAndDataSource];
  self.deviceListController = nil;
    [[SettingsManager sharedInstance] setBackupFolders:nil];
  [NSApp endSheet:self.window];
}

- (IBAction)saveBtn:(id)sender
{
  [self.deviceListController cleanDelegateAndDataSource];
  self.deviceListController = nil;
  [[SettingsManager sharedInstance] saveBackupFolders];
  [NSApp endSheet:self.window];
}
@end
