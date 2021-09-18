//
//  FSBrowser.h
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface FSBrowser : NSBrowser
@end


@class FSNode;
@class FSBrowserCell;
@interface FSBrowserController : NSArrayController {
@private
  FSBrowser *_browser;
  FSNode *_rootnode;
  CGRect _generalImageFrame;
    NSInteger _selectedColumn;
}

- (void) reloadBrowser;

@property (retain) IBOutlet FSBrowser *browser;
@property (retain) FSNode *rootnode;
@property (assign) CGRect generalImageFrame;
@property (readwrite) NSInteger selectedColumn;

@end
