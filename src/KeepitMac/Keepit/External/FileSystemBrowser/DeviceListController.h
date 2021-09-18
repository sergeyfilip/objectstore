//
//  DeviceListController.h
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface DeviceListView : NSOutlineView
@end

@interface DeviceParent : NSObject {
@private
  NSString *_displayTitle;
  NSMutableArray *_children;
}

@property (copy) NSString *displayTitle;
@property (retain) NSMutableArray *children;
@end

@interface DeviceChild : NSObject {
@private
	NSString *_volumePath;
}
@property (copy, readonly) NSString *displayTitle;
@property (copy) NSString *volumePath;
@end

@interface DeviceListController : NSObject {
@private
  DeviceListView *_devicesOutlineView;
  NSMutableArray *_deviceList;
  DeviceParent *_deviceGroup;
}

@property (retain) IBOutlet DeviceListView *devicesOutlineView;
@property (retain) NSMutableArray *deviceList;
@property (retain) DeviceParent *deviceGroup;

- (void)cleanDelegateAndDataSource;

@end
