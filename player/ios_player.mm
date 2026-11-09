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
#include "game_art.hpp"

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
    _controls = [BGPixelControls new]; _controls.gameSystem = 1; _controls.fullWidthTop = YES; [_canvas addSubview:_controls];
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
    if (!_game) { _controls.previewMode = YES; _loading.hidden = YES; [_controls setNeedsDisplay]; return; }   // layout preview (test hook)
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

// Bergamot-style library: Bergie, pixel lettering, panel buttons, search and library-row tiles
// showing each game's own icon and title. A basic version of the Bergamot app's library.
static UIColor* BGInk() { return BGColor(@"EBF1E9"); }
static UIColor* BGAccent() { return BGColor(@"65E5E3"); }
static UIImage* BGSurface(NSString* name, NSInteger frame) { return [BGAsset(name, frame) resizableImageWithCapInsets:UIEdgeInsetsMake(8, 8, 8, 8) resizingMode:UIImageResizingModeStretch]; }

@interface BGPixelLabel : UILabel
@end
@implementation BGPixelLabel
- (void)drawTextInRect:(CGRect)rect {
    NSString* chars = @"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/-.+<>!? "; NSString* text = self.text.uppercaseString;
    UIImage* font = BGAsset(@"font", 0); if (!font) { [super drawTextInRect:rect]; return; }
    const CGFloat scale = MAX(1, round(self.font.pointSize / 8)), advance = 6 * scale;
    CGFloat x = rect.origin.x; const CGFloat y = rect.origin.y + MAX(0, (rect.size.height - 7 * scale) / 2);
    CGContextSetInterpolationQuality(UIGraphicsGetCurrentContext(), kCGInterpolationNone);
    for (NSUInteger i = 0; i < text.length && x + advance <= CGRectGetMaxX(rect); ++i) {
        NSRange found = [chars rangeOfString:[text substringWithRange:NSMakeRange(i, 1)]];
        if (found.location == NSNotFound) found = [chars rangeOfString:@"?"];
        CGImageRef glyph = CGImageCreateWithImageInRect(font.CGImage, CGRectMake(found.location * 8, 0, 6, 8));
        [[UIImage imageWithCGImage:glyph] drawInRect:CGRectMake(x, y, 6 * scale, 8 * scale)]; CGImageRelease(glyph); x += advance;
    }
}
@end

static UIButton* BGPanelButton(NSString* title, id target, SEL action) {
    UIButton* b = [UIButton buttonWithType:UIButtonTypeCustom];
    [b setBackgroundImage:BGSurface(@"panel", 0) forState:UIControlStateNormal];
    [b setBackgroundImage:BGSurface(@"panel", 2) forState:UIControlStateHighlighted];
    BGPixelLabel* label = [BGPixelLabel new]; label.text = title; label.font = [UIFont systemFontOfSize:16]; label.userInteractionEnabled = NO;
    label.translatesAutoresizingMaskIntoConstraints = NO; [b addSubview:label];
    [NSLayoutConstraint activateConstraints:@[[label.centerXAnchor constraintEqualToAnchor:b.centerXAnchor], [label.centerYAnchor constraintEqualToAnchor:b.centerYAnchor],
        [label.widthAnchor constraintEqualToConstant:title.length * 12 + 2], [label.heightAnchor constraintEqualToConstant:20], [b.heightAnchor constraintEqualToConstant:44]]];
    b.accessibilityLabel = title.capitalizedString; BGNearest(b); [b addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    return b;
}

@interface BGGameTile : UICollectionViewCell
@property(nonatomic, strong) UIImageView *frameView, *icon;
@property(nonatomic, strong) UILabel *title, *detail;
@property(nonatomic, strong) UIView* rule;
@end
@implementation BGGameTile
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _frameView = [UIImageView new]; BGNearest(_frameView); [self.contentView addSubview:_frameView];
        _icon = [UIImageView new]; BGNearest(_icon); _icon.contentMode = UIViewContentModeScaleAspectFit; [self.contentView addSubview:_icon];
        _title = [UILabel new]; _title.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightSemibold]; _title.textColor = BGInk(); [self.contentView addSubview:_title];
        _detail = [UILabel new]; _detail.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular]; _detail.textColor = BGAccent(); [self.contentView addSubview:_detail];
        _rule = [UIView new]; _rule.backgroundColor = [BGAccent() colorWithAlphaComponent:.6]; [self.contentView addSubview:_rule];
        [self refreshState];
    }
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    const CGSize s = self.contentView.bounds.size; const CGFloat art = MIN(56, s.height - 24);
    _frameView.frame = self.contentView.bounds;
    _icon.frame = CGRectMake(12, (s.height - art) / 2, art, art);
    const CGFloat tx = art + 26, tw = MAX(0, s.width - tx - 16);
    _title.frame = CGRectMake(tx, s.height / 2 - 22, tw, 22);
    _detail.frame = CGRectMake(tx, s.height / 2 + 2, tw, 16);
    _rule.frame = CGRectMake(tx, s.height - 14, MAX(0, tw - 4), 2);
}
- (void)refreshState { _frameView.image = BGAsset(@"library-row", self.highlighted ? 2 : self.selected ? 1 : 0); }
- (void)setHighlighted:(BOOL)h { [super setHighlighted:h]; [self refreshState]; }
- (void)setSelected:(BOOL)s { [super setSelected:s]; [self refreshState]; }
@end

// Pixel-art segmented selector drawn from the Aseprite "segment" asset (normal/selected/pressed),
// replacing the stock UISegmentedControl so the library matches the rest of the kit.
@interface BGSegmentControl : UIControl
@property(nonatomic) NSInteger selectedIndex;
- (instancetype)initWithTitles:(NSArray<NSString*>*)titles accessibilityNames:(NSArray<NSString*>*)names;
@end
@implementation BGSegmentControl {
    NSArray<UIButton*>* _cells;
}
- (instancetype)initWithTitles:(NSArray<NSString*>*)titles accessibilityNames:(NSArray<NSString*>*)names {
    if ((self = [super initWithFrame:CGRectZero])) {
        NSMutableArray* cells = [NSMutableArray new];
        for (NSUInteger i = 0; i < titles.count; ++i) {
            UIButton* b = [UIButton buttonWithType:UIButtonTypeCustom]; b.tag = (NSInteger)i; BGNearest(b);
            BGPixelLabel* label = [BGPixelLabel new]; label.text = titles[i]; label.font = [UIFont systemFontOfSize:16];
            label.userInteractionEnabled = NO; label.tag = 1000; [b addSubview:label];
            b.accessibilityLabel = names[i];
            [b addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
            [self addSubview:b]; [cells addObject:b];
        }
        _cells = cells; [self refresh];
        self.isAccessibilityElement = NO;
    }
    return self;
}
- (CGSize)intrinsicContentSize { return CGSizeMake(UIViewNoIntrinsicMetric, 40); }
- (void)layoutSubviews {
    [super layoutSubviews];
    const CGFloat gap = 6, w = (self.bounds.size.width - gap * (_cells.count - 1)) / _cells.count;
    for (UIButton* b in _cells) {
        b.frame = CGRectMake(b.tag * (w + gap), 0, w, self.bounds.size.height);
        UIView* label = [b viewWithTag:1000];
        const CGFloat lw = [(UILabel*)label text].length * 12 + 2;
        label.frame = CGRectMake((w - lw) / 2, (self.bounds.size.height - 20) / 2 - 2, lw, 20);
    }
}
- (void)setSelectedIndex:(NSInteger)i { _selectedIndex = i; [self refresh]; }
- (void)refresh {
    for (UIButton* b in _cells) {
        const BOOL on = b.tag == _selectedIndex;
        // 7 px caps keep the chamfers crisp; the face stretches with nearest sampling.
        UIImage* (^cell)(NSInteger) = ^UIImage*(NSInteger frame) {
            return [BGAsset(@"segment", frame) resizableImageWithCapInsets:UIEdgeInsetsMake(7, 7, 7, 7) resizingMode:UIImageResizingModeStretch];
        };
        [b setBackgroundImage:cell(on ? 1 : 0) forState:UIControlStateNormal];
        [b setBackgroundImage:cell(2) forState:UIControlStateHighlighted];
        ((UILabel*)[b viewWithTag:1000]).textColor = on ? BGInk() : [BGInk() colorWithAlphaComponent:.7];
        b.accessibilityTraits = UIAccessibilityTraitButton | (on ? UIAccessibilityTraitSelected : 0);
        [[b viewWithTag:1000] setNeedsDisplay];
    }
}
- (void)tapped:(UIButton*)b {
    if (b.tag == _selectedIndex) return;
    self.selectedIndex = b.tag; [self sendActionsForControlEvents:UIControlEventValueChanged];
}
@end

@interface BGGamesController : UIViewController <UICollectionViewDataSource, UICollectionViewDelegateFlowLayout, UIDocumentPickerDelegate, UITextFieldDelegate>
@end
@implementation BGGamesController {
    UICollectionView* _grid; NSArray<NSURL*>* _games; NSArray<NSURL*>* _shown; UILabel* _message; BGSegmentControl* _scale;
    UITextField* _search; UILabel* _scaleNote; BOOL _pickingUpdate; NSCache<NSString*, NSDictionary*>* _art;
}
- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }
- (void)viewDidLoad {
    [super viewDidLoad];
    _art = [NSCache new];
    UIImage* grid = BGAsset(@"background", 0);
    self.view.backgroundColor = grid ? [UIColor colorWithPatternImage:grid] : BGColor(@"141B25");
    UIImageView* bergie = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"fuitlet"]]; BGNearest(bergie);
    bergie.contentMode = UIViewContentModeScaleAspectFit; bergie.accessibilityLabel = @"Bergie";
    [bergie.widthAnchor constraintEqualToConstant:64].active = YES; [bergie.heightAnchor constraintEqualToConstant:64].active = YES;
    BGPixelLabel* title = [BGPixelLabel new]; title.text = @"BERGAMOT 3DS"; title.font = [UIFont systemFontOfSize:24]; title.textColor = BGInk();
    [title.heightAnchor constraintEqualToConstant:28].active = YES; title.accessibilityLabel = @"Bergamot 3DS";
    UIStackView* head = [[UIStackView alloc] initWithArrangedSubviews:@[bergie, title]]; head.spacing = 10; head.alignment = UIStackViewAlignmentCenter;
    UIView* rule = [UIView new]; rule.backgroundColor = BGAccent(); [rule.heightAnchor constraintEqualToConstant:2].active = YES;
    UIStackView* buttons = [[UIStackView alloc] initWithArrangedSubviews:@[BGPanelButton(@"IMPORT", self, @selector(importGame)), BGPanelButton(@"UPDATE", self, @selector(importUpdate))]];
    buttons.spacing = 10; buttons.distribution = UIStackViewDistributionFillEqually;
    _scale = [[BGSegmentControl alloc] initWithTitles:@[@"AUTO", @"1X", @"2X", @"3X", @"4X"]
                                    accessibilityNames:@[@"Automatic upscaling", @"Native upscaling", @"2× upscaling", @"3× upscaling", @"4× upscaling"]];
    const NSInteger stored = [NSUserDefaults.standardUserDefaults integerForKey:@"BGPlayerScale"];
    _scale.selectedIndex = stored >= 1 && stored <= 4 ? stored : 0;
    _scaleNote = [UILabel new]; _scaleNote.font = [UIFont systemFontOfSize:12]; _scaleNote.textColor = [BGInk() colorWithAlphaComponent:.6];
    [_scale addTarget:self action:@selector(scaleChanged) forControlEvents:UIControlEventValueChanged];
    BGPixelLabel* scaleLabel = [BGPixelLabel new]; scaleLabel.text = @"UPSCALING"; scaleLabel.font = [UIFont systemFontOfSize:12]; scaleLabel.textColor = BGInk();
    [scaleLabel.heightAnchor constraintEqualToConstant:14].active = YES;
    UIStackView* scaleRow = [[UIStackView alloc] initWithArrangedSubviews:@[scaleLabel, _scale, _scaleNote]];
    scaleRow.axis = UILayoutConstraintAxisVertical; scaleRow.spacing = 6;
    [_scale.heightAnchor constraintEqualToConstant:40].active = YES;
    UIImageView* searchFrame = [[UIImageView alloc] initWithImage:BGSurface(@"field", 0)]; BGNearest(searchFrame); searchFrame.userInteractionEnabled = YES;
    [searchFrame.heightAnchor constraintEqualToConstant:44].active = YES;
    _search = [UITextField new]; _search.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular]; _search.textColor = BGInk();
    _search.attributedPlaceholder = [[NSAttributedString alloc] initWithString:@"SEARCH YOUR GAMES..." attributes:@{NSForegroundColorAttributeName: [BGInk() colorWithAlphaComponent:.6]}];
    _search.autocorrectionType = UITextAutocorrectionTypeNo; _search.clearButtonMode = UITextFieldViewModeWhileEditing; _search.returnKeyType = UIReturnKeyDone;
    _search.delegate = self; _search.accessibilityLabel = @"Search games";
    [_search addTarget:self action:@selector(filter) forControlEvents:UIControlEventEditingChanged];
    _search.translatesAutoresizingMaskIntoConstraints = NO; [searchFrame addSubview:_search];
    [NSLayoutConstraint activateConstraints:@[[_search.leadingAnchor constraintEqualToAnchor:searchFrame.leadingAnchor constant:14],
        [_search.trailingAnchor constraintEqualToAnchor:searchFrame.trailingAnchor constant:-14], [_search.topAnchor constraintEqualToAnchor:searchFrame.topAnchor],
        [_search.bottomAnchor constraintEqualToAnchor:searchFrame.bottomAnchor]]];
    _message = [UILabel new]; _message.numberOfLines = 0; _message.textColor = [BGInk() colorWithAlphaComponent:.75]; _message.font = [UIFont systemFontOfSize:13];
    UICollectionViewFlowLayout* layout = [UICollectionViewFlowLayout new]; layout.minimumLineSpacing = 12; layout.minimumInteritemSpacing = 10;
    _grid = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layout];
    _grid.backgroundColor = UIColor.clearColor; _grid.dataSource = self; _grid.delegate = self; _grid.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
    [_grid registerClass:BGGameTile.class forCellWithReuseIdentifier:@"game"];
    UIStackView* stack = [[UIStackView alloc] initWithArrangedSubviews:@[head, rule, buttons, scaleRow, searchFrame, _message, _grid]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 12; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [stack setCustomSpacing:4 afterView:head];
    [self.view addSubview:stack];
    UILayoutGuide* g = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:g.topAnchor constant:12], [stack.bottomAnchor constraintEqualToAnchor:g.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16], [stack.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:UIApplicationDidBecomeActiveNotification object:nil];
    [self describeScale];
    [self reload];
}
- (void)viewWillTransitionToSize:(CGSize)size withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)c {
    [super viewWillTransitionToSize:size withTransitionCoordinator:c]; [_grid.collectionViewLayout invalidateLayout];
}
- (void)reload {
    NSArray* files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:BGGamesFolder() includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    NSMutableArray* games = [NSMutableArray new];
    for (NSURL* u in files) if ([BGGameExtensions() containsObject:u.pathExtension.lowercaseString]) [games addObject:u];
    [games sortUsingComparator:^NSComparisonResult(NSURL* a, NSURL* b) { return [[self titleFor:a] localizedCaseInsensitiveCompare:[self titleFor:b]]; }];
    _games = games; [self filter];
    if (!_message.text.length || [_message.text hasPrefix:@"Import a game"] || [_message.text hasPrefix:@"Tap a game"])
        _message.text = games.count ? @"Tap a game to play. Saves are kept automatically."
                                    : @"Import a game, or copy games into Files › On My iPhone › Bergamot 3DS › Games.";
}
- (void)filter {
    NSString* q = _search.text;
    if (!q.length) { _shown = _games; } else {
        NSMutableArray* m = [NSMutableArray new];
        for (NSURL* u in _games) if ([[self titleFor:u] localizedCaseInsensitiveContainsString:q] || [u.lastPathComponent localizedCaseInsensitiveContainsString:q]) [m addObject:u];
        _shown = m;
    }
    [_grid reloadData];
}
- (BOOL)textFieldShouldReturn:(UITextField*)t { [t resignFirstResponder]; return YES; }
// Icon and title from the game's own SMDH (or a .smdh sidecar); read once, off the main thread.
- (NSString*)titleFor:(NSURL*)u { NSString* t = [_art objectForKey:u.path][@"title"]; return t.length ? t : u.URLByDeletingPathExtension.lastPathComponent; }
- (void)loadArt:(NSURL*)u forIndex:(NSIndexPath*)ip {
    if ([_art objectForKey:u.path]) return;
    [_art setObject:@{} forKey:u.path];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto art = bergamont::read_game_art(u.fileSystemRepresentation);
        if (!art) art = bergamont::read_game_art([[u URLByDeletingPathExtension] URLByAppendingPathExtension:@"smdh"].fileSystemRepresentation);
        NSMutableDictionary* d = [NSMutableDictionary new];
        if (art) {
            NSString* full = [[NSString alloc] initWithCharacters:(const unichar*)art->title.data() length:art->title.size()];
            d[@"title"] = [[full componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet].firstObject stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] ?: @"";
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate((void*)art->rgba.data(), 48, 48, 8, 48 * 4, cs, kCGImageAlphaPremultipliedLast);
            CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
            if (img) { d[@"icon"] = [UIImage imageWithCGImage:img]; CGImageRelease(img); }
            if (ctx) CGContextRelease(ctx); CGColorSpaceRelease(cs);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_art setObject:d forKey:u.path];
            if ([self->_shown containsObject:u] && ip.item < (NSInteger)self->_shown.count && [self->_shown[ip.item] isEqual:u]) [self->_grid reloadItemsAtIndexPaths:@[ip]];
        });
    });
}
- (NSInteger)collectionView:(UICollectionView*)c numberOfItemsInSection:(NSInteger)s { (void)c; (void)s; return (NSInteger)_shown.count; }
- (CGSize)collectionView:(UICollectionView*)c layout:(UICollectionViewLayout*)l sizeForItemAtIndexPath:(NSIndexPath*)ip {
    (void)l; (void)ip; const CGFloat w = c.bounds.size.width; const NSInteger cols = w >= 680 ? 2 : 1;
    return CGSizeMake(floor((w - 10 * (cols - 1)) / cols), 88);
}
- (UICollectionViewCell*)collectionView:(UICollectionView*)c cellForItemAtIndexPath:(NSIndexPath*)ip {
    BGGameTile* tile = [c dequeueReusableCellWithReuseIdentifier:@"game" forIndexPath:ip];
    NSURL* u = _shown[(NSUInteger)ip.item]; NSDictionary* art = [_art objectForKey:u.path];
    tile.title.text = [self titleFor:u];
    tile.detail.text = [NSString stringWithFormat:@"3DS · %@", u.pathExtension.uppercaseString];
    tile.icon.image = art[@"icon"] ?: BGAsset(@"icon-3ds", 0);
    tile.accessibilityLabel = tile.title.text; tile.isAccessibilityElement = YES; tile.accessibilityTraits = UIAccessibilityTraitButton;
    if (!art) [self loadArt:u forIndex:ip];
    return tile;
}
- (void)collectionView:(UICollectionView*)c didSelectItemAtIndexPath:(NSIndexPath*)ip { [c deselectItemAtIndexPath:ip animated:YES]; [self play:_shown[(NSUInteger)ip.item]]; }
- (void)scaleChanged { [NSUserDefaults.standardUserDefaults setInteger:_scale.selectedIndex forKey:@"BGPlayerScale"]; [self describeScale]; }
- (void)describeScale {
    NSInteger px = 0; const NSInteger pick = BGAutoScale(&px);
    _scaleNote.text = _scale.selectedIndex == 0
        ? [NSString stringWithFormat:@"Auto renders at %ld× for this %@'s %ld-pixel-wide screen.", (long)pick,
              UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? @"iPad" : @"iPhone", (long)px]
        : (_scale.selectedIndex == 1 ? @"Native 3DS resolution, 400×240." : @"Higher upscaling looks sharper and uses more GPU.");
}
// Scripted testing: launch with --bg-play "<file name in Games>" to open it directly.
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    static BOOL done = NO; if (done) return; done = YES;
    NSArray* args = NSProcessInfo.processInfo.arguments; NSUInteger i = [args indexOfObject:@"--bg-play"];
    if (i != NSNotFound && i + 1 < args.count) [self play:[BGGamesFolder() URLByAppendingPathComponent:args[i + 1]]];
    // --bg-preview-layout shows the play screen's layout without starting the emulator (Simulator checks).
    if ([args containsObject:@"--bg-preview-layout"]) [self presentViewController:[[BGPlayController alloc] initWithGame:nil] animated:NO completion:nil];
}
- (void)play:(NSURL*)game {
    [_search resignFirstResponder];
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
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
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
