// SPDX-License-Identifier: GPL-3.0-or-later
// Bergamot 3DS for iPhone and iPad: the Bergamot app's 3DS screen and controls,
// without its library, Fuitlet habitat or DS/GBA cores.
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "player_session.h"

// Minimal stand-ins for the Bergamot front end helpers that pixel_controls.h uses.
static UIColor* BGColor(NSString* hex) { unsigned v = 0; [[NSScanner scannerWithString:hex] scanHexInt:&v]; return [UIColor colorWithRed:((v >> 16) & 255) / 255.0 green:((v >> 8) & 255) / 255.0 blue:(v & 255) / 255.0 alpha:1]; }
static UIImage* BGAsset(NSString* name, NSInteger frame) {
    static NSCache* cache; static dispatch_once_t once; dispatch_once(&once, ^{ cache = [NSCache new]; });
    NSString* key = [NSString stringWithFormat:@"default-%@-%ld", name, (long)frame];
    UIImage* im = [cache objectForKey:key];
    if (!im) { im = [UIImage imageWithContentsOfFile:[NSBundle.mainBundle pathForResource:key ofType:@"png" inDirectory:@"PixelArt"]]; if (im) [cache setObject:im forKey:key]; }
    return im;
}
static void BGNearest(UIView* v) { v.layer.magnificationFilter = kCAFilterNearest; v.layer.minificationFilter = kCAFilterNearest; }
#import "pixel_controls.h"

static NSArray<NSString*>* BGGameExtensions() { return @[@"3ds", @"cci", @"cxi", @"cia", @"3dsx"]; }
static NSURL* BGGamesFolder() {
    NSURL* u = [BGPlayerDocuments() URLByAppendingPathComponent:@"Games"];
    [NSFileManager.defaultManager createDirectoryAtURL:u withIntermediateDirectories:YES attributes:nil error:nil];
    return u;
}
static NSArray<UTType*>* BGTypes(NSArray<NSString*>* exts) {
    NSMutableArray* t = [NSMutableArray new];
    for (NSString* e in exts) { UTType* u = [UTType typeWithFilenameExtension:e]; if (u) [t addObject:u]; }
    [t addObject:UTTypeData]; return t;
}
static void BGAlert(UIViewController* vc, NSString* title, NSString* message) {
    UIAlertController* a = [UIAlertController alertControllerWithTitle:title message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [vc presentViewController:a animated:YES completion:nil];
}

@interface BGMetalView : UIView
@end
@implementation BGMetalView {
    CADisplayLink* _link;
}
+ (Class)layerClass { return CAMetalLayer.class; }
- (instancetype)init {
    if ((self = [super init])) {
        CAMetalLayer* l = (CAMetalLayer*)self.layer;
        l.device = MTLCreateSystemDefaultDevice(); l.pixelFormat = MTLPixelFormatBGRA8Unorm; l.framebufferOnly = YES;
        l.maximumDrawableCount = 3;   // the third drawable is what keeps iPhone at full speed
        l.opaque = NO; self.opaque = NO; self.backgroundColor = UIColor.clearColor; self.userInteractionEnabled = NO;
    }
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CAMetalLayer* l = (CAMetalLayer*)self.layer; const CGFloat s = self.window.screen.scale ?: 3;
    l.contentsScale = s; l.drawableSize = CGSizeMake(self.bounds.size.width * s, self.bounds.size.height * s);
}
- (void)didMoveToWindow {
    [super didMoveToWindow]; [_link invalidate]; _link = nil;
    if (self.window) {   // tell ProMotion we present at 60 Hz
        _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
        _link.preferredFrameRateRange = CAFrameRateRangeMake(60, 60, 60);
        [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
}
- (void)tick:(CADisplayLink*)l { (void)l; }
@end

@interface BGPlayController : UIViewController
- (instancetype)initWithGame:(NSURL*)game;
@property(nonatomic, copy) void (^finished)(NSString* message, BOOL failed);
@end
@implementation BGPlayController {
    NSURL* _game; UIView* _canvas; BGMetalView* _screen; BGPixelControls* _controls; BG3DSSession* _session;
    UILabel* _status; UILabel* _loading; BOOL _menuOpen;
}
- (instancetype)initWithGame:(NSURL*)game { if ((self = [super init])) { _game = game; self.modalPresentationStyle = UIModalPresentationFullScreen; } return self; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (void)viewDidLoad {
    [super viewDidLoad];
    UIImage* grid = BGAsset(@"background", 0);
    self.view.backgroundColor = grid ? [UIColor colorWithPatternImage:grid] : BGColor(@"141B25");
    _canvas = [UIView new]; [self.view addSubview:_canvas];
    _screen = [BGMetalView new]; [_canvas addSubview:_screen];
    _controls = [BGPixelControls new]; _controls.gameSystem = 1; [_canvas addSubview:_controls];
    _status = [UILabel new]; _status.font = [UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightMedium];
    _status.textColor = [BGColor(@"93A4B5") colorWithAlphaComponent:.8]; _status.hidden = ![NSUserDefaults.standardUserDefaults boolForKey:@"BGShowStats"];
    [self.view addSubview:_status];
    _loading = [UILabel new]; _loading.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightSemibold];
    _loading.textColor = BGColor(@"5EE6E6"); _loading.textAlignment = NSTextAlignmentCenter;
    _loading.backgroundColor = [BGColor(@"141B25") colorWithAlphaComponent:.9]; _loading.layer.cornerRadius = 6; _loading.clipsToBounds = YES;
    _loading.text = @"Loading…"; [self.view addSubview:_loading];
    _session = [[BG3DSSession alloc] initWithLayer:(CAMetalLayer*)_screen.layer];
    __weak BGPlayController* weakSelf = self;
    _controls.inputChanged = ^(CTRSkinInput in) { BGPlayController* s = weakSelf; if (s) [s->_session setInput:in from:BGInputSourceScreen]; };
    _controls.menuPressed = ^{ [weakSelf showMenu]; };
    _session.controllerMenu = ^{ [weakSelf showMenu]; };
    _session.loadingChanged = ^(NSString* m) { BGPlayController* s = weakSelf; if (!s) return; s->_loading.hidden = m == nil; if (m) s->_loading.text = m; };
    _session.statusChanged = ^(NSString* t) { BGPlayController* s = weakSelf; if (s) s->_status.text = t; };
    _session.ended = ^(NSString* m, BOOL failed) { BGPlayController* s = weakSelf; if (s && s.finished) s.finished(m, failed); };
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resign) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(becomeActive) name:UIApplicationDidBecomeActiveNotification object:nil];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGRect f = self.view.safeAreaLayoutGuide.layoutFrame;
    if (!CGRectEqualToRect(_canvas.frame, f)) {
        _canvas.frame = f; _screen.frame = _canvas.bounds; _controls.frame = _canvas.bounds;
        [_controls refreshLayout];
        auto s = _controls.screenSlots;
        [_session setTopSlot:CGRectMake(s.top.x, s.top.y, s.top.width, s.top.height) bottomSlot:CGRectMake(s.bottom.x, s.bottom.y, s.bottom.width, s.bottom.height)];
    }
    _status.frame = CGRectMake(CGRectGetMinX(f) + 8, CGRectGetMinY(f), 200, 14);
    _loading.frame = CGRectMake(CGRectGetMidX(f) - 130, CGRectGetMinY(f) + f.size.height * .22, 260, 36);
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (!_session.running) { UIApplication.sharedApplication.idleTimerDisabled = YES; [_session run:_game]; }
}
- (void)viewWillDisappear:(BOOL)animated { [super viewWillDisappear:animated]; UIApplication.sharedApplication.idleTimerDisabled = NO; }
- (void)resign { [_controls releaseTouches]; [_session setPaused:YES]; }
- (void)becomeActive { if (!_menuOpen) [_session setPaused:NO]; }
- (void)showMenu {
    if (_menuOpen || !_session.running) return;
    _menuOpen = YES; [_controls releaseTouches]; [_session setPaused:YES];
    UIAlertController* m = [UIAlertController alertControllerWithTitle:_game.URLByDeletingPathExtension.lastPathComponent
        message:@"Save in the game before you stop." preferredStyle:UIAlertControllerStyleActionSheet];
    [m addAction:[UIAlertAction actionWithTitle:@"Resume" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { self->_menuOpen = NO; [self->_session setPaused:NO]; }]];
    [m addAction:[UIAlertAction actionWithTitle:_status.hidden ? @"Show FPS" : @"Hide FPS" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        self->_status.hidden = !self->_status.hidden; [NSUserDefaults.standardUserDefaults setBool:!self->_status.hidden forKey:@"BGShowStats"];
        self->_menuOpen = NO; [self->_session setPaused:NO]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Stop Game" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) { self->_menuOpen = NO; [self->_session stop]; }]];
    m.popoverPresentationController.sourceView = self.view;
    m.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMaxY(self.view.bounds) - 40, 1, 1);
    [self presentViewController:m animated:YES completion:nil];
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
@end

@interface BGGamesController : UIViewController <UITableViewDataSource, UITableViewDelegate, UIDocumentPickerDelegate>
@end
@implementation BGGamesController {
    UITableView* _table; NSArray<NSURL*>* _games; UILabel* _message; UISegmentedControl* _scale; BOOL _pickingUpdate;
}
- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }
- (UIButton*)button:(NSString*)title action:(SEL)action {
    UIButtonConfiguration* c = [UIButtonConfiguration filledButtonConfiguration];
    c.title = title; c.baseBackgroundColor = BGColor(@"1F2C3A"); c.baseForegroundColor = BGColor(@"5EE6E6");
    c.cornerStyle = UIButtonConfigurationCornerStyleSmall;
    UIButton* b = [UIButton buttonWithConfiguration:c primaryAction:nil]; [b addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    UIImage* grid = BGAsset(@"background", 0);
    self.view.backgroundColor = grid ? [UIColor colorWithPatternImage:grid] : BGColor(@"141B25");
    UIImageView* mascot = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"fuitlet"]]; BGNearest(mascot);
    mascot.contentMode = UIViewContentModeScaleAspectFit; [mascot.widthAnchor constraintEqualToConstant:72].active = YES; [mascot.heightAnchor constraintEqualToConstant:72].active = YES;
    UILabel* title = [UILabel new]; title.text = @"BERGAMOT 3DS"; title.textColor = BGColor(@"DFE8EF");
    title.font = [UIFont monospacedSystemFontOfSize:24 weight:UIFontWeightBold];
    UIStackView* head = [[UIStackView alloc] initWithArrangedSubviews:@[mascot, title]]; head.spacing = 12; head.alignment = UIStackViewAlignmentCenter;
    _scale = [[UISegmentedControl alloc] initWithItems:@[@"1×", @"2×", @"3×", @"4×"]];
    _scale.selectedSegmentIndex = BGPlayerScale() - 1; _scale.selectedSegmentTintColor = BGColor(@"5EE6E6");
    [_scale setTitleTextAttributes:@{NSForegroundColorAttributeName: BGColor(@"DFE8EF")} forState:UIControlStateNormal];
    [_scale setTitleTextAttributes:@{NSForegroundColorAttributeName: BGColor(@"141B25")} forState:UIControlStateSelected];
    [_scale addTarget:self action:@selector(scaleChanged) forControlEvents:UIControlEventValueChanged];
    UILabel* scaleLabel = [UILabel new]; scaleLabel.text = @"Resolution"; scaleLabel.textColor = BGColor(@"93A4B5"); scaleLabel.font = [UIFont systemFontOfSize:14];
    UIStackView* scaleRow = [[UIStackView alloc] initWithArrangedSubviews:@[scaleLabel, _scale]]; scaleRow.spacing = 12;
    UIStackView* buttons = [[UIStackView alloc] initWithArrangedSubviews:@[[self button:@"Import Game" action:@selector(importGame)], [self button:@"Install Update" action:@selector(importUpdate)]]];
    buttons.spacing = 10; buttons.distribution = UIStackViewDistributionFillEqually;
    _message = [UILabel new]; _message.numberOfLines = 0; _message.textColor = BGColor(@"93A4B5"); _message.font = [UIFont systemFontOfSize:13];
    _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _table.backgroundColor = UIColor.clearColor; _table.dataSource = self; _table.delegate = self; _table.separatorColor = BGColor(@"2B3A4C");
    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[head, buttons, scaleRow, _message, _table]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 14; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [stack setCustomSpacing:6 afterView:scaleRow];
    [self.view addSubview:stack];
    UILayoutGuide* g = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:g.topAnchor constant:16], [stack.bottomAnchor constraintEqualToAnchor:g.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16], [stack.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:UIApplicationDidBecomeActiveNotification object:nil];
    [self reload];
}
- (void)reload {
    NSArray* files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:BGGamesFolder() includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    NSMutableArray* games = [NSMutableArray new];
    for (NSURL* u in files) if ([BGGameExtensions() containsObject:u.pathExtension.lowercaseString]) [games addObject:u];
    [games sortUsingComparator:^NSComparisonResult(NSURL* a, NSURL* b) { return [a.lastPathComponent localizedCaseInsensitiveCompare:b.lastPathComponent]; }];
    _games = games; [_table reloadData];
    if (!_message.text.length || [_message.text hasPrefix:@"Tap Import"] || [_message.text hasPrefix:@"Tap a game"])
        _message.text = games.count ? @"Tap a game to play. Your saves are kept automatically."
                                    : @"Tap Import Game, or copy games into Files › On My iPhone › Bergamot 3DS › Games.";
}
- (void)scaleChanged { [NSUserDefaults.standardUserDefaults setInteger:_scale.selectedSegmentIndex + 1 forKey:@"BGPlayerScale"]; }
- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s { (void)t; (void)s; return (NSInteger)_games.count; }
- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)p {
    UITableViewCell* c = [t dequeueReusableCellWithIdentifier:@"g"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"g"];
    NSURL* u = _games[(NSUInteger)p.row];
    c.backgroundColor = UIColor.clearColor; c.textLabel.textColor = BGColor(@"DFE8EF"); c.detailTextLabel.textColor = BGColor(@"93A4B5");
    c.textLabel.text = u.URLByDeletingPathExtension.lastPathComponent; c.detailTextLabel.text = u.pathExtension.uppercaseString;
    UIView* sel = [UIView new]; sel.backgroundColor = BGColor(@"1F2C3A"); c.selectedBackgroundView = sel;
    return c;
}
// Scripted testing: launch with --bg-play "<file name in Games>" to open it directly.
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    static BOOL done = NO; if (done) return; done = YES;
    NSArray* args = NSProcessInfo.processInfo.arguments; NSUInteger i = [args indexOfObject:@"--bg-play"];
    if (i != NSNotFound && i + 1 < args.count) [self play:[BGGamesFolder() URLByAppendingPathComponent:args[i + 1]]];
}
- (void)tableView:(UITableView*)t didSelectRowAtIndexPath:(NSIndexPath*)p {
    [t deselectRowAtIndexPath:p animated:YES];
    [self play:_games[(NSUInteger)p.row]];
}
- (void)play:(NSURL*)game {
    BGPlayController* play = [[BGPlayController alloc] initWithGame:game];
    __weak BGGamesController* weakSelf = self;
    play.finished = ^(NSString* message, BOOL failed) {
        [weakSelf dismissViewControllerAnimated:YES completion:^{
            BGGamesController* s = weakSelf; if (!s) return;
            if (failed) BGAlert(s, @"Could not start the game", message); else s->_message.text = @"Stopped. Your save is kept.";
        }];
    };
    [self presentViewController:play animated:YES completion:nil];
}
- (void)pick:(NSArray<NSString*>*)exts update:(BOOL)update {
    _pickingUpdate = update;
    UIDocumentPickerViewController* p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:BGTypes(exts) asCopy:YES];
    p.delegate = self; p.allowsMultipleSelection = !update; [self presentViewController:p animated:YES completion:nil];
}
- (void)importGame { [self pick:BGGameExtensions() update:NO]; }
- (void)importUpdate { [self pick:@[@"cia"] update:YES]; }
- (void)documentPicker:(UIDocumentPickerViewController*)c didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)c;
    if (_pickingUpdate) {
        _message.text = @"Installing…";
        BG3DSSession* installer = [[BG3DSSession alloc] initWithLayer:nil];
        [installer installPackage:urls.firstObject completion:^(NSString* m, BOOL failed) { (void)installer; self->_message.text = m; if (failed) BGAlert(self, @"Install failed", m); }];
        return;
    }
    NSUInteger added = 0;
    for (NSURL* u in urls) {
        if (![BGGameExtensions() containsObject:u.pathExtension.lowercaseString]) continue;
        NSURL* dest = [BGGamesFolder() URLByAppendingPathComponent:u.lastPathComponent];
        [NSFileManager.defaultManager removeItemAtURL:dest error:nil];
        if ([NSFileManager.defaultManager moveItemAtURL:u toURL:dest error:nil]) ++added;
    }
    _message.text = added ? [NSString stringWithFormat:@"Imported %lu game%@.", (unsigned long)added, added == 1 ? @"" : @"s"] : @"That file isn't a 3DS game this app can open.";
    [self reload];
}
@end

@interface BGSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end
@implementation BGSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)s options:(UISceneConnectionOptions*)o {
    (void)s; (void)o;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    self.window.rootViewController = [BGGamesController new]; self.window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [self.window makeKeyAndVisible];
}
@end
@interface BGAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation BGAppDelegate
- (UISceneConfiguration*)application:(UIApplication*)a configurationForConnectingSceneSession:(UISceneSession*)s options:(UISceneConnectionOptions*)o {
    (void)a; (void)o; UISceneConfiguration* c = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:s.role]; c.delegateClass = BGSceneDelegate.class; return c;
}
@end
int main(int argc, char* argv[]) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(BGAppDelegate.class)); } }
