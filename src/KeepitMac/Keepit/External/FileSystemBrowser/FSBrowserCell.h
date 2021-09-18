//
//  FSBrowserCell.h
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface FSBrowserCell : NSBrowserCell {
@private
  FSNode *_node;
  NSImage *_iconImage;
  NSButtonCell *_checkboxCell;
  NSRect _checkboxRect;
  NSRect _imageFrame;
  NSSize _minimumSizeNeedToDisplay;
    NSInteger _currentColumn;
}

- (void) loadCellContents;

@property (copy) FSNode *node;
@property (copy) NSImage *iconImage;
@property (retain) NSButtonCell *checkboxCell;
@property (assign) NSRect checkboxRect;
@property (assign) NSRect imageFrame;
@property (assign) NSSize minimumSizeNeedToDisplay;
@property (readwrite) NSInteger currentColumn;

@end
