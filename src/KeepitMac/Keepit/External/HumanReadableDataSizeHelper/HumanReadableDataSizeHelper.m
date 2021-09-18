//http://stackoverflow.com/questions/572614/objc-cocoa-class-for-converting-size-to-human-readable-string
//
//Use it generally like this: [HumanReadableDataSizeHelper humanReadableSizeFromBytes:[NSNumber numberWithDouble:doubleValue]].
//
//But, it looks like you want SI units with a 1024 multiplier so you would use it like this: [HumanReadableDataSizeHelper humanReadableSizeFromBytes:[NSNumber numberWithDouble:doubleValue] useSiPrefixes:YES useSiMultiplier:NO]
//
//The reason I default to binary prefixes (ki, Mi) is because those seem to be the most appropriate unit prefix set to use for sizes of data on a computer. What you requested was the SI unit prefixes but using a multiplier of 1024, technically incorrect. Though I will note that SI prefixes for multiples of 1024 is fairly common and binary prefixes are not well accepted (according to Wikipedia).
//

#import "HumanReadableDataSizeHelper.h"

@implementation HumanReadableDataSizeHelper


+ (NSString *)humanReadableSizeFromBytes:(NSNumber *)sizeInBytes
{
  return [self humanReadableSizeFromBytes:sizeInBytes  useSiPrefixes:NO  useSiMultiplier:NO];
}


+ (NSString *)humanReadableSizeFromBytes:(NSNumber *)sizeInBytes  useSiPrefixes:(BOOL)useSiPrefixes  useSiMultiplier:(BOOL)useSiMultiplier
{
  NSString *unitSymbol = @"B";
  NSInteger multiplier;
  NSArray *prefixes;
  
  if (useSiPrefixes)
  {
    /*  SI prefixes
     http://en.wikipedia.org/wiki/Kilo-
     kilobyte   (kB)    10^3
     megabyte   (MB)    10^6
     gigabyte   (GB)    10^9
     terabyte   (TB)    10^12
     petabyte   (PB)    10^15
     exabyte    (EB)    10^18
     zettabyte  (ZB)    10^21
     yottabyte  (YB)    10^24
     */
    
    prefixes = [NSArray arrayWithObjects: @"", @"k", @"M", @"G", @"T", @"P", @"E", @"Z", @"Y", nil];
  }
  else
  {
    /*  Binary prefixes
     http://en.wikipedia.org/wiki/Binary_prefix
     kibibyte   (KiB)   2^10 = 1.024 * 10^3
     mebibyte   (MiB)   2^20 ≈ 1.049 * 10^6
     gibibyte   (GiB)   2^30 ≈ 1.074 * 10^9
     tebibyte   (TiB)   2^40 ≈ 1.100 * 10^12
     pebibyte   (PiB)   2^50 ≈ 1.126 * 10^15
     exbibyte   (EiB)   2^60 ≈ 1.153 * 10^18
     zebibyte   (ZiB)   2^70 ≈ 1.181 * 10^21
     yobibyte   (YiB)   2^80 ≈ 1.209 * 10^24
     */
    
    prefixes = [NSArray arrayWithObjects: @"", @"ki", @"Mi", @"Gi", @"Ti", @"Pi", @"Ei", @"Zi", @"Yi", nil];
  }
  
  if (useSiMultiplier)
  {
    multiplier = 1000;
  }
  else
  {
    multiplier = 1024;
  }
  
  NSInteger exponent = 0;
  double size = [sizeInBytes doubleValue];
  
  while ( (size >= multiplier) && (exponent < [prefixes count]) )
  {
    size /= multiplier;
    exponent++;
  }
  
  NSNumberFormatter* formatter = [[NSNumberFormatter alloc] init];
  [formatter setMaximumFractionDigits:2];
  [formatter setNumberStyle:NSNumberFormatterDecimalStyle]; // Uses localized number formats.
  
  NSString *sizeInUnits = [formatter stringFromNumber:[NSNumber numberWithDouble:size]];
    [formatter release];
  
  return [NSString stringWithFormat:@"%@ %@%@", sizeInUnits, [prefixes objectAtIndex:exponent], unitSymbol];
}

@end