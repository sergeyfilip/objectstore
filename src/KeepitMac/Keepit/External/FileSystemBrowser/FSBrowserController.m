//
//  FSBrowser.m
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import "FSBrowserController.h"
#import "FSNode.h"
#import "Definitions.h"
#import "FSBrowserCell.h"

@implementation FSBrowser

- (BOOL) canBecomeKeyView {
	return YES;
}

- (void) keyUp:(NSEvent *)theEvent {
  DLog(@"keyUp");
	if ([@" " isEqualToString:[theEvent characters]]) {
		FSBrowserCell *cell = [self selectedCell];
		switch (cell.node.state) {
			case NSOffState:
			case NSOnState:
				cell.node.state = !cell.node.state;
				break;
			default:
				cell.node.state = NSOnState;
				break;
		}
		[self setNeedsDisplay];
	} else
		[super keyUp:theEvent];
}

@end

@implementation FSBrowserController

@synthesize browser = _browser;
@synthesize rootnode = _rootnode;
@synthesize generalImageFrame = _generalImageFrame;
@synthesize selectedColumn = _selectedColumn;

- (void) awakeFromNib {
	[[NSUserDefaults standardUserDefaults] addObserver:self forKeyPath:FSBShowInvisibleFlesKey
                                               options:0 context:@"Preferences"];
	[[NSUserDefaults standardUserDefaults] addObserver:self forKeyPath:FSBTreatPackagesAsDirectoriesKey
                                               options:0 context:@"Preferences"];
	[[NSUserDefaults standardUserDefaults] addObserver:self forKeyPath:FSBLastSelectedVolumeKey
                                               options:0 context:@"Preferences"];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillTerminate:)
                                                 name:NSApplicationWillTerminateNotification object:NSApp];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(redrawBrowser:)name:FSBRedrawBrowserNotification object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(SelectedCell:) name:@"SelectedCell" object:nil];
    
	[self.browser setCellClass:[FSBrowserCell class]];
	[self.browser setMaxVisibleColumns:FSBMaxVisibleColums];
	[self.browser setMinColumnWidth:NSWidth(self.browser.bounds) / (CGFloat)FSBMaxVisibleColums];
    
	[self reloadBrowser];
	NSString *oldPath = [[NSUserDefaults standardUserDefaults] objectForKey:FSBLastBrowserPathKey];
	if (oldPath) {
		[self.browser setPath:oldPath];
  }
}

- (void) applicationWillTerminate:(NSNotification *)aNotification {
	[[NSUserDefaults standardUserDefaults] setObject:self.browser.path forKey:FSBLastBrowserPathKey];
}
- (void) redrawBrowser:(NSNotification *)aNotification {
    [self updateFileBrowserSelections];
	[self.browser setNeedsDisplay];
  DLog(@"redrawBrowser");
}

- (void) dealloc {
    DLog(@"FSBrowserController::dealloc");
    [[NSNotificationCenter defaultCenter] removeObserver:self name:@"SelectedCell" object:nil];
	[[NSUserDefaults standardUserDefaults] removeObserver:self forKeyPath:FSBShowInvisibleFlesKey];
    [[NSUserDefaults standardUserDefaults] removeObserver:self forKeyPath:FSBTreatPackagesAsDirectoriesKey];
    [[NSUserDefaults standardUserDefaults] removeObserver:self forKeyPath:FSBLastSelectedVolumeKey];
	[[NSNotificationCenter defaultCenter] removeObserver:self];
    [_browser release];
    [_rootnode release];
  [super dealloc];
}

- (void)SelectedCell:(NSNotification* )notification
{
    id something = [notification object];
    if ([something isKindOfClass:[NSNumber class]]) {
        NSInteger column = [(NSNumber* )something integerValue];
        [self setSelectedColumn:column];
    }
}

- (void) reloadBrowser {
  DLog(@"reloadBrowser");
	NSString *oldPath = self.browser.path;
	[self.browser loadColumnZero];
	[self.browser setPath:oldPath];
}

- (void) observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
  DLog(@"browser observeValueForKeyPath");
	if (context == @"Preferences") {
		if ([keyPath isEqualToString:FSBShowInvisibleFlesKey] ||
		    [keyPath isEqualToString:FSBTreatPackagesAsDirectoriesKey] ||
		    [keyPath isEqualToString:FSBLastSelectedVolumeKey])
			[self reloadBrowser];
	}
}

- (FSNode *) parentnodeForColumn:(NSInteger)column {
  DLog(@"browser parentnodeForColumn");
    
	FSNode *result = nil;
	NSString *selectedVolume = [[NSUserDefaults standardUserDefaults] objectForKey:FSBLastSelectedVolumeKey];
	if (column == 0) {
        result = [[[FSNode alloc] initWithParent:nil atRelativePath:selectedVolume] autorelease];
//        [self setRootnode:nil];
//        [self setRootnode:result];
//        [result release];
	} else {
		FSBrowserCell *selectedCell = [self.browser selectedCellInColumn:column - 1];
		result = [selectedCell node];
	}
	return result;
}

- (void)updateFileBrowserSelections
{
    NSInteger column = _selectedColumn;
    
    while (column >= 0) {
        
        column--;
        
        FSBrowserCell* cell = nil;
        cell = [_browser selectedCellInColumn:column];
        if (cell) {
            NSInteger aState = 0;
            NSUInteger selections = cell.node.subNodes.count;
            
            NSInteger nextColumn = column + 1;
            if (column >= 0) {
                NSUInteger cellsCount = [self browser:_browser numberOfRowsInColumn:nextColumn];
                selections = cellsCount;
                
                for (NSUInteger theIndex = 0; theIndex < cellsCount; theIndex++) {
                    FSBrowserCell* aCell = [_browser loadedCellAtRow:theIndex column:nextColumn];
                    
                    if (aCell) {
                        if (aCell.checkboxCell.state != NSOnState) {
                            selections--;
                            break;
                        }
                            
                    }
                }
                
                if (selections != cell.node.subNodes.count) aState = NSMixedState;
                else if (selections == cell.node.subNodes.count && selections > 0) aState = NSOnState;
                
                if (cell.node) [cell.node changeState:aState];
                [cell.checkboxCell setState:aState];
            }
        }
    }
}

#pragma mark -
#pragma mark NSBrowser Delegate Methods

- (NSInteger) browser:(NSBrowser *)sender numberOfRowsInColumn:(NSInteger)column {
  DLog(@"browser numberOfRowsInColumn");
	FSNode *parentnode = [self parentnodeForColumn:column];
	return [[parentnode subNodes] count];
}

- (void) browser:(NSBrowser *)sender willDisplayCell:(FSBrowserCell *)cell atRow:(NSInteger)row column:(NSInteger)column {
  DLog(@"browser willDisplayCell");
//    DLog(@"willDisplayCell:%d, %d",row, column);
    [cell setCurrentColumn:column];
	FSNode *parentnode = [self parentnodeForColumn:column];
    NSArray* subNodes = [NSArray arrayWithArray:[parentnode subNodes]];
	if (subNodes.count) {
        FSNode *currentnode = [subNodes objectAtIndex:row];
        if (currentnode) {
            [cell setNode:currentnode];
            [cell loadCellContents];
        }
    }
    
//  [cell setLoaded:YES];
//  [cell sto]
}

#pragma mark -
#pragma mark NSTableView Delegate Methods

- (void) tableViewSelectionDidChange:(NSNotification *)notification {
  DLog(@"tableViewSelectionDidChange");
	[self reloadBrowser];
}

- (void)reloadData {
  DLog(@"reloadData");
}

- (void)viewWillDraw {
  DLog(@"viewWillDraw");
  // We have to call super first in case the NSTableView does some layout in -viewWillDraw
  // [super viewWillDraw];
}

@end
