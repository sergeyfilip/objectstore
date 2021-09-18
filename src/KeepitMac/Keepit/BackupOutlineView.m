//
//  BackupOutlineView.m
//  Keepit
//
//  Created by Aleksei Antonovich on 6/6/13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "BackupOutlineView.h"

@implementation BackupOutlineView

//@synthesize colors = _colors;
//
//static NSInteger latestIndexOfColor = 0;
static NSInteger countOfIndex = 0;
//
//static NSColor * firstColor = nil;
//static NSColor * secondColor = nil;
//static NSColor * latestColor = nil;;
//
//- (void)drawRow:(NSInteger)row clipRect:(NSRect)clipRect
//{
//  if (!firstColor) {
//    
//    firstColor = [NSColor colorWithSRGBRed:(229.f/255.f) green:(229.f/255.f) blue:(229.f/255.f) alpha:1.0f];
//    secondColor = [NSColor colorWithSRGBRed:(218.f/255.f) green:(226.f/255.f) blue:(227.f/255.f) alpha:1.0f];
//  }
//  
//  if (row == 0) {
//    
//    latestColor = 0;
//    latestIndexOfColor = 0;
//    [self setNeedsDisplay];
//  }
//  countOfIndex = [self numberOfRows];
//  
//  if (!latestColor) {
//    
//    if (row % 2) {
//      latestColor = firstColor;
//    } else {
//      
//      latestColor = secondColor;
//    }
//  } else {
//    
//    if ([self levelForRow:row] == 0) {
//      
//      latestIndexOfColor++;      
//      if (latestIndexOfColor % 2) {
//        
//        latestColor = firstColor;
//      } else {
//        
//        latestColor = secondColor;
//      }
//    }
//  }
//  [latestColor setFill];
//  NSRectFill([self rectOfRow:row]);
//  [super drawRow:row clipRect:clipRect];
//}
//
+(void)setCountRows:(NSInteger)countRows {
  
  countOfIndex = countRows;
}

@end

//@implementation NSColor (ColorChangingFun)
//
//+(NSArray*)controlAlternatingRowBackgroundColors
//{
//  if (!firstColor) {
//    
//    firstColor = [NSColor colorWithSRGBRed:(229.f/255.f) green:(229.f/255.f) blue:(229.f/255.f) alpha:1.0f];
//    secondColor = [NSColor colorWithSRGBRed:(218.f/255.f) green:(226.f/255.f) blue:(227.f/255.f) alpha:1.0f];
//  }
//  if (countOfIndex % 2 == 1) {
//    
//    return [NSArray arrayWithObjects:firstColor, secondColor, nil];
//  } else {
//    
//    return [NSArray arrayWithObjects:secondColor, firstColor, nil];
//  }
//}
//
//@end
