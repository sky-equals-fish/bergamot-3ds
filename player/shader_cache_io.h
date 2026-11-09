#pragma once
// Moves Azahar's per-game shader caches between devices. The cache records each game's shader
// and pipeline setups rather than GPU binaries, so a cache made on one device prepares the same
// shaders on another (Azahar regenerates them for the new device). A device that imports a cache
// builds those shaders behind "Preparing shaders" instead of stalling on them during play.
//
// A .bgshaders file: 8-byte magic "BGSHADR1", u32 file count, then per file a u16 name length,
// the name (ASCII), a u64 size and the bytes; integers little-endian. Only files named like
// Azahar's own (<16 hex digits>_<vs|fs|gs|pl>.vkch) are written or accepted.
// Needs player_session.h (BGPlayerDataRoot).
#import <Foundation/Foundation.h>
#include <cstring>

static NSString* const BGShaderPackExtension = @"bgshaders";

static NSURL* BGShaderCacheFolder() {
    NSURL* u = [BGPlayerDataRoot() URLByAppendingPathComponent:@"saves/Azahar/shaders/vulkan/transferable"];
    [NSFileManager.defaultManager createDirectoryAtURL:u withIntermediateDirectories:YES attributes:nil error:nil];
    return u;
}

// Returns the title ID part of an accepted cache file name, or nil.
static NSString* BGShaderCacheTitle(NSString* name) {
    static NSRegularExpression* pattern = [NSRegularExpression regularExpressionWithPattern:@"^([0-9A-F]{16})_(vs|fs|gs|pl)\\.vkch$" options:0 error:nil];
    NSTextCheckingResult* m = [pattern firstMatchInString:name options:0 range:NSMakeRange(0, name.length)];
    return m ? [name substringWithRange:[m rangeAtIndex:1]] : nil;
}
static BOOL BGIsGameTitle(NSString* title) { return ![title isEqualToString:@"0000000000000000"]; }

// Writes every game's shader cache into one file. Returns a short message; *failed on error.
static NSString* BGExportShaderCache(NSURL* destination, BOOL* failed) {
    *failed = YES;
    NSArray<NSURL*>* files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:BGShaderCacheFolder() includingPropertiesForKeys:nil options:0 error:nil];
    NSMutableData* pack = [NSMutableData dataWithBytes:"BGSHADR1" length:8];
    NSMutableArray<NSData*>* bodies = [NSMutableArray new]; NSMutableArray<NSString*>* names = [NSMutableArray new];
    NSMutableSet* titles = [NSMutableSet new];
    for (NSURL* f in [files sortedArrayUsingComparator:^(NSURL* a, NSURL* b) { return [a.lastPathComponent compare:b.lastPathComponent]; }]) {
        NSString* title = BGShaderCacheTitle(f.lastPathComponent);
        if (!title) continue;
        NSData* d = [NSData dataWithContentsOfURL:f options:NSDataReadingMappedIfSafe error:nil];
        if (!d.length) continue;
        [names addObject:f.lastPathComponent]; [bodies addObject:d];
        if (BGIsGameTitle(title)) [titles addObject:title];
    }
    if (!titles.count) return @"There are no shaders to export yet. Play a game first.";
    const uint32_t count = (uint32_t)names.count; [pack appendBytes:&count length:4];
    for (NSUInteger i = 0; i < names.count; ++i) {
        NSData* n = [names[i] dataUsingEncoding:NSASCIIStringEncoding];
        const uint16_t nameLength = (uint16_t)n.length; const uint64_t size = bodies[i].length;
        [pack appendBytes:&nameLength length:2]; [pack appendData:n]; [pack appendBytes:&size length:8]; [pack appendData:bodies[i]];
    }
    NSError* error = nil;
    if (![pack writeToURL:destination options:NSDataWritingAtomic error:&error]) return error.localizedDescription ?: @"Couldn't write the file.";
    *failed = NO;
    return [NSString stringWithFormat:@"Exported shaders for %lu game%@.", (unsigned long)titles.count, titles.count == 1 ? @"" : @"s"];
}

static NSString* BGMergeShaderSets(NSDictionary<NSString*, NSDictionary<NSString*, NSData*>*>* byTitle, BOOL* failed);

// Merges a .bgshaders file into this device's caches. A game's set of files is replaced only
// when the imported set is larger, so an import never shrinks a cache. Do not call while a
// game is running.
static NSString* BGImportShaderCache(NSURL* source, BOOL* failed) {
    *failed = YES;
    NSData* pack = [NSData dataWithContentsOfURL:source options:NSDataReadingMappedIfSafe error:nil];
    const uint8_t* p = (const uint8_t*)pack.bytes; const uint64_t total = pack.length;
    if (total < 12 || std::memcmp(p, "BGSHADR1", 8) != 0 || total > (1ull << 31)) return @"That isn't a Bergamot shader file.";
    uint32_t count; std::memcpy(&count, p + 8, 4);
    uint64_t at = 12;
    NSMutableDictionary<NSString*, NSMutableDictionary<NSString*, NSData*>*>* byTitle = [NSMutableDictionary new];
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLength; uint64_t size;
        if (at + 2 > total) return @"That shader file is damaged.";
        std::memcpy(&nameLength, p + at, 2); at += 2;
        if (nameLength == 0 || nameLength > 64 || at + nameLength + 8 > total) return @"That shader file is damaged.";
        NSString* name = [[NSString alloc] initWithBytes:p + at length:nameLength encoding:NSASCIIStringEncoding]; at += nameLength;
        std::memcpy(&size, p + at, 8); at += 8;
        if (size > total - at) return @"That shader file is damaged.";
        NSString* title = name ? BGShaderCacheTitle(name) : nil;
        if (!title) return @"That shader file has an unexpected entry.";
        if (!byTitle[title]) byTitle[title] = [NSMutableDictionary new];
        byTitle[title][name] = [pack subdataWithRange:NSMakeRange(at, size)];
        at += size;
    }
    return BGMergeShaderSets(byTitle, failed);
}

// Shared by both import paths: a game's set replaces this device's only when it is larger.
static NSString* BGMergeShaderSets(NSDictionary<NSString*, NSDictionary<NSString*, NSData*>*>* byTitle, BOOL* failed) {
    NSURL* folder = BGShaderCacheFolder(); NSFileManager* fm = NSFileManager.defaultManager;
    NSUInteger imported = 0, kept = 0;
    for (NSString* title in byTitle) {
        if (!BGIsGameTitle(title)) continue;
        NSDictionary<NSString*, NSData*>* incoming = byTitle[title];
        uint64_t incomingSize = 0, existingSize = 0;
        for (NSData* d in incoming.allValues) incomingSize += d.length;
        NSArray<NSString*>* stages = @[@"vs", @"fs", @"gs", @"pl"];
        for (NSString* s in stages) {
            NSNumber* size = nil;
            [[folder URLByAppendingPathComponent:[NSString stringWithFormat:@"%@_%@.vkch", title, s]] getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
            existingSize += size.unsignedLongLongValue;
        }
        if (incomingSize <= existingSize) { ++kept; continue; }
        for (NSString* s in stages) {
            NSString* name = [NSString stringWithFormat:@"%@_%@.vkch", title, s];
            NSURL* dest = [folder URLByAppendingPathComponent:name];
            if (NSData* d = incoming[name]) [d writeToURL:dest options:NSDataWritingAtomic error:nil];
            else [fm removeItemAtURL:dest error:nil];   // keep each game's set consistent
        }
        ++imported;
    }
    *failed = NO;
    if (!imported && !kept) return @"There are no game shaders in that.";
    NSMutableString* text = [NSMutableString new];
    if (imported) [text appendFormat:@"Imported shaders for %lu game%@. They're prepared the next time the game starts.", (unsigned long)imported, imported == 1 ? @"" : @"s"];
    if (kept) [text appendFormat:@"%@Kept this device's shaders for %lu game%@, which already had as many.", text.length ? @" " : @"", (unsigned long)kept, kept == 1 ? @"" : @"s"];
    return text;
}

// Imports Azahar's own cache files picked directly, for example the four
// <title>_vs/_fs/_gs/_pl.vkch files from another app's shaders/vulkan/transferable folder. Each
// game needs at least its vs, fs and pl files, so a partial pick never replaces a full set.
static NSString* BGImportShaderFiles(NSArray<NSURL*>* urls, BOOL* failed) {
    *failed = YES;
    NSMutableDictionary<NSString*, NSMutableDictionary<NSString*, NSData*>*>* byTitle = [NSMutableDictionary new];
    for (NSURL* u in urls) {
        NSString* title = BGShaderCacheTitle(u.lastPathComponent);
        if (!title) return [NSString stringWithFormat:@"%@ isn't a shader cache file (expected names like 000400000011C500_vs.vkch).", u.lastPathComponent];
        NSData* d = [NSData dataWithContentsOfURL:u options:NSDataReadingMappedIfSafe error:nil];
        if (!d.length || d.length > (1ull << 30)) return [NSString stringWithFormat:@"Couldn't read %@.", u.lastPathComponent];
        if (!byTitle[title]) byTitle[title] = [NSMutableDictionary new];
        byTitle[title][u.lastPathComponent] = d;
    }
    for (NSString* title in byTitle)
        for (NSString* stage in @[@"vs", @"fs", @"pl"])
            if (!byTitle[title][[NSString stringWithFormat:@"%@_%@.vkch", title, stage]])
                return @"Pick all of a game's shader files together: its _vs, _fs, _gs and _pl .vkch files.";
    return BGMergeShaderSets(byTitle, failed);
}
