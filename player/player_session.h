// SPDX-License-Identifier: GPL-3.0-or-later
// Bergamot 3DS player: one emulation session shared by the iOS and Mac apps.
// Lifted from the Bergamot app's 3DS run loop; no library, Fuitlet or DS/GBA.
#pragma once
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#endif
#include <sys/sysctl.h>
#include "runtime.hpp"
#include "metal_presenter.h"
#include "display_layout.hpp"
#include "frame_pacer.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unistd.h>
#if TARGET_OS_OSX
#include <crt_externs.h>
#include <spawn.h>
#endif

#ifndef BG_SKIN_INPUT_DEFINED
#define BG_SKIN_INPUT_DEFINED
// Same contract as the Bergamot app's touch controls.
struct CTRSkinInput {
    unsigned buttons = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0, tx = 0, ty = 0;
    bool touching = false;
};
#endif

enum BGInputSource : NSInteger { BGInputSourceScreen = 0, BGInputSourceKeyboard = 1, BGInputSourceController = 2, BGInputSourceCount = 3 };

static NSURL* BGPlayerDocuments() {
#if TARGET_OS_IOS
    return [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
#else
    NSURL* u = [[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject
                URLByAppendingPathComponent:@"Bergamot 3DS"];
    [NSFileManager.defaultManager createDirectoryAtURL:u withIntermediateDirectories:YES attributes:nil error:nil];
    return u;
#endif
}
// Runtime root: Azahar's system/ and saves/ live below it.
static NSURL* BGPlayerDataRoot() {
    NSURL* u = [BGPlayerDocuments() URLByAppendingPathComponent:@"Data"];
    [NSFileManager.defaultManager createDirectoryAtURL:u withIntermediateDirectories:YES attributes:nil error:nil];
    return u;
}
// Device identifier such as "iPhone16,1"; the Simulator reports the model it simulates.
static NSString* BGDeviceModel() {
    if (NSString* sim = NSProcessInfo.processInfo.environment[@"SIMULATOR_MODEL_IDENTIFIER"]) return sim;
    char machine[64] = {}; size_t size = sizeof(machine);
    return sysctlbyname("hw.machine", machine, &size, nullptr, 0) == 0 ? [NSString stringWithUTF8String:machine] : @"";
}
// Highest upscale this chip is expected to sustain: A17 Pro and newer 3x, A15/A16 2x, older 1x.
// Only the iPhone 15 Pro (iPhone16,1) has been measured; the other tiers are conservative estimates.
static NSInteger BGDeviceScaleCap(NSString* model) {
    NSScanner* scan = [NSScanner scannerWithString:model]; NSString* family = nil; NSInteger major = 0;
    [scan scanCharactersFromSet:NSCharacterSet.letterCharacterSet intoString:&family]; [scan scanInteger:&major];
    if ([family isEqualToString:@"iPhone"]) return major >= 16 ? 3 : major >= 14 ? 2 : 1;   // iPhone16,x = A17 Pro / iPhone 15 Pro
    if ([family isEqualToString:@"iPad"]) return major >= 13 ? 2 : 1;                       // iPad13,x+ = M1 and newer
    return 1;
}
// AUTO upscaling: render at the whole-number scale nearest the top screen's physical pixel width
// (400 px per 1x), then let the presenter's linear filter fit it. 1179 px (iPhone 15 Pro) -> 3x.
static NSInteger BGAutoScale(NSInteger* pixels = nullptr) {
#if TARGET_OS_IOS
    const CGSize native = UIScreen.mainScreen.nativeBounds.size;
    const NSInteger px = (NSInteger)MIN(native.width, native.height);
    if (pixels) *pixels = px;
    return MAX(1, MIN(BGDeviceScaleCap(BGDeviceModel()), (NSInteger)lround(px / 400.0)));
#else
    if (pixels) *pixels = 0;
    return 1;
#endif
}
// Internal resolution. Stored 1-4 is a fixed choice; 0 or unset means AUTO (native 1x on Mac).
static NSInteger BGPlayerScale() {
    const NSInteger s = [NSUserDefaults.standardUserDefaults integerForKey:@"BGPlayerScale"];
    return s >= 1 && s <= 4 ? s : BGAutoScale();
}

// Installed base-game CIAs, keyed by source path. An entry is reused only while the source
// file's size and modification date and the installed content file are all unchanged, so
// replacing or editing the .cia triggers a fresh install.
static NSURL* BGInstallIndexURL() { return [BGPlayerDataRoot() URLByAppendingPathComponent:@"installed-titles.plist"]; }
static NSString* BGSourceKey(NSURL* source) {
    NSDictionary* a = [NSFileManager.defaultManager attributesOfItemAtPath:source.path error:nil];
    return [NSString stringWithFormat:@"%@|%llu|%.0f", source.path.stringByStandardizingPath, a.fileSize, a.fileModificationDate.timeIntervalSince1970];
}
static NSString* BGInstalledContent(NSURL* source) {
    NSDictionary* index = [NSDictionary dictionaryWithContentsOfURL:BGInstallIndexURL()];
    NSString* path = index[BGSourceKey(source)];
    return path && [NSFileManager.defaultManager fileExistsAtPath:path] ? path : nil;
}
static void BGRememberInstall(NSURL* source, NSString* content) {
    NSMutableDictionary* index = [[NSDictionary dictionaryWithContentsOfURL:BGInstallIndexURL()] mutableCopy] ?: [NSMutableDictionary new];
    for (NSString* key in index.allKeys) if ([key hasPrefix:[source.path.stringByStandardizingPath stringByAppendingString:@"|"]]) [index removeObjectForKey:key];
    index[BGSourceKey(source)] = content;
    [index writeToURL:BGInstallIndexURL() error:nil];
}

// Map an extended gamepad positionally onto 3DS buttons (bottom face = B, right = A).
static CTRSkinInput BGControllerInput(GCExtendedGamepad* g) {
    CTRSkinInput in;
    auto set = [&](bool on, unsigned bit) { if (on) in.buttons |= 1u << bit; };
    set(g.buttonA.pressed, 0); set(g.buttonB.pressed, 8); set(g.buttonX.pressed, 1); set(g.buttonY.pressed, 9);
    set(g.leftShoulder.pressed, 10); set(g.rightShoulder.pressed, 11);
    set(g.leftTrigger.pressed, 12); set(g.rightTrigger.pressed, 13);
    set(g.dpad.up.pressed, 4); set(g.dpad.down.pressed, 5); set(g.dpad.left.pressed, 6); set(g.dpad.right.pressed, 7);
    set(g.buttonMenu.pressed, 3); set(g.buttonOptions.pressed, 2);
    auto axis = [](float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -1.f, 1.f) * 32767)); };
    in.lx = axis(g.leftThumbstick.xAxis.value); in.ly = axis(-g.leftThumbstick.yAxis.value);
    in.rx = axis(g.rightThumbstick.xAxis.value); in.ry = axis(-g.rightThumbstick.yAxis.value);
    return in;
}

@interface BG3DSSession : NSObject
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) BOOL paused;
@property(nonatomic, copy) void (^statusChanged)(NSString* status);        // main queue
@property(nonatomic, copy) void (^ended)(NSString* message, BOOL failed);   // main queue
@property(nonatomic, copy) void (^controllerMenu)(void);                    // main queue
@property(nonatomic, copy) void (^loadingChanged)(NSString* message);       // main queue; nil when play starts
- (instancetype)initWithLayer:(CAMetalLayer*)layer;
- (void)setTopSlot:(CGRect)top bottomSlot:(CGRect)bottom;
- (void)run:(NSURL*)game;
- (void)setInput:(CTRSkinInput)input from:(BGInputSource)source;
- (void)setPaused:(BOOL)paused;
// When NO, frames are still emulated (game, audio, timing continue) but not drawn. Set it while the
// window is fully covered: macOS throttles drawables for hidden windows, which otherwise stalls play.
- (void)setPresenting:(BOOL)presenting;
- (void)stop;
// Installs an update or DLC CIA into the data root, decrypting it with the user's keys if needed.
// No game may be running.
- (void)installPackage:(NSURL*)package completion:(void (^)(NSString* message, BOOL failed))completion;
@end

@implementation BG3DSSession {
    CAMetalLayer* _layer;
    std::mutex _mutex;               // guards _runtime, _inputs, _slots
    ctr::Runtime* _runtime;          // non-owning; lives on the worker
    CTRSkinInput _inputs[BGInputSourceCount];
    ctr::DualScreenLayout _slots;
    std::atomic_bool _stop, _pausedFlag, _presenting;
    BOOL _running;
    AVAudioEngine* _engine; AVAudioPlayerNode* _player; AVAudioFormat* _format;
    id _connectObserver;
}
- (instancetype)initWithLayer:(CAMetalLayer*)layer {
    if ((self = [super init])) {
        _layer = layer; _runtime = nullptr; _stop = false; _pausedFlag = false; _presenting = true;
        _slots = {{.03, .03, .94, .45}, {.15, .52, .70, .45}};
        __weak BG3DSSession* weakSelf = self;
        _connectObserver = [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* n) { [weakSelf attachController:n.object]; }];
        for (GCController* c in GCController.controllers) [self attachController:c];
    }
    return self;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:_connectObserver]; }
- (void)attachController:(GCController*)controller {
    GCExtendedGamepad* pad = controller.extendedGamepad;
    if (!pad) return;
    __weak BG3DSSession* weakSelf = self;
    pad.valueChangedHandler = ^(GCExtendedGamepad* g, GCControllerElement* element) {
        BG3DSSession* s = weakSelf; if (!s) return;
        if (element == g.buttonHome && g.buttonHome.pressed) { if (s.controllerMenu) dispatch_async(dispatch_get_main_queue(), s.controllerMenu); return; }
        [s setInput:BGControllerInput(g) from:BGInputSourceController];
    };
}
- (BOOL)running { return _running; }
- (BOOL)paused { return _pausedFlag; }
- (void)setTopSlot:(CGRect)top bottomSlot:(CGRect)bottom {
    ctr::DualScreenLayout s{{top.origin.x, top.origin.y, top.size.width, top.size.height},
                            {bottom.origin.x, bottom.origin.y, bottom.size.width, bottom.size.height}};
    if (!ctr::valid_slot(s.top) || !ctr::valid_slot(s.bottom)) return;
    const std::lock_guard lock(_mutex); _slots = s;
}
// Merge every source: buttons are OR-ed, each analog axis takes the strongest deflection.
- (void)pushInputLocked {
    if (!_runtime) return;
    CTRSkinInput merged;
    auto strongest = [](int16_t a, int16_t b) { return std::abs(b) > std::abs(a) ? b : a; };
    if (!_pausedFlag) for (const auto& in : _inputs) {
        merged.buttons |= in.buttons;
        merged.lx = strongest(merged.lx, in.lx); merged.ly = strongest(merged.ly, in.ly);
        merged.rx = strongest(merged.rx, in.rx); merged.ry = strongest(merged.ry, in.ry);
        if (in.touching && !merged.touching) { merged.tx = in.tx; merged.ty = in.ty; merged.touching = true; }
    }
    for (unsigned i = 0; i < 16; ++i) _runtime->set_button(i, (merged.buttons >> i) & 1);
    _runtime->set_circle_pad(merged.lx, merged.ly);
    _runtime->set_c_stick(merged.rx, merged.ry);
    _runtime->set_touch(merged.tx, merged.ty, merged.touching);
}
- (void)setInput:(CTRSkinInput)input from:(BGInputSource)source {
    const std::lock_guard lock(_mutex); _inputs[source] = input; [self pushInputLocked];
}
- (void)setPaused:(BOOL)paused {
    if (!_running) return;
    const std::lock_guard lock(_mutex);
    if (paused) for (auto& in : _inputs) in = {};   // never leave a button held across a pause
    _pausedFlag = paused; [self pushInputLocked];
}
- (void)stop { [self setPaused:NO]; _stop = true; }
- (void)setPresenting:(BOOL)presenting { _presenting = presenting; }
- (void)reportLoading:(NSString*)message {
    dispatch_async(dispatch_get_main_queue(), ^{ if (self.loadingChanged) self.loadingChanged(message); });
}
- (void)finish:(NSString*)message failed:(BOOL)failed {
    std::fprintf(stderr, "BG_SESSION_END failed=%d message=%s\n", failed ? 1 : 0, message.UTF8String ?: "");
    dispatch_async(dispatch_get_main_queue(), ^{ self->_running = NO; if (self.ended) self.ended(message, failed); });
}
- (void)installPackage:(NSURL*)package completion:(void (^)(NSString*, BOOL))completion {
    if (_running) { completion(@"Stop the game before installing an update.", YES); return; }
    const std::string root = BGPlayerDataRoot().fileSystemRepresentation;
    std::thread([package, root, completion] {
        NSString* message = nil; BOOL failed = NO;
        const BOOL scoped = [package startAccessingSecurityScopedResource];
        try {
            ctr::Runtime runtime(root);
            const auto info = runtime.inspect(package.fileSystemRepresentation);
            (void)info;   // encrypted CIAs are decrypted during install with the user's keys (patch 0011)
            runtime.install_package(package.fileSystemRepresentation);
            message = [NSString stringWithFormat:@"Installed %@", package.lastPathComponent];
        } catch (const std::exception& e) { message = [NSString stringWithUTF8String:e.what()]; failed = YES; }
        if (scoped) [package stopAccessingSecurityScopedResource];
        dispatch_async(dispatch_get_main_queue(), ^{ completion(message, failed); });
    }).detach();
}
- (void)run:(NSURL*)game {
    if (_running) return;
    _running = YES; _stop = false; _pausedFlag = false;
    { const std::lock_guard lock(_mutex); for (auto& in : _inputs) in = {}; }
    const unsigned scale = static_cast<unsigned>(BGPlayerScale());
    const bool queueOrdered = [NSUserDefaults.standardUserDefaults boolForKey:@"BGQueueOrderedPresent"];
    const std::string root = BGPlayerDataRoot().fileSystemRepresentation;
    CAMetalLayer* layer = _layer;
    std::thread([self, game, scale, queueOrdered, root, layer] {
        @autoreleasepool {
            const BOOL scoped = [game startAccessingSecurityScopedResource];
            NSString* result = @"Stopped"; BOOL failed = NO;
            try {
                ctr::Runtime runtime(root, true, scale);
                // Default: prepared shader cache, synchronous compiles for anything new (accurate, no
                // pop-in). Async compiling was measured with a prepared cache: no fewer stalls, and it
                // can draw incomplete scenes, so it stays opt-in (BGAsyncShaders / BG_ASYNC_SHADERS=1).
                const char* asyncEnv = getenv("BG_ASYNC_SHADERS");
                const bool asyncShaders = asyncEnv ? atoi(asyncEnv) != 0 : [NSUserDefaults.standardUserDefaults boolForKey:@"BGAsyncShaders"];
                runtime.set_option("citra_async_shader_compilation", asyncShaders ? "enabled" : "disabled");
                CTRMetalPresenter* presenter = nil;
                runtime.set_gpu_async_presenter([self, layer, &presenter, &runtime, queueOrdered](VkDevice device, VkImage image, unsigned, unsigned) -> ctr::VulkanBackend::Completion {
                    if (!self->_presenting) return {};   // hidden window: keep emulating, skip the draw
                    id<MTLTexture> texture = CTRExportTexture(device, image);
                    if (!presenter) presenter = queueOrdered
                        ? [[CTRMetalPresenter alloc] initWithCommandQueue:CTRExportCommandQueue(device, runtime.gpu_queue())]
                        : [[CTRMetalPresenter alloc] initWithDevice:texture.device];
                    ctr::DualScreenLayout s; { const std::lock_guard lock(self->_mutex); s = self->_slots; }
                    [presenter setTopScreenSlot:CGRectMake(s.top.x, s.top.y, s.top.width, s.top.height)
                               bottomScreenSlot:CGRectMake(s.bottom.x, s.bottom.y, s.bottom.width, s.bottom.height)];
                    if (queueOrdered) {
                        id<MTLCommandBuffer> command = [presenter enqueuePresent:texture on:layer];
                        return [command] { CTRWaitForPresentation(command); };
                    }
                    [presenter present:texture on:layer];
                    return {};
                }, queueOrdered ? ctr::Runtime::HandoffSync::QueueOrdered : ctr::Runtime::HandoffSync::Serialized);
                const auto tStart = std::chrono::steady_clock::now();
                auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
                std::filesystem::path launch(game.fileSystemRepresentation);
                if ([game.pathExtension.lowercaseString isEqualToString:@"cia"]) {
                    const auto info = runtime.inspect(launch);
                    if ((info.title_id >> 32) != 0x00040000) throw std::runtime_error("This CIA is an update or DLC. Install it with Install Update, then open the base game.");
                    if (NSString* installed = BGInstalledContent(game)) {
                        launch = installed.fileSystemRepresentation;      // already installed: skip the copy
                    } else {
                        launch = runtime.install_package(launch);
                        BGRememberInstall(game, [NSString stringWithUTF8String:launch.c_str()]);
                    }
                }
                const auto tInstalled = std::chrono::steady_clock::now();
                // "Preparing shaders": build everything this title's cache knows before frame one,
                // so effects seen before never compile mid-game. Progress is throttled to ~10 Hz.
                auto lastReport = std::make_shared<std::chrono::steady_clock::time_point>();
                runtime.set_shader_progress([self, lastReport](int stage, std::size_t current, std::size_t total) {
                    const auto now = std::chrono::steady_clock::now();
                    if (stage != 3 || total == 0 || now - *lastReport < std::chrono::milliseconds(100)) return;
                    *lastReport = now;
                    [self reportLoading:[NSString stringWithFormat:@"Preparing shaders %zu%%", current * 100 / total]];
                });
                [self reportLoading:@"Loading…"];
                runtime.load(launch);
                const auto tLoaded = std::chrono::steady_clock::now();
                [self reportLoading:@"Preparing shaders…"];
                runtime.wait_for_shader_cache();
                const auto tPrepared = std::chrono::steady_clock::now();
                [self reportLoading:nil];
                std::fprintf(stderr, "BG_TIMING install_ms=%.0f load_ms=%.0f shader_wait_ms=%.0f\n", ms(tStart, tInstalled), ms(tInstalled, tLoaded), ms(tLoaded, tPrepared));
                { const std::lock_guard lock(self->_mutex); self->_runtime = &runtime; [self pushInputLocked]; }
                struct Clear { BG3DSSession* s; ~Clear() { const std::lock_guard lock(s->_mutex); s->_runtime = nullptr; } } clear{self};
                const auto av = runtime.av_info();
                if (!std::isfinite(av.timing.fps) || av.timing.fps <= 0) throw std::runtime_error("Core reported an invalid frame rate");
                const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / av.timing.fps));
                [self startAudio:av.timing.sample_rate];
                auto pending = std::make_shared<std::vector<int16_t>>(); pending->reserve(4096);
                auto queued = std::make_shared<std::atomic_uint64_t>(0);
                const auto queueLimit = static_cast<std::uint64_t>(av.timing.sample_rate * 0.12);
                AVAudioPlayerNode* player = self->_player; AVAudioFormat* format = self->_format;
                runtime.set_audio_sink([player, format, pending, queued, queueLimit](const int16_t* samples, std::size_t count) {
                    if (!player || !count) return;
                    pending->insert(pending->end(), samples, samples + count * 2);
                    constexpr std::size_t batch = 512; std::size_t offset = 0;   // ~15.6 ms at 32,728 Hz
                    while (pending->size() - offset >= batch * 2) {
                        if (queued->load() + batch <= queueLimit) {
                            AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:batch];
                            buffer.frameLength = batch;
                            for (std::size_t i = 0; i < batch; ++i) {
                                buffer.floatChannelData[0][i] = (*pending)[offset + i * 2] / 32768.0f;
                                buffer.floatChannelData[1][i] = (*pending)[offset + i * 2 + 1] / 32768.0f;
                            }
                            *queued += batch;
                            [player scheduleBuffer:buffer completionHandler:^{ *queued -= batch; }];
                        }
                        offset += batch * 2;
                    }
                    if (offset) pending->erase(pending->begin(), pending->begin() + offset);
                });
                auto deadline = std::chrono::steady_clock::now();
                ctr::FramePacer pacer{period, deadline};
                auto nextSample = deadline + std::chrono::seconds(1);
                // Test hook: BG_AUTO_A=<frames> taps A for 6 frames every <frames> frames, to walk
                // through intros in timed benchmarks. Uses the controller input slot.
                const long autoA = getenv("BG_AUTO_A") ? atol(getenv("BG_AUTO_A")) : 0;
                // Diagnostic: BG_SAMPLE_STALLS=<dir> spawns /usr/bin/sample on this process when a
                // single frame has run longer than 250 ms, capturing what the core is doing mid-stall.
                auto stepBegan = std::make_shared<std::atomic<std::int64_t>>(0);
                auto watching = std::make_shared<std::atomic_bool>(true);
                std::thread watchdog;
#if TARGET_OS_OSX   // /usr/bin/sample exists only on macOS
                if (const char* dir = getenv("BG_SAMPLE_STALLS")) {
                    const long long thresholdNs = (getenv("BG_SAMPLE_MS") ? atoll(getenv("BG_SAMPLE_MS")) : 250) * 1'000'000LL;
                    watchdog = std::thread([stepBegan, watching, thresholdNs, out = std::string(dir)] {
                        std::int64_t sampledFor = -1; int n = 0;
                        while (*watching) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(25));
                            const auto began = stepBegan->load();
                            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                            if (began && began != sampledFor && now - began > thresholdNs && n < 12) {
                                sampledFor = began;
                                const std::string pid = std::to_string(getpid()), file = out + "/stall-" + std::to_string(n++) + ".txt";
                                const char* argv[] = {"/usr/bin/sample", pid.c_str(), "1", "1", "-mayDie", "-file", file.c_str(), nullptr};
                                pid_t child; posix_spawn(&child, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), *_NSGetEnviron());
                                std::fprintf(stderr, "BG_STALL_SAMPLE %s at_ms=%.0f\n", file.c_str(), (now - began) / 1e6);
                            }
                        }
                    });
                }
#endif
                struct StopWatchdog { std::shared_ptr<std::atomic_bool> w; std::thread& t; ~StopWatchdog() { *w = false; if (t.joinable()) t.join(); } } stopWatchdog{watching, watchdog};
                while (!self->_stop && !runtime.stopped()) {
                    if (self->_pausedFlag) {
                        runtime.drain_presentation(); [player pause];
                        while (self->_pausedFlag && !self->_stop) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        if (self->_stop) break;
                        (void)runtime.performance();
                        deadline = std::chrono::steady_clock::now(); pacer.resume(deadline); nextSample = deadline + std::chrono::seconds(1);
                        [player play];
                    }
                    if (autoA > 0) {
                        CTRSkinInput tap; if (runtime.frames() % autoA < 6) tap.buttons = 1u << 8;
                        [self setInput:tap from:BGInputSourceController];
                    }
                    const auto stepStart = std::chrono::steady_clock::now();
                    const auto before = runtime.handoff_stats();
                    stepBegan->store(stepStart.time_since_epoch().count());
                    @autoreleasepool { runtime.step(); }
                    stepBegan->store(0);
                    if (const double stepMs = ms(stepStart, std::chrono::steady_clock::now()); stepMs > 40) {
                        // Split a long frame: GPU fence wait, presenter (Metal encode + drawable),
                        // and the rest, which is core CPU work including any synchronous shader compile.
                        const auto after = runtime.handoff_stats();
                        const double gpu = after.producer_wait_ms - before.producer_wait_ms + after.consumer_wait_ms - before.consumer_wait_ms;
                        const double present = after.presenter_ms - before.presenter_ms;
                        std::fprintf(stderr, "BG_LONG_STEP frame=%llu ms=%.1f t=%.2f gpu_wait=%.1f present=%.1f core=%.1f uptime=%.2f\n",
                                     (unsigned long long)runtime.frames(), stepMs, ms(tStart, stepStart) / 1000, gpu, present, stepMs - gpu - present,
                                     NSProcessInfo.processInfo.systemUptime);
                    }
                    std::this_thread::sleep_until(pacer.advance(std::chrono::steady_clock::now()));
                    if (std::chrono::steady_clock::now() >= nextSample) {
                        const auto stats = runtime.performance();
                        NSString* status = [NSString stringWithFormat:@"%.0f FPS · %.0f%%", stats.game_fps, stats.emulation_speed * 100];
                        const auto h = runtime.handoff_stats();
                        std::fprintf(stderr, "BG_STATUS game_fps=%.1f speed=%.1f handoff_frames=%llu presenter_ms=%.1f producer_ms=%.1f\n",
                                     stats.game_fps, stats.emulation_speed * 100, (unsigned long long)h.frames, h.presenter_ms, h.producer_wait_ms);
                        dispatch_async(dispatch_get_main_queue(), ^{ if (self->_running && self.statusChanged) self.statusChanged(status); });
                        nextSample += std::chrono::seconds(1);
                    }
                }
                runtime.unload();
            } catch (const std::exception& e) { result = [NSString stringWithUTF8String:e.what()]; failed = YES; }
            [self->_player stop]; [self->_engine stop]; self->_player = nil; self->_engine = nil;
            if (scoped) [game stopAccessingSecurityScopedResource];
            [self finish:result failed:failed];
        }
    }).detach();
}
- (void)startAudio:(double)rate {
    _format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate channels:2];
    _engine = [AVAudioEngine new]; _player = [AVAudioPlayerNode new];
    [_engine attachNode:_player];
    [_engine connect:_player to:_engine.mainMixerNode format:_format];
#if TARGET_OS_IOS
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback error:nil];
    [AVAudioSession.sharedInstance setActive:YES error:nil];
#endif
    if ([_engine startAndReturnError:nil]) [_player play]; else { _player = nil; }
}
@end
