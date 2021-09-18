//
//  FSNode.m
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import "FSNode.h"
#import "Definitions.h"
#import "SettingsManager.h"

@interface FSNode (Private)
- (BOOL) shouldDisplay:(NSString *)path;
@end;

@implementation FSNode

@synthesize absolutePath = _absolutePath;
@synthesize relativePath = _relativePath;
@synthesize parentNode = _parentNode;
@synthesize isLink = _isLink;
@synthesize isDirectory = _isDirectory;
@synthesize isReadable = _isReadable;

+ (FSNode *) nodeWithParent:(FSNode *)parent atRelativePath:(NSString *)path {
  //DLog(@"nodeWithParent");
	return [[[FSNode alloc] initWithParent:parent atRelativePath:path] autorelease];
}

- (id) copyWithZone:(NSZone *)zone {
  //DLog(@"copyWithZone");
	return /*[FSNode nodeWithParent:self.parentNode atRelativePath:self.relativePath]*/[[FSNode alloc] initWithParent:self.parentNode atRelativePath:self.relativePath];
}

- (id) initWithParent:(FSNode *)parent atRelativePath:(NSString *)path {
  //DLog(@"initWithParent node absolut path: %@", path);
	if (self = [super init]) {
		path = [path stringByStandardizingPath];
		self.parentNode = parent;
		self.relativePath = path;
        
		if (self.parentNode != nil)
			self.absolutePath = [self.parentNode.absolutePath stringByAppendingPathComponent:self.relativePath];
		else
			self.absolutePath = self.relativePath;
        
		NSFileManager *fileManager = [NSFileManager defaultManager];
        
		/*NSDictionary *fileAttributes = [fileManager attributesOfItemAtPath:self.absolutePath error:nil];
		self.isLink = [[fileAttributes fileType] isEqualToString:NSFileTypeSymbolicLink];  it was made because the fileType return Dirrectory type for simylink*/
        BOOL linked = NO;
        if ([self checkForAliasFileWithPath:self.absolutePath]) linked = YES;
        else {
            NSError* error = nil;
            [fileManager destinationOfSymbolicLinkAtPath:self.absolutePath error:&error];
            if (!error) linked = YES;
        }
        self.isLink = linked;
        
		BOOL isDir;
		BOOL exists = [fileManager fileExistsAtPath:self.absolutePath isDirectory:&isDir];
		self.isDirectory = (exists && isDir &&
		                    ([[NSUserDefaults standardUserDefaults] boolForKey:FSBTreatPackagesAsDirectoriesKey] ||
		                     ![[NSWorkspace sharedWorkspace] isFilePackageAtPath:self.absolutePath]));
        
		self.isReadable = [fileManager isReadableFileAtPath:self.absolutePath];
	}
	return self;
}

- (void)dealloc
{
    [_subNodes release];
    [_parentNode release];
    [_relativePath release];
    [_absolutePath release];
    [super dealloc];
}

- (NSMutableArray *) subNodes {
  DLog(@"subNodes node absolut path: %@", self.absolutePath);
	if (_subNodes == nil) {
		NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:self.absolutePath error:nil];
		NSUInteger subCount = [contents count];
		NSMutableArray *mutableSubNodes = [[NSMutableArray alloc] initWithCapacity:subCount];
		for (NSString *subNodePath in contents) {
			NSString *path = [self.absolutePath stringByAppendingPathComponent:subNodePath];
			NSError* error = nil;
            [[NSWorkspace sharedWorkspace] typeOfFile:path error:&error];
            if (!error) {
                if ([self shouldDisplay:path]) {
                    FSNode *node = [FSNode nodeWithParent:self atRelativePath:subNodePath];
                    if ([node isDirectory] || FSBIncludeFiles ||
                        ([[NSWorkspace sharedWorkspace] isFilePackageAtPath:node.absolutePath] && FSBIncludePackages)) {
                        if (node.isReadable && !node.isLink) {
                            [mutableSubNodes addObject:node];
                        }
                    }
                }
            }else {
                DLog(@"error %@ for file at path %@", error.localizedDescription, path);
            }
		}
		_subNodes = [[NSMutableArray alloc] initWithArray:mutableSubNodes];
        [mutableSubNodes release];
	}
	return _subNodes;
}

- (BOOL)checkForAliasFileWithPath:(NSString* )path {
    BOOL isAliasFile = NO;
    FSRef fsRef;
    FSPathMakeRef((const UInt8 *)[path fileSystemRepresentation], &fsRef, NULL);
    Boolean isAliasFileBoolean, isFolder;
    FSIsAliasFile (&fsRef, &isAliasFileBoolean, &isFolder);
    if(isAliasFileBoolean)
        isAliasFile=YES;

    return isAliasFile;
}

- (NSImage *) iconImage {
  DLog(@"iconImage node absolut path: %@", self.absolutePath);
	NSSize size = NSMakeSize(FSBIconWH, FSBIconWH);
	NSString *path = self.absolutePath;
	NSImage *iconImage = [[NSWorkspace sharedWorkspace] iconForFile:path];
	if (!iconImage)
		iconImage = [[NSWorkspace sharedWorkspace] iconForFileType:[path pathExtension]];
	if (!iconImage)
		iconImage = [[NSWorkspace sharedWorkspace] iconForFileType:NSFileTypeForHFSTypeCode(kGenericDocumentIcon)];
	else {
		[iconImage setScalesWhenResized:YES];
		[iconImage setSize:size];
		if (self.isLink) {
			IconRef aliasBadgeIconRef;
			OSStatus result = GetIconRef(kOnSystemDisk, kSystemIconsCreator, kAliasBadgeIcon, &aliasBadgeIconRef);
			if (result == noErr) {
				NSImage *aliasBadge = [[[NSImage alloc] initWithIconRef:aliasBadgeIconRef] autorelease];
				NSImage *nodeImageWithBadge = [[[NSImage alloc] initWithSize:[iconImage size]] autorelease];
				[aliasBadge setScalesWhenResized:YES];
				[aliasBadge setSize:[iconImage size]];
				[nodeImageWithBadge lockFocus];
                [iconImage drawAtPoint:NSZeroPoint fromRect:NSZeroRect operation:NSCompositeSourceOver fraction:1.0f];
                [aliasBadge drawAtPoint:NSZeroPoint fromRect:NSZeroRect operation:NSCompositeSourceOver fraction:1.0f];
				[nodeImageWithBadge unlockFocus];
				iconImage = nodeImageWithBadge;
			}
			ReleaseIconRef(aliasBadgeIconRef);
		}
	}
	return iconImage;
}

- (NSInteger) state {
  
  DLog(@"state node absolut path: %@", self.absolutePath);
    NSMutableDictionary *includedItems = [NSMutableDictionary dictionaryWithDictionary:[[SettingsManager sharedInstance] getBackupFolders]];
    
    NSArray *excludedItems = [includedItems objectForKey:self.absolutePath];
    if (excludedItems) {
        if ([excludedItems count]) {
            _state = NSMixedState;
        } else {
            _state = NSOnState;
        }
    } else {
		NSUInteger subNodesSelected = 0;
        // Find out if the current node is intermediary in include path
		for (NSString *checkedItem in [includedItems allKeys]) {
			NSRange range = [checkedItem rangeOfString:[self.absolutePath stringByAppendingFormat:@"/"] options:0];
			if (range.length > 0 && range.location == 0) {
				subNodesSelected++;
            }
		}
		if (subNodesSelected == 0)
			_state = NSOffState;
		else// if (subNodesSelected < self.subNodes.count)
			_state = NSMixedState;
//		else
//			_state = NSOnState;

		if (_state == NSOffState) {
            BOOL bShouldSearch = YES;
            // find explicit inclusion parent by going up the three to the root
            NSString *path = self.absolutePath;
            while (![path isEqualToString:@"/"] && bShouldSearch) {
                NSArray *excludedItems = [includedItems objectForKey:path];
                if (excludedItems) {
                    _state = NSOnState;
                    if ([excludedItems count]) {
                        // find explicit exclusion parent by going up the three to the root
                        NSString *pathEx = self.absolutePath;
                        while (![pathEx isEqualToString:@"/"]) {
                            if ([excludedItems containsObject:pathEx]) {
                                _state = NSOffState;
                                bShouldSearch = NO;
                                break;
                            }
                            pathEx = [pathEx stringByDeletingLastPathComponent];
                        }
                        
                        // check if exlude paths contain my path (i.e. current node is intermediary)
                        if (bShouldSearch) {
                            for (NSString *excludedItem in excludedItems) {
                                NSRange range = [excludedItem rangeOfString:self.absolutePath options:0];
                                if (range.length > 0 && range.location == 0) {
                                    _state = NSMixedState;
                                    bShouldSearch = NO;
                                    break;
                                }
                            }
                        }
                    }
                }
                path = [path stringByDeletingLastPathComponent];
            }
		}
	}
    
    //DLog(@"FSBRedrawBrowserNotification2");
		//[[NSNotificationCenter defaultCenter] postNotificationName:FSBRedrawBrowserNotification object:self];
    /*static int i = 0;
    if (++i%100 == 0) {
        //
    }*/
    
	return _state;
}

- (void)changeState:(NSInteger )state
{
    _state = state;
    
   /* NSMutableDictionary *includedItems = [NSMutableDictionary dictionaryWithDictionary:[[SettingsManager sharedInstance] getBackupFolders]];
    NSMutableArray* exItems = nil;
    if (self.parentNode) {
        exItems = [NSMutableArray arrayWithArray:[includedItems objectForKey:self.parentNode.absolutePath]];
    }
    
    BOOL somethingChanged = NO;
    
    if (exItems) {
        NSInteger index = NSIntegerMax;
        for (NSInteger aIndex = 0; aIndex < exItems.count; aIndex++) {
            NSString* path = [exItems objectAtIndex:aIndex];
            if ([path isKindOfClass:[NSString class]]) {
                if ([path isEqualToString:self.parentNode.absolutePath]) {
                    index = aIndex;
                    break;
                }
            }
        }
        if (index != NSIntegerMax) {
            if (_state != NSOffState) {
                somethingChanged = YES;
                [exItems removeObjectAtIndex:index];
            }
        }else {
            if (_state == NSOffState) {
                somethingChanged = YES;
                [exItems addObject:self.parentNode.absolutePath];
            }
        }
    }
    
    if (somethingChanged) {
        [includedItems setObject:exItems forKey:self.parentNode.absolutePath];
        [[SettingsManager sharedInstance] setBackupFolders:includedItems];
    }*/
}

- (void) setState:(NSInteger)value {
	_state = value;
    
    DLog(@"___________________________");
    
  DLog(@"setState node absolut path: %@", self.absolutePath);
    
	NSMutableDictionary *includedItems = [NSMutableDictionary dictionaryWithDictionary:[[SettingsManager sharedInstance] getBackupFolders]];
    NSMutableDictionary *newIncludedItems = [NSMutableDictionary dictionary];

    DLog(@"inc dirrectories %@", includedItems);
    
    // remove includes (excludes) down the tree
		/*for (NSString *checkedItem in [includedItems allKeys]) {
    
            NSRange range = [checkedItem rangeOfString:self.absolutePath options:0];
            
            if (range.length < 1 || range.location == NSNotFound) {
                // just copy old value
                [newIncludedItems setObject:[includedItems objectForKey:checkedItem] forKey:checkedItem];
            }else {
                NSArray* firstPathComponents = [NSArray arrayWithArray:[self.absolutePath pathComponents]];
                NSString* firstPathLatestFolder = [firstPathComponents lastObject];
                
                NSArray* secondPathComponents = [NSArray arrayWithArray:[checkedItem pathComponents]];
                NSString* secondPathLastFolder = [secondPathComponents lastObject];
                
                DLog(@"items overboard %@", checkedItem);
                
                if ([firstPathLatestFolder isEqualToString:secondPathLastFolder] && firstPathComponents.count != secondPathComponents.count) {
                    DLog(@"resqued %@", checkedItem);
                    [newIncludedItems setObject:[includedItems objectForKey:checkedItem] forKey:checkedItem];
                }
            }
	}*/
    
    BOOL isKeyExist = NO;
    
    NSArray* keys = [NSArray arrayWithArray:[includedItems allKeys]];
    
    for (NSString* checkedItem in keys) {
        NSString* checkPath = nil;
        BOOL couldAddPath = NO;
        NSUInteger pathComponents = 0;
        
        if ([checkPath isEqualToString:@"/Volumes/Macintosh HD/.TemporaryItems"]) {
            //for breakpoint
        }
        
        if (checkedItem.length < self.absolutePath.length) {
            checkPath = self.absolutePath;
            while (checkedItem.length < checkPath.length) {
                checkPath = [checkPath stringByDeletingLastPathComponent];
                pathComponents++;
            }
            if (![checkedItem isEqualToString:checkPath]) {
                couldAddPath = YES;
            }else {
                couldAddPath = YES;
                isKeyExist = YES;
                if (pathComponents > 1) {
                    if (_state == NSOnState || _state == NSMixedState) {
                        isKeyExist = NO;
                        couldAddPath = NO;
                    }else {
                        isKeyExist = NO;
                    }
                }
            }
            
        }else if (checkedItem.length > self.absolutePath.length) {
            checkPath = checkedItem;
            while (checkPath.length > self.absolutePath.length) {
                checkPath = [checkPath stringByDeletingLastPathComponent];
            }
            if (![self.absolutePath isEqualToString:checkPath]) {
                couldAddPath = YES;
            }else {
                couldAddPath = NO;
                [includedItems removeObjectForKey:checkedItem];
                isKeyExist = NO;
            }
        }else if (checkedItem.length == self.absolutePath.length) {
            if (![checkedItem isEqualToString:self.absolutePath]) couldAddPath = YES;
        }
        
        if (couldAddPath) [newIncludedItems setObject:[includedItems objectForKey:checkedItem] forKey:checkedItem];
    }

	if (_state == NSOnState) {
        // find explicit inclusion parent by going up the three to the root
        NSString *path = [self.absolutePath stringByDeletingLastPathComponent];
        while (![path isEqualToString:@"/"]) {
            NSArray *excludedItems = [newIncludedItems objectForKey:path];
            if (excludedItems) {
                if ([excludedItems count]) {
                    // find explicit exclusion parent by going up the three to the root
                    if ([excludedItems containsObject:self.absolutePath]) {
                        NSMutableArray *newExcludedItems = [NSMutableArray arrayWithArray:excludedItems];
                        [newExcludedItems removeObject:self.absolutePath];
                        [newIncludedItems setObject:newExcludedItems forKey:path];
                        break;
                    }
                }
                
            }
            path = [path stringByDeletingLastPathComponent];
        }
        // set empty exclusions for newly included folder
        if (!isKeyExist) [newIncludedItems setObject:[NSMutableArray array] forKey:self.absolutePath];
    } else if (_state == NSOffState) {
        // find explicit inclusion parent (for exclusion) by going up the three to the root
        NSString *path = [self.absolutePath stringByDeletingLastPathComponent];
        while (![path isEqualToString:@"/"]) {
            NSArray *excludedItems = [newIncludedItems objectForKey:path];
            if (excludedItems) {
                // remove excludes down the tree
                NSMutableArray *newExcludedItems = [NSMutableArray array];
                for (NSString *excludedItem in excludedItems) {
                    NSRange range = [excludedItem rangeOfString:self.absolutePath options:0];
                    if (range.length < 1 || range.location == NSNotFound) {
                        // just copy old value
                        [newExcludedItems addObject:excludedItem];
                    }
                }
                [newExcludedItems addObject:self.absolutePath];
                [newIncludedItems setObject:newExcludedItems forKey:path];
                break;
            }
            path = [path stringByDeletingLastPathComponent];
        }
    }
    
    DLog(@"outc dirrectories %@", newIncludedItems);
    
    DLog(@"<<<<<<<<<<<<<<<<<<<<<finish>>>>>>>>>>>>>>>>>>>>>>>>>>");
    
    [[SettingsManager sharedInstance] setBackupFolders:newIncludedItems];

    //DLog(@"FSBRedrawBrowserNotification1");
		[[NSNotificationCenter defaultCenter] postNotificationName:FSBRedrawBrowserNotification object:self];
}

@dynamic state, subNodes;

@end

@implementation FSNode (Private)

- (BOOL) shouldDisplay:(NSString *)path {
  
  DLog(@"shouldDisplay, %@", path);
	if ([path isEqualToString:@"/dev"] ||
	    [path isEqualToString:@"/Network"] ||
	    [path isEqualToString:@"/mach"] ||
	    [path isEqualToString:@"/mach.sym"] ||
	    [path isEqualToString:@"/net"] ||
	    [path isEqualToString:@"/cores"] ||
	    [path isEqualToString:@"/tmp"] ||
	    [path isEqualToString:@"/home"])
		return NO;
    
	NSString *lastPathComponent = [path lastPathComponent];
	if ([[lastPathComponent stringByDeletingPathExtension] isEqualToString:@"mach_kernel"] ||
	    [lastPathComponent isEqualToString:@".DS_Store"] ||
	    [lastPathComponent isEqualToString:@"Desktop DB"] ||
	    [lastPathComponent isEqualToString:@"Desktop DF"] ||
	    [lastPathComponent isEqualToString:@".Spotlight-V100"] ||
	    [lastPathComponent isEqualToString:@".SymAVQSFile"] ||
	    [lastPathComponent isEqualToString:@".vol"] ||
	    [lastPathComponent isEqualToString:@".Trashes"] ||
	    [lastPathComponent isEqualToString:@".hotfiles.btree"] ||
	    [lastPathComponent isEqualToString:@".fseventsd"] ||
	    [lastPathComponent isEqualToString:@".VolumeIcon.icns"] ||
	    [lastPathComponent isEqualToString:@".com.apple.timemachine.supported"] ||
	    [lastPathComponent isEqualToString:@"Backups.backupdb"])
		return NO;
    
	if (![[NSUserDefaults standardUserDefaults] boolForKey:FSBShowInvisibleFlesKey]) {
		BOOL isInvisible = NO;
		NSURL *url = [NSURL fileURLWithPath:path];
		NSNumber *isHidden = nil;
		if ([url getResourceValue:&isHidden forKey:NSURLIsHiddenKey error:nil])
			isInvisible = [isHidden isEqual:[NSNumber numberWithInt:1]];
		if (isInvisible)
			return NO;
	}
    
	return YES;
}

@end
