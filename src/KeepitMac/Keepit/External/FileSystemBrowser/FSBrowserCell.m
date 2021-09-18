//
//  FSBrowserCell.m
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "FSNode.h"
#import "FSBrowserCell.h"
#import "Definitions.h"

@interface FSBrowserCell (PrivateUtilities)
- (NSDictionary *) fsStringAttributes;
@end

@implementation FSBrowserCell

@synthesize node = _node;
@synthesize iconImage = _iconImage;
@synthesize checkboxCell = _checkboxCell;
@synthesize checkboxRect = _checkboxRect;
@synthesize imageFrame = _imageFrame;
@synthesize minimumSizeNeedToDisplay = _minimumSizeNeedToDisplay;
@synthesize currentColumn = _currentColumn;


- (void)dealloc
{
    [_checkboxCell release];
    [_iconImage release];
    [_node release];
    [super dealloc];
}

- (BOOL) startTrackingAt:(NSPoint)startPoint inView:(NSView *)controlView {
	if (NSPointInRect(startPoint, self.checkboxRect)) {
        NSInteger state = ([self.checkboxCell state] == NSOnState) ? NSOffState:NSOnState;
        
		[self.checkboxCell setState:state];
        [[NSNotificationCenter defaultCenter] postNotificationName:@"SelectedCell" object:[NSNumber numberWithInteger:_currentColumn] userInfo:nil];
		self.node.state = [self.checkboxCell state];

	}
	return [super startTrackingAt:startPoint inView:controlView];
}

- (void) drawWithFrame:(NSRect)cellFrame inView:(NSView *)controlView {
  
  NSRect rect = NSZeroRect;
  NSDivideRect(cellFrame, &rect, &cellFrame, FSBIconWH, NSMinXEdge);
  
  if (!NSEqualRects(self.checkboxRect, rect)) {
  DLog(@"drawWithFrame: %@", self.node.absolutePath);
  self.checkboxRect = rect;
    
    if (!self.checkboxCell) {
      NSButtonCell* btnCell = [[NSButtonCell alloc] init];
        [self setCheckboxCell:btnCell];
        [btnCell release];
      [self.checkboxCell setButtonType:NSSwitchButton];
      [self.checkboxCell setControlSize:NSSmallControlSize];
      [self.checkboxCell setImagePosition:NSImageOnly];
      [self.checkboxCell setAllowsMixedState:YES];
      [self.checkboxCell setState:self.node.state];
      [self.checkboxCell setBackgroundColor:([self isHighlighted] || [self state] ?
                                             [self highlightColorInView:controlView]:[NSColor whiteColor])];
    }
  }
  [self.checkboxCell drawWithFrame:self.checkboxRect inView:controlView];
  [super drawWithFrame:cellFrame inView:controlView];
}

- (id) copyWithZone:(NSZone *)zone {
	DLog(@"copyWithZone");
	FSBrowserCell *result = [super copyWithZone:zone];
	result.node = self.node;
	result.iconImage = self.iconImage;
	return result;
}

- (void) loadCellContents {
	DLog(@"loadCellContents");
	NSString *stringValue = [[NSFileManager defaultManager] displayNameAtPath:self.node.absolutePath];
	NSAttributedString *attrStringValue = [[NSAttributedString alloc] initWithString:stringValue
                                                                        attributes:[self fsStringAttributes]];
	[self setAttributedStringValue:attrStringValue];
    [attrStringValue release];
	self.iconImage = [self.node iconImage];
	[self setEnabled:self.node.isReadable];
	[self setLeaf:!self.node.isDirectory];
}

- (NSSize) cellSizeForBounds:(NSRect)aRect {
  
  if (self.minimumSizeNeedToDisplay.width == 0.0f) {
  
  NSSize theSize = [super cellSizeForBounds:aRect];
  DLog(@"cellSizeForBounds %@ theSize:%@", self.node.absolutePath, NSStringFromSize(theSize));
  NSSize iconSize = self.iconImage ? self.iconImage.size : NSZeroSize;
  DLog(@"cellSizeForBounds %@ iconSize:%@", self.node.absolutePath, NSStringFromSize(iconSize));
  theSize.width += iconSize.width + FSBiconInsetH + FSBIconTextSpace;
  theSize.height = FSBIconWH + FSBiconInsetV * 2.0f;
  DLog(@"cellSizeForBounds %@ returntheSize:%@", self.node.absolutePath, NSStringFromSize(theSize));
  self.minimumSizeNeedToDisplay = theSize;
  return theSize;
  } else {
   
   return self.minimumSizeNeedToDisplay;
   }
}

- (void) drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)controlView {

  static CGRect iconImageFrame;
  
  if (self.iconImage) {
		NSSize imageSize = self.iconImage.size;
		NSRect imageFrame, highlightRect, textFrame;
    
		NSDivideRect(cellFrame, &imageFrame, &textFrame, FSBiconInsetH + FSBIconTextSpace + imageSize.width, NSMinXEdge);
    
    if (iconImageFrame.size.width == 0.0f) {
             
        imageFrame.size = self.iconImage.size;
        imageFrame.origin = cellFrame.origin;
        imageFrame.origin.x += 3;
        imageFrame.origin.y += ceilf((cellFrame.size.height - imageFrame.size.height) / 2.0f);
      
      if ([self isHighlighted] || [self state] != 0) {
        [[self highlightColorInView:controlView] set];
        highlightRect = NSMakeRect(NSMinX(cellFrame), NSMinY(cellFrame),
                                   NSWidth(cellFrame) - NSWidth(textFrame), NSHeight(cellFrame));
        NSRectFill(highlightRect);
      }
      //self.imageFrame = imageFrame;
      iconImageFrame = imageFrame;
      DLog(@"drawInteriorWithFrame absolutPaht: %@  imageFrame:%@", self.node.absolutePath, NSStringFromRect(imageFrame));
    } else {
     
      imageFrame.origin.y += ceilf((cellFrame.size.height - imageFrame.size.height) / 2.0f);
      iconImageFrame.origin.y = imageFrame.origin.y;
    }
    
		[self.iconImage drawInRect:iconImageFrame fromRect:NSZeroRect operation:NSCompositeSourceOver fraction:1.0f respectFlipped:YES hints:nil];
    
		[super drawInteriorWithFrame:textFrame inView:controlView];
	} else
		[super drawInteriorWithFrame:cellFrame inView:controlView];
}

- (NSRect) expansionFrameWithFrame:(NSRect)cellFrame inView:(NSView *)view {
  
	NSRect expansionFrame = [super expansionFrameWithFrame:cellFrame inView:view];
  
	if (!NSIsEmptyRect(expansionFrame)) {
    DLog(@"expansionFrameWithFrame: %@ %@", self.node.absolutePath, NSStringFromRect(expansionFrame));
		NSSize iconSize = self.iconImage ? self.iconImage.size : NSZeroSize;
		expansionFrame.origin.x = expansionFrame.origin.x + iconSize.width + FSBiconInsetH + FSBIconTextSpace;
		expansionFrame.size.width = expansionFrame.size.width - (iconSize.width + FSBIconTextSpace + FSBiconInsetH / 2.0f);
	}
	return expansionFrame;
}

- (void) drawWithExpansionFrame:(NSRect)cellFrame inView:(NSView *)view {
  
	DLog(@"drawWithExpansionFrame");
	[super drawWithExpansionFrame:cellFrame inView:view];
}

- (NSDictionary *) fsStringAttributes {
  
	static NSDictionary *fsStringAttributes = nil;
	if (fsStringAttributes == nil) {
		NSMutableParagraphStyle *ps = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
		[ps setLineBreakMode:NSLineBreakByTruncatingMiddle];
		fsStringAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:[NSFont systemFontOfSize:[NSFont systemFontSize]], NSFontAttributeName, ps, NSParagraphStyleAttributeName, nil];
        [ps release];
    DLog(@"fsStringAttributes %@ %@", self.node.absolutePath, fsStringAttributes);
	}
	return fsStringAttributes;
}

@end
