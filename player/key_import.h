#pragma once
// Installs the user's keys.txt (or Citra's older aes_keys.txt), boot9.bin and otp.bin into
// Azahar's sysdata folder. keys.txt goes through the Bergamot app's validator (AES section,
// known key names, 32 hex digits each); the .bin files are checked by size. Never logs values.
// Needs player_session.h (BGPlayerDataRoot) and ios/user_keys.h (BGValidatedAESKeys).
#import <Foundation/Foundation.h>

static NSURL* BGSysDataFolder() {
    NSURL* u = [BGPlayerDataRoot() URLByAppendingPathComponent:@"saves/Azahar/sysdata"];
    [NSFileManager.defaultManager createDirectoryAtURL:u withIntermediateDirectories:YES attributes:nil error:nil];
    return u;
}

// Returns a one-line summary; *problems lists the files that were refused.
static NSString* BGInstallKeyFiles(NSArray<NSURL*>* urls, BOOL gameRanThisLaunch, NSArray<NSString*>** problemsOut) {
    NSMutableArray* done = [NSMutableArray new]; NSMutableArray* problems = [NSMutableArray new];
    NSURL* sys = BGSysDataFolder();
    for (NSURL* u in urls) {
        NSString* name = u.lastPathComponent.lowercaseString; NSData* data = [NSData dataWithContentsOfURL:u];
        if (!data) { [problems addObject:[NSString stringWithFormat:@"Couldn't read %@.", u.lastPathComponent]]; continue; }
        if ([name hasSuffix:@".txt"]) {
            // Older files name the scrambler constant "generator", which Azahar calls
            // "generatorConstant". Always saved as keys.txt, the only name this Azahar reads.
            NSString* raw = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
            NSRegularExpression* old = [NSRegularExpression regularExpressionWithPattern:@"(?m)^\\s*generator\\s*=" options:0 error:nil];
            raw = [old stringByReplacingMatchesInString:raw options:0 range:NSMakeRange(0, raw.length) withTemplate:@"generatorConstant="];
            NSString* keys = BGValidatedAESKeys([raw dataUsingEncoding:NSUTF8StringEncoding]);
            if (!keys) { [problems addObject:[NSString stringWithFormat:@"%@ has a line that isn't a valid AES key.", u.lastPathComponent]]; continue; }
            NSURL* dest = [sys URLByAppendingPathComponent:@"keys.txt"];
            [keys writeToURL:dest atomically:YES encoding:NSUTF8StringEncoding error:nil];
            [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions: @0600} ofItemAtPath:dest.path error:nil];
            [done addObject:[NSString stringWithFormat:@"keys.txt (%lu keys)", (unsigned long)([keys componentsSeparatedByString:@"="].count - 1)]];
        } else if ([name isEqualToString:@"boot9.bin"] || [name isEqualToString:@"otp.bin"]) {
            const NSUInteger want = [name isEqualToString:@"otp.bin"] ? 256 : 65536;
            if (data.length != want && !([name isEqualToString:@"boot9.bin"] && data.length == 32768)) {
                [problems addObject:[NSString stringWithFormat:@"%@ is the wrong size.", u.lastPathComponent]]; continue;
            }
            NSURL* dest = [sys URLByAppendingPathComponent:name];
            [data writeToURL:dest atomically:YES];
            [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions: @0600} ofItemAtPath:dest.path error:nil];
            [done addObject:name];
        } else if ([name isEqualToString:@"seeddb.bin"]) {
            // Seeds for games that use seed encryption: a 16-byte header, then 32 bytes per title.
            if (data.length < 16 || (data.length - 16) % 32 != 0) {
                [problems addObject:[NSString stringWithFormat:@"%@ is the wrong size.", u.lastPathComponent]]; continue;
            }
            NSURL* dest = [sys URLByAppendingPathComponent:name];
            [data writeToURL:dest atomically:YES];
            [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions: @0600} ofItemAtPath:dest.path error:nil];
            [done addObject:[NSString stringWithFormat:@"seeddb.bin (%lu seeds)", (unsigned long)((data.length - 16) / 32)]];
        } else {
            [problems addObject:[NSString stringWithFormat:@"%@ isn't keys.txt, seeddb.bin, boot9.bin or otp.bin.", u.lastPathComponent]];
        }
    }
    NSMutableString* text = [NSMutableString new];
    if (done.count) [text appendFormat:@"Installed %@.", [done componentsJoinedByString:@", "]];
    if (done.count && gameRanThisLaunch) [text appendString:@" Quit and reopen the app so they take effect."];
    if (problems.count) [text appendFormat:@"%@%@", text.length ? @" " : @"", [problems componentsJoinedByString:@" "]];
    if (problemsOut) *problemsOut = done.count ? @[] : problems;
    return text;
}
