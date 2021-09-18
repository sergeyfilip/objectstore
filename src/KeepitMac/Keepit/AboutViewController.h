//
//  AboutViewController.h
//  Keepit
//
//  Created by vg on 5/2/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface AboutViewController : NSViewController {
  BOOL _hasResizableWidth;
  BOOL _hasResizableHeight;
  NSTextField *_versionTextField;
  NSTextField *_copyrightTextField;
}

@end
