//
//  Definitions.m
//  FileSystemBrowser
//
//  Created by Karl Moskowski on 2012-09-26.
//  Copyright (c) 2012 Karl Moskowski. All rights reserved.
//

#import "Definitions.h"

NSString *const FSBCheckedItemsKey = @"CheckedItems";
NSString *const IncludedItemsKey = @"IncludedItems";
NSString *const ExcludedItemsKey = @"ExcludedItems";
//NSString *const FSBItemPathsKey = @"ItemPaths";
//NSString *const FSBItemExclusionsKey = @"ItemExclusions";
//NSString *const FSBItemLabelKey = @"ItemLabel";
//NSString *const FSBItemTypeKey = @"ItemType";
//NSString *const FSBIconPathKey = @"IconPath";

NSString *const FSBShowInvisibleFlesKey = @"ShowInvisibleFiles";
NSString *const FSBTreatPackagesAsDirectoriesKey = @"TreatPackagesAsDirectories";

NSString *const FSBRedrawBrowserNotification = @"FSBrowser.notification.redrawBrowser";

NSUInteger const FSBMaxVisibleColums = 10;
NSString *const FSBLastBrowserPathKey = @"LastBrowserPath";
NSString *const FSBLastSelectedVolumeKey = @"LastSelectedVolume";

CGFloat const FSBIconWH = 16.0f;
CGFloat const FSBiconInsetV = 2.0f;
CGFloat const FSBiconInsetH = 4.0f;
CGFloat const FSBIconTextSpace = 2.0f;

BOOL const FSBIncludeFiles = YES;
BOOL const FSBIncludePackages = YES;
BOOL const FSBIncludeReadOnlyVolumes = YES;
BOOL const FSBIncludeNetworkVolumes = NO;
