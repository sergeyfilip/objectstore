#import <Cocoa/Cocoa.h>

@interface HumanReadableDataSizeHelper : NSObject


/**
 @brief  Produces a string containing the largest appropriate units and the new fractional value.
 @param  sizeInBytes  The value to convert in bytes.
 
 This function converts the bytes value to a value in the greatest units that produces a value >= 1 and returns the new value and units as a string.
 
 The magnitude multiplier used is 1024 and the prefixes used are the binary prefixes (ki, Mi, ...).
 */
+ (NSString *)humanReadableSizeFromBytes:(NSNumber *)sizeInBytes;

/**
 @brief  Produces a string containing the largest appropriate units and the new fractional value.
 @param  sizeInBytes  The value to convert in bytes.
 @param  useSiPrefixes  Controls what prefix-set is used.
 @param  useSiMultiplier  Controls what magnitude multiplier is used.
 
 This function converts the bytes value to a value in the greatest units that produces a value >= 1 and returns the new value and units as a string.
 
 When useSiPrefixes is true, the prefixes used are the SI unit prefixes (k, M, ...).
 When useSiPrefixes is false, the prefixes used are the binary prefixes (ki, Mi, ...).
 
 When useSiMultiplier is true, the magnitude multiplier used is 1000
 When useSiMultiplier is false, the magnitude multiplier used is 1024.
 */
+ (NSString *)humanReadableSizeFromBytes:(NSNumber *)sizeInBytes  useSiPrefixes:(BOOL)useSiPrefixes  useSiMultiplier:(BOOL)useSiMultiplier;


@end