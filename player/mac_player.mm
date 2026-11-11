// SPDX-License-Identifier: GPL-3.0-or-later
// Bergamot 3DS for Mac: open a game, play it. Saves are kept automatically.
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "player_session.h"
#import "user_keys.h"
#import "key_import.h"
#import "shader_cache_io.h"

// Azahar reads keys.txt once per launch, when the first game starts.
static BOOL BGGameRanThisLaunch = NO;
#include <map>

static NSArray<NSString*>* BGGameExtensions() { return @[@"3ds", @"cci", @"cxi", @"cia", @"3dsx", @"app", @"elf", @"axf"]; }
static BOOL BGIsGame(NSURL* u) { return [BGGameExtensions() containsObject:u.pathExtension.lowercaseString]; }
static NSColor* BGHex(unsigned v) { return [NSColor colorWithSRGBRed:((v >> 16) & 255) / 255.0 green:((v >> 8) & 255) / 255.0 blue:(v & 255) / 255.0 alpha:1]; }

@interface BGMacScreen : NSView
@property(nonatomic, copy) void (^keyInput)(unsigned short, BOOL);
@property(nonatomic, copy) void (^pointer)(CGPoint, BOOL);
@property(nonatomic, copy) void (^dropped)(NSURL*);
@property(nonatomic, copy) void (^resized)(void);
@end
@implementation BGMacScreen
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) { self.wantsLayer = YES; [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]]; }
    return self;
}
- (CALayer*)makeBackingLayer {
    CAMetalLayer* l = [CAMetalLayer layer]; l.device = MTLCreateSystemDefaultDevice();
    l.pixelFormat = MTLPixelFormatBGRA8Unorm; l.framebufferOnly = YES; l.maximumDrawableCount = 3; l.opaque = NO;
    // The emulator paces itself at the console's 59.83 Hz. With display sync on, every
    // present blocks for the next vblank, so a ProMotion panel idling at 48 Hz held the
    // game at exactly 80% speed. BG_DISPLAY_SYNC=1 restores it for comparison.
    const char* sync = getenv("BG_DISPLAY_SYNC");
    l.displaySyncEnabled = sync && atoi(sync) != 0;
    l.backgroundColor = BGHex(0x141B25).CGColor;
    return l;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)layout {
    [super layout];
    CAMetalLayer* l = (CAMetalLayer*)self.layer;
    l.contentsScale = self.window.backingScaleFactor ?: 2;
    l.drawableSize = [self convertRectToBacking:self.bounds].size;
    if (self.resized) self.resized();
}
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; self.needsLayout = YES; }
- (void)keyDown:(NSEvent*)e { if (!e.isARepeat && self.keyInput) self.keyInput(e.keyCode, YES); }
- (void)keyUp:(NSEvent*)e { if (self.keyInput) self.keyInput(e.keyCode, NO); }
- (void)flagsChanged:(NSEvent*)e { (void)e; }
- (void)mouseDown:(NSEvent*)e { [self.window makeFirstResponder:self]; if (self.pointer) self.pointer([self convertPoint:e.locationInWindow fromView:nil], YES); }
- (void)mouseDragged:(NSEvent*)e { if (self.pointer) self.pointer([self convertPoint:e.locationInWindow fromView:nil], YES); }
- (void)mouseUp:(NSEvent*)e { if (self.pointer) self.pointer([self convertPoint:e.locationInWindow fromView:nil], NO); }
- (NSURL*)draggedGame:(id<NSDraggingInfo>)info {
    NSURL* u = [NSURL URLFromPasteboard:info.draggingPasteboard];
    return u && BGIsGame(u) ? u : nil;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info { return [self draggedGame:info] ? NSDragOperationCopy : NSDragOperationNone; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info { NSURL* u = [self draggedGame:info]; if (u && self.dropped) self.dropped(u); return u != nil; }
@end

@interface BGMacPlayer : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation, NSMenuDelegate>
@end
@implementation BGMacPlayer {
    NSWindow* _window; BGMacScreen* _screen; NSView* _empty; NSTextField* _hint; NSTextField* _pausedLabel;
    BG3DSSession* _session; CTRSkinInput _keys; NSString* _title; BOOL _sideBySide; NSURL* _pendingOpen;
    NSWindow* _settings; NSPopUpButton *_scalePopup, *_layoutPopup, *_handoffPopup, *_shaderPopup; NSTextField* _scaleNote;
    id<NSObject> _playing;   // keeps the display awake and undimmed while a game runs
}
- (ctr::DualScreenLayout)slots {
    return _sideBySide ? ctr::DualScreenLayout{{.01, .12, .55, .76}, {.58, .2, .41, .6}}
                       : ctr::DualScreenLayout{{.02, .01, .96, .485}, {.12, .505, .76, .485}};
}
- (void)applySlots { auto s = [self slots]; [_session setTopSlot:CGRectMake(s.top.x, s.top.y, s.top.width, s.top.height) bottomSlot:CGRectMake(s.bottom.x, s.bottom.y, s.bottom.width, s.bottom.height)]; }
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    (void)n;
    _sideBySide = [NSUserDefaults.standardUserDefaults boolForKey:@"BGPlayerSideBySide"];
    [self buildMenu];
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 720)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Bergamot 3DS"; _window.delegate = self; _window.minSize = NSMakeSize(400, 480);
    _window.backgroundColor = BGHex(0x141B25); _window.frameAutosaveName = @"BGPlayerWindow";
    _screen = [[BGMacScreen alloc] initWithFrame:_window.contentView.bounds];
    _screen.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _window.contentView = _screen;
    _session = [[BG3DSSession alloc] initWithLayer:(CAMetalLayer*)_screen.layer];
    [self applySlots];
    __weak BGMacPlayer* weakSelf = self;
    _session.statusChanged = ^(NSString* s) { BGMacPlayer* p = weakSelf; if (p) p->_window.subtitle = s; };
    _session.ended = ^(NSString* message, BOOL failed) { [weakSelf sessionEnded:message failed:failed]; };
    _session.controllerMenu = ^{ [weakSelf togglePause:nil]; };
    _session.loadingChanged = ^(NSString* message) { [weakSelf showLoading:message]; };
    _screen.keyInput = ^(unsigned short k, BOOL down) { [weakSelf key:k down:down]; };
    _screen.pointer = ^(CGPoint p, BOOL down) { [weakSelf pointer:p down:down]; };
    _screen.dropped = ^(NSURL* u) { [weakSelf openGame:u]; };
    [self buildEmptyState];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(occlusionChanged:) name:NSWindowDidChangeOcclusionStateNotification object:_window];
    [_window center]; [_window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    if (_pendingOpen) { NSURL* u = _pendingOpen; _pendingOpen = nil; [self application:NSApp openFile:u.path]; }
    // Benchmark hook: BG_RUN_SECONDS=N stops the game after N seconds and quits cleanly.
    if (const char* seconds = getenv("BG_RUN_SECONDS"))
        [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:atof(seconds)];   // run-loop timer, not a main-queue block
}
- (NSTextField*)label:(NSString*)text size:(CGFloat)size color:(NSColor*)color {
    NSTextField* t = [NSTextField labelWithString:text]; t.font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightMedium];
    t.textColor = color; t.alignment = NSTextAlignmentCenter; t.translatesAutoresizingMaskIntoConstraints = NO; return t;
}
- (void)buildEmptyState {
    _empty = [NSView new]; _empty.translatesAutoresizingMaskIntoConstraints = NO; [_screen addSubview:_empty];
    NSImage* art = [NSImage imageNamed:@"fuitlet"];
    NSImageView* mascot = [NSImageView imageViewWithImage:art ?: [NSImage new]];
    mascot.imageScaling = NSImageScaleProportionallyUpOrDown; mascot.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* title = [self label:@"BERGAMOT 3DS" size:22 color:BGHex(0xDFE8EF)];
    _hint = [self label:@"Drop a game here, or press ⌘O" size:13 color:BGHex(0x93A4B5)];
    NSButton* open = [NSButton buttonWithTitle:@"Open Game…" target:self action:@selector(openDocument:)];
    open.translatesAutoresizingMaskIntoConstraints = NO; open.bezelColor = BGHex(0x5EE6E6);
    NSTextField* keys = [self label:@"Z/X/A/S  A/B/Y/X   Arrows  Circle Pad   IJKL  D-Pad\nQ/W  L/R   1/2  ZL/ZR   Return  Start   Tab  Select   Esc  Pause\nMouse  Touch screen   Game controllers work too" size:11 color:BGHex(0x93A4B5)];
    keys.maximumNumberOfLines = 3;
    for (NSView* v in @[mascot, title, _hint, open, keys]) [_empty addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [_empty.centerXAnchor constraintEqualToAnchor:_screen.centerXAnchor], [_empty.centerYAnchor constraintEqualToAnchor:_screen.centerYAnchor],
        [_empty.widthAnchor constraintLessThanOrEqualToAnchor:_screen.widthAnchor constant:-32],
        [mascot.topAnchor constraintEqualToAnchor:_empty.topAnchor], [mascot.centerXAnchor constraintEqualToAnchor:_empty.centerXAnchor],
        [mascot.widthAnchor constraintEqualToConstant:128], [mascot.heightAnchor constraintEqualToConstant:128],
        [title.topAnchor constraintEqualToAnchor:mascot.bottomAnchor constant:12], [title.centerXAnchor constraintEqualToAnchor:_empty.centerXAnchor],
        [_hint.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:6], [_hint.centerXAnchor constraintEqualToAnchor:_empty.centerXAnchor],
        [_hint.widthAnchor constraintLessThanOrEqualToAnchor:_empty.widthAnchor],
        [open.topAnchor constraintEqualToAnchor:_hint.bottomAnchor constant:16], [open.centerXAnchor constraintEqualToAnchor:_empty.centerXAnchor],
        [keys.topAnchor constraintEqualToAnchor:open.bottomAnchor constant:24], [keys.centerXAnchor constraintEqualToAnchor:_empty.centerXAnchor],
        [keys.bottomAnchor constraintEqualToAnchor:_empty.bottomAnchor],
        [keys.leadingAnchor constraintGreaterThanOrEqualToAnchor:_empty.leadingAnchor], [keys.trailingAnchor constraintLessThanOrEqualToAnchor:_empty.trailingAnchor],
    ]];
    _pausedLabel = [self label:@"PAUSED · Esc to resume" size:16 color:BGHex(0x5EE6E6)];
    _pausedLabel.drawsBackground = YES; _pausedLabel.backgroundColor = [BGHex(0x141B25) colorWithAlphaComponent:.85];
    _pausedLabel.hidden = YES; [_screen addSubview:_pausedLabel];
    [NSLayoutConstraint activateConstraints:@[[_pausedLabel.centerXAnchor constraintEqualToAnchor:_screen.centerXAnchor],
                                              [_pausedLabel.centerYAnchor constraintEqualToAnchor:_screen.centerYAnchor]]];
}
- (NSMenuItem*)item:(NSString*)title action:(SEL)action key:(NSString*)key tag:(NSInteger)tag {
    NSMenuItem* i = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key]; i.target = self; i.tag = tag; return i;
}
- (void)buildMenu {
    NSMenu* bar = [NSMenu new];
    NSMenu* app = [[NSMenu alloc] initWithTitle:@"Bergamot 3DS"];
    [app addItemWithTitle:@"About Bergamot 3DS" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app addItem:NSMenuItem.separatorItem];
    [app addItem:[self item:@"Settings…" action:@selector(showSettings:) key:@"," tag:0]];
    [app addItem:[self item:@"Show Data Folder" action:@selector(showData:) key:@"" tag:0]];
    [app addItem:NSMenuItem.separatorItem];
    [app addItemWithTitle:@"Hide Bergamot 3DS" action:@selector(hide:) keyEquivalent:@"h"];
    [app addItemWithTitle:@"Quit Bergamot 3DS" action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
    [file addItem:[self item:@"Open Game…" action:@selector(openDocument:) key:@"o" tag:0]];
    NSMenuItem* recent = [[NSMenuItem alloc] initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
    NSMenu* recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
    recentMenu.delegate = self;
    recent.submenu = recentMenu; [file addItem:recent];
    [file addItem:[self item:@"Install Update or DLC (.cia)…" action:@selector(installUpdate:) key:@"" tag:0]];
    [file addItem:[self item:@"Install Keys…" action:@selector(installKeys:) key:@"" tag:0]];
    [file addItem:NSMenuItem.separatorItem];
    [file addItem:[self item:@"Export Shaders…" action:@selector(exportShaders:) key:@"" tag:0]];
    [file addItem:[self item:@"Import Shaders…" action:@selector(importShaders:) key:@"" tag:0]];
    [file addItem:NSMenuItem.separatorItem];
    [file addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    NSMenu* emu = [[NSMenu alloc] initWithTitle:@"Emulation"];
    [emu addItem:[self item:@"Pause" action:@selector(togglePause:) key:@"p" tag:0]];
    [emu addItem:[self item:@"Stop" action:@selector(stopGame:) key:@"." tag:0]];
    [emu addItem:NSMenuItem.separatorItem];
    NSMenuItem* res = [[NSMenuItem alloc] initWithTitle:@"Upscaling" action:nil keyEquivalent:@""];
    NSMenu* resMenu = [[NSMenu alloc] initWithTitle:@"Upscaling"];
    for (NSInteger s = 1; s <= 6; ++s) [resMenu addItem:[self item:[NSString stringWithFormat:@"%ld× (%ldp)", (long)s, (long)s * 240] action:@selector(setScale:) key:@"" tag:s]];
    res.submenu = resMenu; [emu addItem:res];
    [emu addItem:[self item:@"Side-by-Side Screens" action:@selector(toggleLayout:) key:@"l" tag:0]];
    [emu addItem:[self item:@"Queue-Ordered GPU Handoff" action:@selector(toggleHandoff:) key:@"" tag:0]];
    NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
    [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [window addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"].keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    for (NSMenu* m in @[app, file, emu, window]) { NSMenuItem* top = [NSMenuItem new]; top.submenu = m; [bar addItem:top]; }
    NSApp.mainMenu = bar; NSApp.windowsMenu = window;
}
- (void)menuNeedsUpdate:(NSMenu*)menu {
    [menu removeAllItems];
    for (NSURL* u in NSDocumentController.sharedDocumentController.recentDocumentURLs) {
        NSMenuItem* i = [self item:u.lastPathComponent action:@selector(openRecent:) key:@"" tag:0]; i.representedObject = u; [menu addItem:i];
    }
    if (menu.numberOfItems) [menu addItem:NSMenuItem.separatorItem];
    [menu addItem:[self item:@"Clear Menu" action:@selector(clearRecent:) key:@"" tag:0]];
}
- (void)openRecent:(NSMenuItem*)item { [self openGame:item.representedObject]; }
- (void)clearRecent:(id)sender { (void)sender; [NSDocumentController.sharedDocumentController clearRecentDocuments:nil]; }
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(openRecent:)) return !_session.running;
    if (item.action == @selector(setScale:)) { item.state = item.tag == BGPlayerScale(); return !_session.running; }
    if (item.action == @selector(toggleHandoff:)) { item.state = BGQueueOrderedPresent(); return !_session.running; }
    if (item.action == @selector(toggleLayout:)) { item.state = _sideBySide; return YES; }
    if (item.action == @selector(togglePause:)) { item.title = _session.paused ? @"Resume" : @"Pause"; return _session.running; }
    if (item.action == @selector(stopGame:)) return _session.running;
    if (item.action == @selector(installUpdate:)) return !_session.running;
    if (item.action == @selector(importShaders:)) return !_session.running;
    return YES;
}
- (void)openDocument:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    NSMutableArray* types = [NSMutableArray new];
    for (NSString* e in BGGameExtensions()) { UTType* t = [UTType typeWithFilenameExtension:e]; if (t) [types addObject:t]; }
    panel.allowedContentTypes = types; panel.message = @"Choose a 3DS game";
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) { if (r == NSModalResponseOK) [self openGame:panel.URL]; }];
}
- (BOOL)application:(NSApplication*)app openFile:(NSString*)path {
    (void)app; NSURL* url = [NSURL fileURLWithPath:path];
    if ([url.pathExtension.lowercaseString isEqualToString:BGShaderPackExtension]) [self importShaderPack:url];
    else [self openGame:url];
    return YES;
}
- (void)exportShaders:(id)sender {
    (void)sender;
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = @"Bergamot 3DS Shaders.bgshaders";
    UTType* type = [UTType typeWithFilenameExtension:BGShaderPackExtension]; if (type) panel.allowedContentTypes = @[type];
    panel.message = @"Save every game's shaders to bring to another device";
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        BOOL failed = NO; NSString* message = BGExportShaderCache(panel.URL, &failed);
        self->_hint.stringValue = message; if (failed) [self alert:@"Export failed" message:message];
    }];
}
- (void)importShaders:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    NSMutableArray* types = [NSMutableArray new];
    for (NSString* e in @[BGShaderPackExtension, @"vkch"]) { UTType* t = [UTType typeWithFilenameExtension:e]; if (t) [types addObject:t]; }
    panel.allowedContentTypes = types; panel.allowsMultipleSelection = YES;
    panel.message = @"Choose a .bgshaders file, or a game's four .vkch shader cache files";
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        if (panel.URLs.count == 1 && [panel.URL.pathExtension.lowercaseString isEqualToString:BGShaderPackExtension]) { [self importShaderPack:panel.URL]; return; }
        if (self->_session.running) { self->_hint.stringValue = @"Stop the current game first (⌘.)"; NSBeep(); return; }
        BOOL failed = NO; NSString* message = BGImportShaderFiles(panel.URLs, &failed);
        self->_hint.stringValue = message; if (failed) [self alert:@"Import failed" message:message];
    }];
}
- (void)importShaderPack:(NSURL*)url {
    if (!_session) { _pendingOpen = url; return; }
    if (_session.running) { _hint.stringValue = @"Stop the current game first (⌘.)"; NSBeep(); return; }
    BOOL failed = NO; NSString* message = BGImportShaderCache(url, &failed);
    _hint.stringValue = message; if (failed) [self alert:@"Import failed" message:message];
}
- (void)openGame:(NSURL*)url {
    if (!_session) { _pendingOpen = url; return; }   // Finder launch: open after the window exists
    if (_session.running) { _hint.stringValue = @"Stop the current game first (⌘.)"; NSBeep(); return; }
    [NSDocumentController.sharedDocumentController noteNewRecentDocumentURL:url];
    _title = url.URLByDeletingPathExtension.lastPathComponent;
    _window.title = _title; _window.subtitle = @"Loading…"; _empty.hidden = YES; _keys = {};
    [self applySlots];
    BGGameRanThisLaunch = YES;
    [_session run:url];
    // Controller-only play does not count as user activity, and an idle display dim makes macOS
    // withhold drawables for ~1 s mid-game. Hold the display awake for the session, as games do.
    _playing = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated | NSActivityIdleDisplaySleepDisabled
                                                            reason:@"Playing a 3DS game"];
    [self syncSettings];
    [_window makeFirstResponder:_screen];
}
- (void)installUpdate:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    UTType* cia = [UTType typeWithFilenameExtension:@"cia"]; if (cia) panel.allowedContentTypes = @[cia];
    panel.message = @"Choose an update or DLC .cia";
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        self->_hint.stringValue = @"Installing…";
        [self->_session installPackage:panel.URL completion:^(NSString* message, BOOL failed) { self->_hint.stringValue = message; if (failed) [self alert:@"Install failed" message:message]; }];
    }];
}
- (void)installKeys:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = YES;
    panel.message = @"Choose keys.txt or aes_keys.txt, and optionally seeddb.bin, boot9.bin and otp.bin";
    [panel beginSheetModalForWindow:_window completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        NSArray<NSString*>* problems = nil;
        self->_hint.stringValue = BGInstallKeyFiles(panel.URLs, BGGameRanThisLaunch, &problems);
        if (problems.count) [self alert:@"Keys not installed" message:[problems componentsJoinedByString:@"\n"]];
    }];
}
- (void)sessionEnded:(NSString*)message failed:(BOOL)failed {
    if (_playing) { [NSProcessInfo.processInfo endActivity:_playing]; _playing = nil; }
    dispatch_async(dispatch_get_main_queue(), ^{ [self syncSettings]; });
    _empty.hidden = NO; _pausedLabel.hidden = YES; _window.title = @"Bergamot 3DS"; _window.subtitle = @"";
    _hint.stringValue = failed ? @"Could not start that game" : [NSString stringWithFormat:@"%@ stopped. Your save is kept.", _title ?: @"Game"];
    [(CAMetalLayer*)_screen.layer setNeedsDisplay];
    if (failed) [self alert:@"Could not start the game" message:message];
}
- (void)alert:(NSString*)title message:(NSString*)message {
    NSAlert* a = [NSAlert new]; a.messageText = title; a.informativeText = message ?: @""; [a beginSheetModalForWindow:_window completionHandler:nil];
}
- (void)showLoading:(NSString*)message {
    _pausedLabel.stringValue = message ?: @"PAUSED · Esc to resume";
    _pausedLabel.hidden = message == nil;
}
- (void)occlusionChanged:(NSNotification*)n {
    (void)n;
    const BOOL visible = (_window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    [_session setPresenting:visible];
    std::fprintf(stderr, "BG_OCCLUSION visible=%d uptime=%.2f\n", visible ? 1 : 0, NSProcessInfo.processInfo.systemUptime);
}
- (void)togglePause:(id)sender {
    (void)sender; if (!_session.running) return;
    const BOOL paused = !_session.paused; _keys = {}; [_session setPaused:paused];
    _pausedLabel.stringValue = @"PAUSED · Esc to resume"; _pausedLabel.hidden = !paused; if (!paused) [_window makeFirstResponder:_screen];
}
- (void)stopGame:(id)sender { (void)sender; [_session stop]; }
- (void)setScale:(NSMenuItem*)item { [NSUserDefaults.standardUserDefaults setInteger:item.tag forKey:@"BGPlayerScale"]; [self syncSettings]; }
- (void)toggleHandoff:(id)sender { (void)sender; NSUserDefaults* d = NSUserDefaults.standardUserDefaults; [d setBool:!BGQueueOrderedPresent() forKey:@"BGQueueOrderedPresent"]; [self syncSettings]; }
- (void)toggleLayout:(id)sender {
    (void)sender; _sideBySide = !_sideBySide; [NSUserDefaults.standardUserDefaults setBool:_sideBySide forKey:@"BGPlayerSideBySide"];
    [self applySlots];
    NSRect f = _window.frame; NSSize content = _sideBySide ? NSMakeSize(960, 480) : NSMakeSize(600, 720);
    NSRect target = [_window frameRectForContentRect:NSMakeRect(0, 0, content.width, content.height)];
    f.origin.y += f.size.height - target.size.height; f.size = target.size; [_window setFrame:f display:YES animate:YES];
    [self syncSettings];
}
// A standard Settings window. Resolution is chosen before a game starts and applies at launch.
// Standard Settings window. Every option here is read when a game starts.
- (NSPopUpButton*)popup:(NSArray<NSString*>*)titles action:(SEL)action {
    NSPopUpButton* p = [NSPopUpButton new]; [p addItemsWithTitles:titles]; p.target = self; p.action = action; return p;
}
- (void)showSettings:(id)sender {
    (void)sender;
    if (!_settings) {
        _settings = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 480, 230) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        _settings.title = @"Settings"; _settings.releasedWhenClosed = NO;
        _scalePopup = [self popup:@[@"1× Native (400×240)", @"2× (800×480)", @"3× (1200×720)", @"4× (1600×960)", @"5× (2000×1200)", @"6× (2400×1440)"] action:@selector(scalePicked:)];
        _layoutPopup = [self popup:@[@"Stacked", @"Side by side"] action:@selector(layoutPicked:)];
        _handoffPopup = [self popup:@[@"Sync each frame", @"Queue-ordered (default)"] action:@selector(handoffPicked:)];
        _shaderPopup = [self popup:@[@"Prepare cached shaders (recommended)", @"Also compile new shaders in background"] action:@selector(shadersPicked:)];
        NSGridView* grid = [NSGridView gridViewWithViews:@[
            @[[NSTextField labelWithString:@"Upscaling:"], _scalePopup],
            @[[NSTextField labelWithString:@"Screen layout:"], _layoutPopup],
            @[[NSTextField labelWithString:@"GPU handoff:"], _handoffPopup],
            @[[NSTextField labelWithString:@"Shaders:"], _shaderPopup]]];
        grid.rowSpacing = 10; grid.columnSpacing = 10;
        [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        _scaleNote = [NSTextField wrappingLabelWithString:@""]; _scaleNote.textColor = NSColor.secondaryLabelColor; _scaleNote.font = [NSFont systemFontOfSize:11];
        NSStackView* stack = [NSStackView stackViewWithViews:@[grid, _scaleNote]];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical; stack.alignment = NSLayoutAttributeLeading; stack.spacing = 14;
        stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20); stack.translatesAutoresizingMaskIntoConstraints = NO;
        [_settings.contentView addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:_settings.contentView.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:_settings.contentView.trailingAnchor], [stack.topAnchor constraintEqualToAnchor:_settings.contentView.topAnchor],
            [stack.bottomAnchor constraintEqualToAnchor:_settings.contentView.bottomAnchor], [_scaleNote.widthAnchor constraintEqualToConstant:440]]];
        [_settings center];
    }
    [self syncSettings];
    [_settings makeKeyAndOrderFront:nil];
}
- (void)syncSettings {
    if (!_settings) return;
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    [_scalePopup selectItemAtIndex:BGPlayerScale() - 1];
    [_layoutPopup selectItemAtIndex:_sideBySide ? 1 : 0];
    [_handoffPopup selectItemAtIndex:BGQueueOrderedPresent() ? 1 : 0];
    [_shaderPopup selectItemAtIndex:[d boolForKey:@"BGAsyncShaders"] ? 1 : 0];
    const BOOL idle = !_session.running;
    _scalePopup.enabled = _handoffPopup.enabled = _shaderPopup.enabled = idle;   // read at game start
    _scaleNote.stringValue = idle
        ? @"Native (1×) is the 3DS's own resolution and runs best; more upscaling looks sharper and needs more GPU. "
          @"Background shader compiling avoids a pause the first time an effect appears, but can briefly draw scenes with pieces missing."
        : @"Stop the game to change upscaling, GPU handoff or shaders. Screen layout can change any time.";
}
- (void)scalePicked:(NSPopUpButton*)popup { [NSUserDefaults.standardUserDefaults setInteger:popup.indexOfSelectedItem + 1 forKey:@"BGPlayerScale"]; }
- (void)layoutPicked:(NSPopUpButton*)popup { if ((popup.indexOfSelectedItem == 1) != _sideBySide) [self toggleLayout:nil]; }
- (void)handoffPicked:(NSPopUpButton*)popup { [NSUserDefaults.standardUserDefaults setBool:popup.indexOfSelectedItem == 1 forKey:@"BGQueueOrderedPresent"]; }
- (void)shadersPicked:(NSPopUpButton*)popup { [NSUserDefaults.standardUserDefaults setBool:popup.indexOfSelectedItem == 1 forKey:@"BGAsyncShaders"]; }
- (void)showData:(id)sender { (void)sender; [NSWorkspace.sharedWorkspace openURL:BGPlayerDataRoot()]; }
- (void)key:(unsigned short)key down:(BOOL)down {
    if (key == 53) { if (down) [self togglePause:nil]; return; }   // Esc
    static const std::map<unsigned short, unsigned> buttons = {
        {6, 8}, {7, 0}, {0, 1}, {1, 9},             // Z=A  X=B  A=Y  S=X
        {12, 10}, {13, 11}, {18, 12}, {19, 13},     // Q=L  W=R  1=ZL 2=ZR
        {36, 3}, {48, 2},                           // Return=Start  Tab=Select
        {34, 4}, {40, 5}, {38, 6}, {37, 7}};        // I/K/J/L = D-pad
    if (auto b = buttons.find(key); b != buttons.end()) {
        if (down) _keys.buttons |= 1u << b->second; else _keys.buttons &= ~(1u << b->second);
    } else {
        static const std::map<unsigned short, std::pair<int, int>> stick = {{126, {0, -1}}, {125, {0, 1}}, {123, {-1, 0}}, {124, {1, 0}}};
        auto s = stick.find(key); if (s == stick.end()) return;
        static unsigned held = 0; const unsigned bit = 1u << (key - 123);
        held = down ? held | bit : held & ~bit;
        int x = 0, y = 0; for (auto& [k, v] : stick) if (held & (1u << (k - 123))) { x += v.first; y += v.second; }
        const double len = std::hypot(x, y) ?: 1;
        _keys.lx = static_cast<int16_t>(x / len * 32767); _keys.ly = static_cast<int16_t>(y / len * 32767);
    }
    if (_session.running && !_session.paused) [_session setInput:_keys from:BGInputSourceKeyboard];
}
- (void)pointer:(CGPoint)p down:(BOOL)down {
    if (!_session.running || _session.paused) return;
    auto point = ctr::map_bottom_touch(ctr::fit_dual_screens([self slots], _screen.bounds.size.width, _screen.bounds.size.height), p.x, p.y);
    CTRSkinInput in; in.touching = down && point.has_value(); if (point) { in.tx = point->x; in.ty = point->y; }
    [_session setInput:in from:BGInputSourceScreen];
}
- (void)applicationDidResignActive:(NSNotification*)n { (void)n; _keys = {}; if (_session.running) [_session setInput:{} from:BGInputSourceKeyboard]; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
    (void)app; if (!_session.running) return NSTerminateNow;
    __weak BGMacPlayer* weakSelf = self;
    _session.ended = ^(NSString*, BOOL) { (void)weakSelf; [NSApp replyToApplicationShouldTerminate:YES]; };
    [_session stop]; return NSTerminateLater;   // unload cleanly so the save is flushed
}
@end

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication* app = NSApplication.sharedApplication;
        BGMacPlayer* delegate = [BGMacPlayer new]; app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
