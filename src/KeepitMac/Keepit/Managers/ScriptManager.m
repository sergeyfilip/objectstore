//
//  ScriptManager.m
//  Keepit
//
//  Created by Vulture on 03.09.13.
//  Copyright (c) 2013 Evalesco. All rights reserved.
//

#import "ScriptManager.h"

@implementation ScriptManager

+ (void)runLoadLaunchdScript
{
    NSString* bundlId = [[NSBundle mainBundle] bundleIdentifier];
    if (bundlId.length) {
        NSString* scriptStr = [NSString stringWithFormat:@"launchctl load -w ~/Library/LaunchAgents/%@.plist", bundlId];
        const char *stringAsChar = [scriptStr cStringUsingEncoding:[NSString defaultCStringEncoding]];
        setuid(0);
        system(stringAsChar);
    }
}

+ (void)runUnloadLaunchdScript
{
    NSString* bundlId = [[NSBundle mainBundle] bundleIdentifier];
    if (bundlId.length) {
        NSString* scriptStr = [NSString stringWithFormat:@"launchctl unload -w ~/Library/LaunchAgents/%@.plist", bundlId];
        const char *stringAsChar = [scriptStr cStringUsingEncoding:[NSString defaultCStringEncoding]];
        setuid(0);
        system(stringAsChar);
    }
}

@end
