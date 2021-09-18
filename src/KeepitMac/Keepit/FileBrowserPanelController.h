//
//  FileBrowserPanelController.h
//  Keepit
//
//  Created by vg on 4/25/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class DeviceListController;
@interface FileBrowserPanelController : NSWindowController {
  DeviceListController *_deviceListController;
}

- (IBAction)cancelBtn:(id)sender;
- (IBAction)saveBtn:(id)sender;
@end
