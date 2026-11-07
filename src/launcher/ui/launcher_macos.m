#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "launcher.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include "maage/time.h"
#include <math.h>
#include <string.h>

static NSStackView *maage_ui_stack(BOOL horizontal, CGFloat spacing) {
    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = horizontal ? NSUserInterfaceLayoutOrientationHorizontal : NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = horizontal ? NSLayoutAttributeCenterY : NSLayoutAttributeLeading;
    stack.spacing = spacing;
    return stack;
}
static NSTextField *maage_ui_label(NSString *text, CGFloat size, BOOL bold) {
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    label.textColor = NSColor.labelColor;
    return label;
}
static void maage_ui_fill(NSView *child, NSView *parent, CGFloat margin) {
    child.translatesAutoresizingMaskIntoConstraints = NO;
    [parent addSubview:child];
    [NSLayoutConstraint activateConstraints:@[
        [child.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor
                                            constant:margin],
        [child.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor
                                             constant:-margin],
        [child.topAnchor constraintEqualToAnchor:parent.topAnchor
                                        constant:margin],
        [child.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor
                                           constant:-margin]
    ]];
}
static NSTextField *maage_ui_row_label(NSString *text, CGFloat size, BOOL bold) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    [label setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
    return label;
}
/* Cover the native Launcher completely during initial preparation. */
@interface MaaGELauncherSplashView : NSView
@end
@implementation MaaGELauncherSplashView
- (BOOL)isOpaque {
    return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(self.bounds);
}
@end
/* Scrolled settings content must start at the TOP even when its preferred
 * height exceeds the clip view. AppKit's default NSView origin is bottom-left. */
@interface MaaGELauncherTopDocumentView : NSView
@end
@implementation MaaGELauncherTopDocumentView
- (BOOL)isFlipped {
    return YES;
}
@end
static NSImageView *maage_ui_brand_logo(CGFloat size, CGFloat cornerRadius) {
    NSString *path = [NSBundle.mainBundle pathForResource:@"icon" ofType:@"png"];
    NSImage *image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    NSImageView *logo = [NSImageView imageViewWithImage:image ?: NSApp.applicationIconImage];
    logo.imageScaling = NSImageScaleProportionallyUpOrDown;
    logo.wantsLayer = YES;
    logo.layer.backgroundColor = [NSColor colorWithSRGBRed:111.0 / 255.0 green:152.0 / 255.0 blue:94.0 / 255.0 alpha:1].CGColor;
    logo.layer.cornerRadius = cornerRadius;
    logo.layer.masksToBounds = YES;
    [logo.widthAnchor constraintEqualToConstant:size].active = YES;
    [logo.heightAnchor constraintEqualToConstant:size].active = YES;
    return logo;
}
/* Native Launcher chrome follows the bundled macOS app icon, not the
 * cross-platform PNG. ICNS owns its background and silhouette. */
static NSImageView *maage_ui_macos_app_icon(CGFloat size) {
    NSString *path = [NSBundle.mainBundle pathForResource:@"icon" ofType:@"icns"];
    NSImage *image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    NSImageView *logo = [NSImageView imageViewWithImage:image ?: NSApp.applicationIconImage];
    logo.imageScaling = NSImageScaleProportionallyUpOrDown;
    logo.wantsLayer = YES;
    [logo.widthAnchor constraintEqualToConstant:size].active = YES;
    [logo.heightAnchor constraintEqualToConstant:size].active = YES;
    return logo;
}
/* Native launch home sized from the InDesign composition (2560 x 1440).
 * The design scales uniformly with window size; no background image or web view. */
static NSRect maage_ui_home_rect(CGFloat x, CGFloat y, CGFloat width, CGFloat height, CGFloat scale, CGFloat originX, CGFloat originY) {
    return NSMakeRect(originX + x * scale, originY + (1440.0 - y - height) * scale, width * scale, height * scale);
}
static NSRect maage_ui_home_centered_text_rect(NSRect box, NSFont *font) {
    CGFloat textHeight = ceil(font.ascender - font.descender + font.leading + 3);
    return NSMakeRect(NSMinX(box), NSMidY(box) - textHeight / 2, NSWidth(box), textHeight);
}
/* Place the SF Symbol into the text run itself, so both glyphs use one real
 * typographic baseline regardless of NSTextField's internal cell geometry. */
/* Account verification belongs to the same line as the account name, after
 * the name rather than in an independently positioned image view. */
static NSAttributedString *maage_ui_home_verified_name(NSString *name, NSFont *font, CGFloat scale) {
    NSDictionary *attributes = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: NSColor.blackColor };
    NSMutableAttributedString *label = [[NSMutableAttributedString alloc] initWithString:name attributes:attributes];
    if (@available(macOS 11.0, *)) {
        NSImage *symbol = [NSImage imageWithSystemSymbolName:@"checkmark.circle.fill" accessibilityDescription:@"Verified Minecraft account"];
        if (symbol) {
            CGFloat size = 24 * scale;
            NSImageSymbolConfiguration *configuration = [NSImageSymbolConfiguration configurationWithPointSize:size weight:NSFontWeightMedium];
            if (@available(macOS 12.0, *)) configuration = [configuration configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:[NSColor colorWithSRGBRed:0.17 green:0.62 blue:0.26 alpha:1]]];
            symbol = [symbol imageWithSymbolConfiguration:configuration] ?: symbol;
            NSTextAttachment *attachment = [[NSTextAttachment alloc] init];
            attachment.image = symbol;
            attachment.bounds = NSMakeRect(0, (font.capHeight - size) / 2, size, size);
            [label appendAttributedString:[[NSAttributedString alloc] initWithString:@" " attributes:attributes]];
            [label appendAttributedString:[NSAttributedString attributedStringWithAttachment:attachment]];
        }
    }
    return label;
}
static NSAttributedString *maage_ui_home_inline(NSString *text, NSString *symbolName, NSFont *font, NSColor *textColor, NSColor *symbolColor, CGFloat symbolSize, NSShadow *shadow) {
    NSMutableDictionary *attributes = [@{ NSFontAttributeName: font, NSForegroundColorAttributeName: textColor } mutableCopy];
    if (shadow) attributes[NSShadowAttributeName] = shadow;
    NSMutableAttributedString *line = [[NSMutableAttributedString alloc] init];
    if (@available(macOS 11.0, *)) {
        NSImage *symbol = [NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
        if (symbol) {
            NSImageSymbolConfiguration *configuration = [NSImageSymbolConfiguration configurationWithPointSize:symbolSize weight:NSFontWeightMedium];
            if (@available(macOS 12.0, *)) {
                configuration = [configuration configurationByApplyingConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:symbolColor]];
            }
            symbol = [symbol imageWithSymbolConfiguration:configuration] ?: symbol;
            NSTextAttachment *attachment = [[NSTextAttachment alloc] init];
            attachment.image = symbol;
            attachment.bounds = NSMakeRect(0, (font.capHeight - symbolSize) / 2, symbolSize, symbolSize);
            [line appendAttributedString:[NSAttributedString attributedStringWithAttachment:attachment]];
            [line appendAttributedString:[[NSAttributedString alloc] initWithString:@"  " attributes:@{ NSFontAttributeName: font }]];
        }
    }
    [line appendAttributedString:[[NSAttributedString alloc] initWithString:text attributes:attributes]];
    return line;
}
/* Use the same intrinsic text measurement for the button's hit area and its
 * caption. Text-field cells need extra horizontal space beyond glyph bounds. */
static CGFloat maage_ui_home_configuration_caption_width(NSFont *font, CGFloat scale) {
    return ceil([@"Launcher Configuration" sizeWithAttributes:@{ NSFontAttributeName: font }].width) + 24 * scale;
}
static CGFloat maage_ui_home_configuration_width(CGFloat scale) {
    NSFont *font = [NSFont systemFontOfSize:38 * scale weight:NSFontWeightMedium];
    return 38 * scale + 17 * scale + maage_ui_home_configuration_caption_width(font, scale) + 36 * scale;
}
@interface MaaGELauncherHomeView : NSView
@property(nonatomic, strong) NSImageView *brand;
@property(nonatomic, strong) NSView *taglineLeftRule, *taglineRightRule;
@property(nonatomic, strong) NSTextField *heading, *tagline, *notice, *version;
@property(nonatomic, strong) NSButton *playButton, *configuration;
@property(nonatomic, strong) NSTextField *readinessLabel;
@property(nonatomic, copy) NSString *readinessText, *readinessSymbol;
@property(nonatomic, strong) NSColor *readinessTint;
@property(nonatomic) CGFloat readinessFontSize;
@property(nonatomic, strong) NSArray<NSView *> *cards;
- (void)updateReadiness:(NSString *)text symbol:(NSString *)symbol tint:(NSColor *)tint;
@end
@implementation MaaGELauncherHomeView
- (void)updateReadiness:(NSString *)text symbol:(NSString *)symbol tint:(NSColor *)tint {
    if ([self.readinessText isEqualToString:text] && [self.readinessSymbol isEqualToString:symbol] && [self.readinessTint isEqual:tint]) return;
    self.readinessText = text;
    self.readinessSymbol = symbol;
    self.readinessTint = tint;
    NSFont *font = self.readinessFontSize > 0 ? [NSFont systemFontOfSize:self.readinessFontSize weight:NSFontWeightMedium] : self.readinessLabel.font;
    self.readinessLabel.attributedStringValue = maage_ui_home_inline(text, symbol, font, NSColor.whiteColor, tint, font.pointSize, self.readinessLabel.shadow);
    [self setNeedsLayout:YES];
}
- (BOOL)isOpaque {
    return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    NSColor *upper = [NSColor colorWithSRGBRed:0.38 green:0.52 blue:0.59 alpha:1];
    NSColor *lower = [NSColor colorWithSRGBRed:0.16 green:0.27 blue:0.30 alpha:1];
    [[[NSGradient alloc] initWithStartingColor:lower endingColor:upper] drawInRect:self.bounds angle:90];
}
- (void)layout {
    [super layout];
    CGFloat width = NSWidth(self.bounds), height = NSHeight(self.bounds);
    if (width <= 40 || height <= 34) return;
    CGFloat scale = MIN((width - 40.0) / 2210.0, (height - 34.0) / 1440.0);
    CGFloat originX = (width - 2560.0 * scale) / 2, originY = (height - 1440.0 * scale) / 2;

    self.brand.frame = maage_ui_home_rect(1054, 0, 451, 451, scale, originX, originY);
    NSFont *headingFont = [NSFont systemFontOfSize:110 * scale weight:NSFontWeightBold];
    if (fabs(self.heading.font.pointSize - headingFont.pointSize) > 0.1) self.heading.font = headingFont;
    self.heading.frame = maage_ui_home_centered_text_rect(maage_ui_home_rect(930, 426, 700, 170, scale, originX, originY), headingFont);
    NSFont *taglineFont = [NSFont systemFontOfSize:45 * scale weight:NSFontWeightRegular];
    if (fabs(self.tagline.font.pointSize - taglineFont.pointSize) > 0.1) {
        self.tagline.font = taglineFont;
        NSShadow *shadow = self.tagline.shadow;
        shadow.shadowBlurRadius = 28 * scale;
        shadow.shadowOffset = NSMakeSize(0, -8 * scale);
        self.tagline.shadow = shadow;
    }
    self.tagline.frame = maage_ui_home_centered_text_rect(maage_ui_home_rect(868, 615, 824, 59, scale, originX, originY), taglineFont);
    self.taglineLeftRule.frame = maage_ui_home_rect(868, 643, 60, 2, scale, originX, originY);
    self.taglineRightRule.frame = maage_ui_home_rect(1626, 643, 66, 2, scale, originX, originY);

    self.playButton.frame = maage_ui_home_rect(915, 708, 730, 154, scale, originX, originY);
    NSFont *statusFont = [NSFont systemFontOfSize:31 * scale weight:NSFontWeightMedium];
    if (fabs(self.readinessFontSize - statusFont.pointSize) > 0.1) {
        self.readinessFontSize = statusFont.pointSize;
        NSString *status = self.readinessText ?: @"Initializing…";
        self.readinessLabel.font = statusFont;
        self.readinessLabel.attributedStringValue = maage_ui_home_inline(status, self.readinessSymbol ?: @"clock.fill", statusFont, NSColor.whiteColor, self.readinessTint ?: NSColor.whiteColor, statusFont.pointSize, self.readinessLabel.shadow);
    }
    NSRect statusBox = maage_ui_home_rect(0, 885, 2560, 49, scale, originX, originY);
    CGFloat statusWidth = MIN(self.readinessLabel.attributedStringValue.size.width + 20 * scale, 1100 * scale);
    self.readinessLabel.frame = maage_ui_home_centered_text_rect(NSMakeRect(NSMidX(self.bounds) - statusWidth / 2, NSMinY(statusBox), statusWidth, NSHeight(statusBox)), statusFont);

    NSArray<NSNumber *> *positions = @[ @188, @743, @1298, @1853 ];
    for (NSUInteger i = 0; i < MIN(self.cards.count, positions.count); ++i)
        self.cards[i].frame = maage_ui_home_rect(positions[i].doubleValue, 1080, 520, 110, scale, originX, originY);
    /* The hit area follows the centered gear-and-caption group, not a 800-unit box. */
    NSRect configurationRow = maage_ui_home_rect(0, 1230, 2560, 56, scale, originX, originY);
    CGFloat configurationWidth = maage_ui_home_configuration_width(scale);
    self.configuration.frame = NSMakeRect(NSMidX(self.bounds) - configurationWidth / 2, NSMinY(configurationRow), configurationWidth, NSHeight(configurationRow));
    NSFont *footerFont = [NSFont systemFontOfSize:26 * scale];
    NSFont *versionFont = [NSFont systemFontOfSize:25 * scale];
    if (fabs(self.notice.font.pointSize - footerFont.pointSize) > 0.1) self.notice.font = footerFont;
    if (fabs(self.version.font.pointSize - versionFont.pointSize) > 0.1) self.version.font = versionFont;
    CGFloat footerWidth = MIN(width - 28, 2200 * scale);
    CGFloat noticeHeight = MAX(15, ceil(footerFont.ascender - footerFont.descender + footerFont.leading + 4));
    CGFloat versionHeight = MAX(15, ceil(versionFont.ascender - versionFont.descender + versionFont.leading + 4));
    self.notice.frame = NSMakeRect((width - footerWidth) / 2, 11, footerWidth, noticeHeight);
    self.version.frame = NSMakeRect((width - footerWidth) / 2, NSMaxY(self.notice.frame) + 10 * scale, footerWidth, versionHeight);
    self.playButton.needsDisplay = YES;
}
@end
/* Layer-backed hover feedback is shared by Play, the managed-card arrows,
 * and Launcher Configuration. Respect the user's Reduce Motion preference. */
static void maage_ui_home_animate_layer(CALayer *layer, NSString *key, id value, CFTimeInterval duration) {
    if (!layer) return;
    id modelValue = [layer valueForKeyPath:key];
    if (modelValue && [modelValue isEqual:value]) return;
    id previous = [[layer presentationLayer] valueForKeyPath:key] ?: modelValue;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer setValue:value forKeyPath:key];
    [CATransaction commit];
    NSString *animationKey = [@"maage.home." stringByAppendingString:key];
    if (NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion || !previous) {
        [layer removeAnimationForKey:animationKey];
        return;
    }
    CABasicAnimation *transition = [CABasicAnimation animationWithKeyPath:key];
    transition.fromValue = previous;
    transition.toValue = value;
    transition.duration = duration;
    transition.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
    [layer addAnimation:transition forKey:animationKey];
}
@interface MaaGELauncherHomeActionButton : NSButton
@property(nonatomic, strong) NSTrackingArea *homeTracking;
@property(nonatomic) BOOL homeHovered, homePressed;
@property(nonatomic) CGFloat homeRestOpacity;
- (void)refreshHomeInteraction;
@end
@implementation MaaGELauncherHomeActionButton
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        _homeRestOpacity = 0.94;
    }
    return self;
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.homeTracking) [self removeTrackingArea:self.homeTracking];
    self.homeTracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:self.homeTracking];
}
- (void)refreshHomeInteraction {
    /* AppKit owns a control's backing-layer geometry. Transforming that layer
     * scales around AppKit's layout origin and makes small arrows jump diagonally. */
    CGFloat opacity = !self.enabled ? 0.86 : self.homePressed ? 0.85 : self.homeHovered ? 1 : self.homeRestOpacity;
    maage_ui_home_animate_layer(self.layer, @"opacity", @(opacity), self.homePressed ? 0.10 : 0.17);
    [self setNeedsDisplay:YES];
}
- (void)mouseEntered:(NSEvent *)event {
    (void) event;
    if (!self.enabled) return;
    self.homeHovered = YES;
    [self refreshHomeInteraction];
}
- (void)mouseExited:(NSEvent *)event {
    (void) event;
    self.homeHovered = NO;
    [self refreshHomeInteraction];
}
- (void)mouseDown:(NSEvent *)event {
    if (!self.enabled) return;
    self.homePressed = YES;
    [self refreshHomeInteraction];
    [super mouseDown:event];
    self.homePressed = NO;
    [self refreshHomeInteraction];
}
- (void)setEnabled:(BOOL)enabled {
    if (self.enabled == enabled) return;
    [super setEnabled:enabled];
    if (!enabled) self.homeHovered = self.homePressed = NO;
    [self refreshHomeInteraction];
    [self.window invalidateCursorRectsForView:self];
}
- (void)resetCursorRects {
    [super resetCursorRects];
    if (self.enabled) [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor];
}
@end
/* Keep the gear and caption in the SAME attributed line, sharing one native
 * text baseline and one hitbox. No independently laid-out NSImageView. */
@interface MaaGELauncherConfigurationButton : MaaGELauncherHomeActionButton
@property(nonatomic, strong) NSTextField *homeCaption;
@property(nonatomic) CGFloat captionFontSize;
@end
@implementation MaaGELauncherConfigurationButton
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.title = @"";
        self.bordered = NO;
        self.accessibilityLabel = @"Launcher Configuration";
        _homeCaption = maage_ui_row_label(@"Launcher Configuration", 18, YES);
        _homeCaption.textColor = NSColor.whiteColor;
        _homeCaption.alignment = NSTextAlignmentCenter;
        [self addSubview:_homeCaption];
        _homeCaption.translatesAutoresizingMaskIntoConstraints = NO;
    }
    return self;
}
- (void)refreshHomeInteraction {
    [super refreshHomeInteraction];
    CGFloat opacity = !self.enabled ? 0 : self.homePressed ? 0.16 : self.homeHovered ? 0.11 : 0;
    NSColor *background = [NSColor colorWithWhite:1 alpha:opacity];
    maage_ui_home_animate_layer(self.layer, @"backgroundColor", (__bridge id) background.CGColor, 0.19);
}
- (void)layout {
    [super layout];
    CGFloat scale = NSHeight(self.bounds) / 56.0;
    if (scale <= 0) return;
    self.layer.cornerRadius = NSHeight(self.bounds) / 2;
    NSFont *font = [NSFont systemFontOfSize:38 * scale weight:NSFontWeightMedium];
    if (fabs(self.captionFontSize - font.pointSize) > 0.1) {
        self.captionFontSize = font.pointSize;
        self.homeCaption.font = font;
        NSShadow *shadow = [[NSShadow alloc] init];
        shadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.46];
        shadow.shadowBlurRadius = 8 * scale;
        shadow.shadowOffset = NSMakeSize(0, -3 * scale);
        self.homeCaption.attributedStringValue = maage_ui_home_inline(@"Launcher Configuration", @"gearshape", font, NSColor.whiteColor, NSColor.whiteColor, 38 * scale, shadow);
    }
    self.homeCaption.frame = maage_ui_home_centered_text_rect(NSInsetRect(self.bounds, 14 * scale, 0), font);
}
- (NSView *)hitTest:(NSPoint)point {
    NSView *hit = [super hitTest:point];
    return hit == self.homeCaption ? self : hit;
}
@end
@interface MaaGELauncherPlayButton : MaaGELauncherHomeActionButton
@end
@implementation MaaGELauncherPlayButton
- (void)layout {
    [super layout];
    CGFloat scale = NSHeight(self.bounds) / 154.0;
    if (scale <= 0) return;
    self.layer.cornerRadius = NSHeight(self.bounds) / 2;
    self.layer.masksToBounds = NO;
    self.layer.shadowColor = NSColor.blackColor.CGColor;
    /* Core Animation's positive Y offset places this layer shadow below the button. */
    self.layer.shadowOffset = NSMakeSize(0, 11 * scale);
    [self refreshHomeInteraction];
}
- (void)refreshHomeInteraction {
    [super refreshHomeInteraction];
    CGFloat scale = MAX(0.01, NSHeight(self.bounds) / 154.0);
    NSColor *fill = !self.enabled ? [NSColor colorWithSRGBRed:0.38 green:0.48 blue:0.39 alpha:1]
                    : self.homePressed ? [NSColor colorWithSRGBRed:0.34 green:0.52 blue:0.27 alpha:1]
                    : self.homeHovered ? [NSColor colorWithSRGBRed:0.45 green:0.70 blue:0.35 alpha:1]
                                       : [NSColor colorWithSRGBRed:103.0 / 255.0 green:161.0 / 255.0 blue:77.0 / 255.0 alpha:1];
    maage_ui_home_animate_layer(self.layer, @"backgroundColor", (__bridge id) fill.CGColor, 0.19);
    maage_ui_home_animate_layer(self.layer, @"shadowOpacity", @(!self.enabled ? 0.22 : self.homeHovered ? 0.51 : 0.36), 0.19);
    maage_ui_home_animate_layer(self.layer, @"shadowRadius", @((self.enabled && self.homeHovered ? 17 : 12) * scale), 0.19);
}
- (void)drawRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    CGFloat scale = NSHeight(self.bounds) / 154.0;
    NSRect rect = NSInsetRect(self.bounds, 6 * scale, 9 * scale);
    NSFont *font = [NSFont systemFontOfSize:60 * scale weight:NSFontWeightSemibold];
    NSShadow *textShadow = [[NSShadow alloc] init];
    textShadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.32];
    textShadow.shadowBlurRadius = 7 * scale;
    textShadow.shadowOffset = NSMakeSize(0, -4 * scale);
    NSDictionary *attributes = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: [NSColor colorWithWhite:1 alpha:self.enabled ? 1 : 0.7], NSShadowAttributeName: textShadow };
    NSSize titleSize = [self.title sizeWithAttributes:attributes];
    CGFloat arrowWidth = 60 * scale, gap = 34 * scale, centerY = NSMidY(rect);
    CGFloat x = NSMidX(rect) - (arrowWidth + gap + titleSize.width) / 2;
    NSBezierPath *arrow = [NSBezierPath bezierPath];
    [arrow moveToPoint:NSMakePoint(x, centerY - 29 * scale)];
    [arrow lineToPoint:NSMakePoint(x + arrowWidth, centerY)];
    [arrow lineToPoint:NSMakePoint(x, centerY + 29 * scale)];
    [arrow closePath];
    [NSGraphicsContext saveGraphicsState];
    [textShadow set];
    [[NSColor colorWithWhite:1 alpha:self.enabled ? 1 : 0.7] setFill];
    [arrow fill];
    [NSGraphicsContext restoreGraphicsState];
    [self.title drawAtPoint:NSMakePoint(x + arrowWidth + gap, centerY - titleSize.height / 2) withAttributes:attributes];
}
@end
static NSImageView *maage_ui_home_symbol(NSString *name, NSColor *color) {
    NSImageView *symbol = [[NSImageView alloc] init];
    if (@available(macOS 11.0, *)) {
        symbol.image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
        if (!symbol.image && [name isEqualToString:@"wrench.and.screwdriver"]) symbol.image = [NSImage imageWithSystemSymbolName:@"hammer" accessibilityDescription:nil];
    }
    symbol.imageScaling = NSImageScaleProportionallyUpOrDown;
    symbol.contentTintColor = color;
    return symbol;
}
/* Each side of a selection card has its own native hit area and hover surface. */
@interface MaaGELauncherHomeCardButton : MaaGELauncherHomeActionButton
@property(nonatomic, weak) NSView *hoverBackground;
@end
@implementation MaaGELauncherHomeCardButton
- (void)refreshHomeInteraction {
    /* Animate white opacity only, behind the icon, text and chevron.
     * The pressed/hovered segments retain the card's white color. */
    NSColor *fill = [NSColor colorWithWhite:1 alpha:self.homePressed ? 0.72 : self.homeHovered ? 0.80 : 0];
    maage_ui_home_animate_layer(self.hoverBackground.layer, @"backgroundColor", (__bridge id) fill.CGColor, 0.18);
}
@end
@interface MaaGELauncherHomeCard : NSView
@property(nonatomic, strong) NSView *symbolTile, *divider, *hoverSurface, *dropdownHighlight, *manageHighlight;
@property(nonatomic, strong) NSImageView *symbol, *downSymbol;
@property(nonatomic, strong) NSTextField *value;
@property(nonatomic, strong) MaaGELauncherHomeCardButton *dropdown, *manage;
@property(nonatomic, strong) NSTrackingArea *homeTracking;
@property(nonatomic, copy) NSString *accountName;
@property(nonatomic) BOOL homeHovered, verified, accountLabelDirty;
- (void)setAccountName:(NSString *)name verified:(BOOL)verified;
@end
@implementation MaaGELauncherHomeCard
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.homeTracking) [self removeTrackingArea:self.homeTracking];
    self.homeTracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:self.homeTracking];
}
- (void)refreshHomeHover {
    maage_ui_home_animate_layer(self.layer, @"shadowOpacity", @(self.homeHovered ? 0.35 : 0.23), 0.18);
}
- (void)setAccountName:(NSString *)name verified:(BOOL)verified {
    if ([_accountName isEqualToString:name] && _verified == verified) return;
    _accountName = [name copy];
    _verified = verified;
    self.accountLabelDirty = YES;
    [self setNeedsLayout:YES];
}
- (void)mouseEntered:(NSEvent *)event {
    (void) event;
    if (self.homeHovered) return;
    self.homeHovered = YES;
    [self refreshHomeHover];
}
- (void)mouseExited:(NSEvent *)event {
    (void) event;
    if (!self.homeHovered) return;
    self.homeHovered = NO;
    [self refreshHomeHover];
}
- (void)resetCursorRects {
    [super resetCursorRects];
    [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor];
}
- (void)layout {
    [super layout];
    CGFloat scale = NSHeight(self.bounds) / 110.0;
    if (scale <= 0) return;
    self.layer.cornerRadius = 28 * scale;
    self.layer.shadowRadius = 15 * scale;
    self.layer.shadowOffset = NSMakeSize(0, -6 * scale);
    /* Clip the independent hover surfaces to the SAME corner radius as
     * the outer card. Keep the outer layer unclipped for its drop shadow. */
    self.hoverSurface.frame = self.bounds;
    self.hoverSurface.layer.cornerRadius = self.layer.cornerRadius;
    self.symbolTile.frame = NSMakeRect(14 * scale, 18 * scale, 74 * scale, 74 * scale);
    self.symbolTile.layer.cornerRadius = 37 * scale;
    self.symbol.frame = NSMakeRect(28 * scale, 32 * scale, 46 * scale, 46 * scale);
    CGFloat fontSize = 28 * scale, available = (NSWidth(self.bounds) / scale - 250) * scale;
    CGFloat nameWidth = MAX(1, available - (self.verified ? 34 * scale : 0));
    NSString *value = self.accountName ?: self.value.stringValue;
    NSFont *font = [NSFont systemFontOfSize:fontSize weight:NSFontWeightMedium];
    CGFloat needed = [value sizeWithAttributes:@{ NSFontAttributeName: font }].width;
    if (needed > nameWidth && needed > 0) font = [NSFont systemFontOfSize:fontSize * nameWidth / needed weight:NSFontWeightMedium];
    BOOL fontChanged = fabs(self.value.font.pointSize - font.pointSize) > 0.1;
    if (fontChanged) self.value.font = font;
    if (self.accountName && (self.accountLabelDirty || fontChanged)) {
        if (self.verified) self.value.attributedStringValue = maage_ui_home_verified_name(self.accountName, font, scale);
        else self.value.stringValue = self.accountName;
        self.accountLabelDirty = NO;
    }
    self.value.frame = maage_ui_home_centered_text_rect(NSMakeRect(105 * scale, 0, available, NSHeight(self.bounds)), font);
    self.downSymbol.frame = NSMakeRect(NSWidth(self.bounds) - 119 * scale, 43 * scale, 23 * scale, 24 * scale);
    CGFloat backing = self.window.backingScaleFactor;
    if (backing < 1) backing = 2;
    CGFloat dividerX = round((NSWidth(self.bounds) - 74 * scale) * backing) / backing;
    CGFloat thickness = MAX(2.0 / backing, round(1.5 * scale * backing) / backing);
    self.divider.frame = NSMakeRect(dividerX, 0, thickness, NSHeight(self.bounds));
    self.dropdownHighlight.frame = NSMakeRect(0, 0, dividerX, NSHeight(self.bounds));
    self.manageHighlight.frame = NSMakeRect(dividerX + thickness, 0, NSWidth(self.bounds) - dividerX - thickness, NSHeight(self.bounds));
    self.dropdown.frame = self.dropdownHighlight.frame;
    self.manage.frame = self.manageHighlight.frame;
}
@end
static NSView *maage_ui_home_card(NSString *symbol, NSString *title, id target, SEL manageAction, NSInteger selection, BOOL account, NSTextField * __strong *display) {
    MaaGELauncherHomeCard *card = [[MaaGELauncherHomeCard alloc] init];
    card.wantsLayer = YES;
    card.layer.backgroundColor = [NSColor colorWithWhite:1 alpha:0.79].CGColor;
    card.layer.borderColor = [NSColor colorWithWhite:1 alpha:0.68].CGColor;
    card.layer.borderWidth = 0.8;
    card.layer.shadowColor = NSColor.blackColor.CGColor;
    card.layer.shadowOpacity = 0.23;
    card.layer.masksToBounds = NO;
    /* First child: a clipped, transparent hover surface behind every icon
     * and caption. Separate child layers maintain independent button states. */
    card.hoverSurface = [[NSView alloc] init];
    card.hoverSurface.wantsLayer = YES;
    card.hoverSurface.layer.masksToBounds = YES;
    [card addSubview:card.hoverSurface];
    card.dropdownHighlight = [[NSView alloc] init];
    card.dropdownHighlight.wantsLayer = YES;
    card.dropdownHighlight.layer.backgroundColor = [NSColor colorWithWhite:1 alpha:0].CGColor;
    [card.hoverSurface addSubview:card.dropdownHighlight];
    card.manageHighlight = [[NSView alloc] init];
    card.manageHighlight.wantsLayer = YES;
    card.manageHighlight.layer.backgroundColor = [NSColor colorWithWhite:1 alpha:0].CGColor;
    [card.hoverSurface addSubview:card.manageHighlight];
    NSColor *ink = [NSColor colorWithSRGBRed:0.19 green:0.25 blue:0.21 alpha:1];
    card.symbolTile = [[NSView alloc] init];
    card.symbolTile.wantsLayer = YES;
    card.symbolTile.layer.backgroundColor = [NSColor colorWithSRGBRed:0.40 green:0.62 blue:0.30 alpha:0.13].CGColor;
    [card addSubview:card.symbolTile];
    card.symbol = maage_ui_home_symbol(symbol, ink);
    [card addSubview:card.symbol];
    card.value = maage_ui_row_label(title, 12, YES);
    card.value.textColor = NSColor.blackColor;
    card.value.alignment = NSTextAlignmentLeft;
    [card addSubview:card.value];
    *display = card.value;
    if (account) {
        card.accountName = title;
        card.accountLabelDirty = YES;
    }
    card.downSymbol = maage_ui_home_symbol(@"chevron.down", ink);
    [card addSubview:card.downSymbol];
    card.dropdown = [[MaaGELauncherHomeCardButton alloc] init];
    card.dropdown.title = @"";
    card.dropdown.bordered = NO;
    card.dropdown.target = target;
    card.dropdown.action = @selector(showHomeDropdown:);
    card.dropdown.tag = selection;
    card.dropdown.hoverBackground = card.dropdownHighlight;
    NSString *selectionName = selection == 0 ? @"installation" : selection == 1 ? @"Client build" : selection == 2 ? @"account" : @"Playspace";
    card.dropdown.accessibilityLabel = [@"Choose " stringByAppendingString:selectionName];
    card.dropdown.toolTip = [@"Choose " stringByAppendingString:selectionName];
    [card addSubview:card.dropdown];
    card.manage = [[MaaGELauncherHomeCardButton alloc] init];
    card.manage.title = @"";
    card.manage.bordered = NO;
    card.manage.hoverBackground = card.manageHighlight;
    card.manage.target = target;
    card.manage.action = manageAction;
    if (@available(macOS 11.0, *)) card.manage.image = [NSImage imageWithSystemSymbolName:@"chevron.right" accessibilityDescription:@"Manage"];
    card.manage.imagePosition = NSImageOnly;
    card.manage.contentTintColor = ink;
    card.manage.toolTip = account ? @"Manage accounts" : selection == 1 ? @"Manage Client builds" : selection == 3 ? @"Manage Playspaces (coming later)" : @"Manage installations";
    card.manage.accessibilityLabel = @"Manage selection";
    [card addSubview:card.manage];
    /* Keep the physical-pixel-aligned divider above both hover surfaces. */
    card.divider = [[NSView alloc] init];
    card.divider.wantsLayer = YES;
    card.divider.layer.backgroundColor = [NSColor colorWithWhite:0 alpha:0.23].CGColor;
    [card addSubview:card.divider];
    /* The card owns explicit responsive frames, not an Auto Layout subgraph. */
    for (NSView *part in card.subviews) part.translatesAutoresizingMaskIntoConstraints = NO;
    card.dropdownHighlight.translatesAutoresizingMaskIntoConstraints = NO;
    card.manageHighlight.translatesAutoresizingMaskIntoConstraints = NO;
    return card;
}
static void maage_ui_home_set_value(NSTextField *label, NSString *value) {
    if ([label.stringValue isEqualToString:value]) return;
    label.stringValue = value;
    [label.superview setNeedsLayout:YES];
    [label.superview setNeedsDisplay:YES];
}
static NSTableCellView *maage_ui_row(NSView *content, CGFloat inset) {
    NSTableCellView *cell = [[NSTableCellView alloc] init];
    content.translatesAutoresizingMaskIntoConstraints = NO;
    [cell addSubview:content];
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor
                                              constant:inset],
        [content.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor
                                               constant:-inset],
        [content.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        [content.topAnchor constraintGreaterThanOrEqualToAnchor:cell.topAnchor
                                                       constant:4],
        [content.bottomAnchor constraintLessThanOrEqualToAnchor:cell.bottomAnchor
                                                       constant:-4]
    ]];
    return cell;
}

/* Native navigation with a gentle MaaGE-tinted, rounded selection. */
@interface MaaGELauncherNavigationRow : NSTableRowView
@end
@implementation MaaGELauncherNavigationRow
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    [[NSColor colorWithSRGBRed:0.86 green:0.19 blue:0.61 alpha:0.20] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 1, 2) xRadius:9 yRadius:9] fill];
}
@end
/* Installation rows retain a distinct accent-colored selection while the editor
 * has focus. Selection remains fully native for keyboard and accessibility. */
@interface MaaGELauncherInstallationRow : NSTableRowView
@end
@implementation MaaGELauncherInstallationRow
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    NSColor *accent = NSColor.controlAccentColor;
    NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 3, 2) xRadius:7 yRadius:7];
    [[accent colorWithAlphaComponent:0.24] setFill];
    [outline fill];
    [[accent colorWithAlphaComponent:0.85] setStroke];
    outline.lineWidth = 1.5;
    [outline stroke];
}
@end
static NSTableView *maage_ui_navigation_table(void) {
    NSTableView *table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 160, 150)];
    table.headerView = nil;
    table.rowHeight = 36;
    table.intercellSpacing = NSMakeSize(0, 4);
    table.autoresizingMask = NSViewWidthSizable;
    table.backgroundColor = NSColor.clearColor;
    table.focusRingType = NSFocusRingTypeNone;
    table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    if (@available(macOS 11.0, *)) table.style = NSTableViewStyleSourceList;
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"page"];
    column.width = 160;
    table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    [table addTableColumn:column];
    return table;
}
@interface MaaGELauncherController : NSObject <NSWindowDelegate, NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, assign) maage_launcher_ui *state;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTableView *sidebar, *settingsSidebar, *installations, *clients, *accounts;
@property(nonatomic, strong) NSView *shell, *body, *playSplash, *playHome;
@property(nonatomic, strong) NSImageView *splashLogo;
@property(nonatomic, strong) NSTextField *homeReadiness, *homeInstallation, *homeVersion, *homeAccount, *homePlayspace;
@property(nonatomic, strong) NSArray<NSView *> *pages;
@property(nonatomic, strong) NSPopUpButton *account, *installation;
@property(nonatomic, strong) NSButton *status, *repair, *verify, *play, *verifyResources, *retryResources, *cancelResources, *signIn, *deviceSignIn, *signOut, *removeAccountButton, *cancelAccount, *deviceCodeButton, *openDevice;
@property(nonatomic, strong) NSTextField *resourceStatus, *accountStatus, *error, *clientStatus, *launchStatus, *activeSummary, *authenticationStatus, *splashMessage, *splashDetail, *splashDeviceCode, *splashProgressSummary, *splashActivityLog;
@property(nonatomic, strong) NSMutableArray<NSString *> *splashActivityEntries;
@property(nonatomic, strong) NSString *lastSplashActivityKey;
@property(nonatomic, strong) NSStackView *splashActions, *splashDeviceActions;
@property(nonatomic, strong) NSButton *splashSignIn, *splashDeviceSignIn, *splashCancelAccount, *splashResourceRetry, *splashSkip;
@property(nonatomic, strong) NSProgressIndicator *progress, *splashProgress;
@property(nonatomic, strong) NSPopUpButton *appearance;
@property(nonatomic) int displayedAppearance;
@property(nonatomic) BOOL splashAnimating, splashDismissed, onboardingDecided, onboardingRequired;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) NSTextField *installationName, *installationArguments, *installationHeading, *installationHint;
@property(nonatomic, strong) NSPopUpButton *installationClient;
@property(nonatomic, strong) NSButton *createInstallationButton, *saveInstallationButton, *duplicateInstallationButton, *removeInstallationButton, *defaultInstallationButton, *activateInstallationButton, *cancelInstallationButton, *folderInstallationButton;
@property(nonatomic) unsigned editedInstallationID, accountRevision, clientRevision;
@property(nonatomic) BOOL hasEditedInstallation, creatingInstallation, updatingInstallationSelection;
@end
@implementation MaaGELauncherController
- (NSButton *)button:(NSString *)title action:(SEL)action {
    return [NSButton buttonWithTitle:title target:self action:action];
}
- (void)finish {
    [self.timer invalidate];
    self.timer = nil;
    [self.window orderOut:nil];
    [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)quit:(id)sender {
    (void) sender;
    [self finish];
}
- (BOOL)windowShouldClose:(NSWindow *)window {
    (void) window;
    [self finish];
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application {
    (void) application;
    [self finish];
    return NSTerminateCancel;
}
- (void)manage:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ACCOUNTS];
}
- (void)addAccount:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ACCOUNTS];
    maage_access_sign_in(self.state->access, 0);
    [self refresh:nil];
}
- (void)signOutAccount:(id)sender {
    (void) sender;
    if (*self.state->account) maage_access_sign_out(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)removeAccount:(id)sender {
    (void) sender;
    if (*self.state->account) maage_access_disconnect(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)repairAssets:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, MAAGE_ASSETS_REPAIR);
    [self refresh:nil];
}
- (void)verifyResources:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, MAAGE_ASSETS_VERIFY);
    [self refresh:nil];
}
- (void)retryResources:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, self.state->resource_mode);
    [self refresh:nil];
}
- (void)cancelResources:(id)sender {
    (void) sender;
    maage_launcher_resources_cancel(self.state);
    [self refresh:nil];
}
- (void)deviceSignIn:(id)sender {
    (void) sender;
    maage_access_sign_in(self.state->access, 1);
    [self refresh:nil];
}
- (void)cancelAccount:(id)sender {
    (void) sender;
    maage_access_cancel_operation(self.state->access);
    [self refresh:nil];
}
- (void)onboardSignIn:(id)sender {
    (void) sender;
    if (maage_status_failed(maage_access_sign_in(self.state->access, 0))) strcpy(self.state->error, "Cannot start Microsoft sign-in. Please retry.");
    [self refresh:nil];
}
- (void)onboardDeviceSignIn:(id)sender {
    [self deviceSignIn:sender];
}
- (void)onboardSkip:(id)sender {
    (void) sender;
    self.splashDismissed = YES;
    [self refresh:nil];
}
- (void)onboardRetryResources:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, MAAGE_ASSETS_PREPARE);
    [self refresh:nil];
}
- (void)copyCode:(id)sender {
    (void) sender;
    if (!*self.state->accounts.device_code) return;
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:@(self.state->accounts.device_code) forType:NSPasteboardTypeString];
}
- (void)openDevice:(id)sender {
    (void) sender;
    if (*self.state->accounts.device_url) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@(self.state->accounts.device_url)]];
}
- (void)showInstallations:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_INSTALLATIONS];
}
- (void)verifyAccount:(id)sender {
    (void) sender;
    if (!self.state->accounts.restored) maage_access_restore(self.state->access);
    else if (self.state->account[0]) maage_access_verify_local(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)playClient:(id)sender {
    (void) sender;
    maage_launcher_client_play(self.state);
    [self refresh:nil];
}
- (void)openProject:(id)sender {
    (void) sender;
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@MAAGE_PROJECT_URL]];
}
- (void)setHomeWindowAppearance:(BOOL)home {
    if (self.window.titlebarAppearsTransparent == home) return;
    self.window.titleVisibility = home ? NSWindowTitleHidden : NSWindowTitleVisible;
    self.window.titlebarAppearsTransparent = home;
    self.window.backgroundColor = home ? [NSColor colorWithSRGBRed:0.31 green:0.42 blue:0.51 alpha:1] : NSColor.windowBackgroundColor;
}
- (void)activatePage:(maage_launcher_page)page {
    self.state->page = page;
    if (page >= MAAGE_PAGE_SETTINGS) {
        NSInteger row = page == MAAGE_PAGE_ABOUT ? 1 : 0;
        [self.sidebar deselectAll:nil];
        if (self.settingsSidebar.selectedRow != row) [self.settingsSidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    } else {
        [self.settingsSidebar deselectAll:nil];
        if (self.sidebar.selectedRow != (NSInteger) page) [self.sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:page] byExtendingSelection:NO];
    }
    for (NSUInteger i = 0; i < self.pages.count; ++i)
        self.pages[i].hidden = i != (NSUInteger) page;
    BOOL home = page == MAAGE_PAGE_PLAY;
    BOOL splash = self.playSplash && !self.playSplash.hidden;
    self.shell.hidden = home || splash;
    self.playHome.hidden = !home || splash;
    BOOL showingHome = home && !splash;
    [self setHomeWindowAppearance:showingHome];
}
- (void)showSettings:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_SETTINGS];
}
- (void)showReadiness:(id)sender {
    (void) sender;
    [self activatePage:maage_launcher_attention_page(self.state)];
}
- (void)showAbout:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ABOUT];
}
/* The left side opens a native selection menu; the right side navigates to
 * the existing management screen. Playspaces remain an explicit placeholder. */
- (void)showHomeDropdown:(id)sender {
    NSButton *button = (NSButton *) sender;
    maage_launcher_ui *state = self.state;
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Choose"];
    menu.autoenablesItems = NO;
    NSInteger selection = button.tag;
    if (selection == 0) {
        for (unsigned i = 0; i < state->count; ++i) {
            maage_installation *installation = &state->installations[i];
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@(installation->name) action:@selector(selectHomeInstallation:) keyEquivalent:@""];
            item.target = self;
            item.tag = (NSInteger) i;
            item.state = installation->id == state->selected ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled = state->catalog_ready && !state->client_job;
            [menu addItem:item];
        }
    } else if (selection == 1) {
        for (unsigned i = 0; state->clients && i < state->clients->count; ++i) {
            maage_client_build *build = &state->clients->builds[i];
            char label[384];
            maage_client_build_label(build, label);
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@(label) action:@selector(selectHomeClient:) keyEquivalent:@""];
            item.target = self;
            item.representedObject = @(build->id);
            item.enabled = state->catalog_ready && state->clients_valid && !state->client_job && !state->launch_pending;
            for (unsigned j = 0; j < state->count; ++j)
                if (state->installations[j].id == state->selected && !strcmp(state->installations[j].client, build->id)) item.state = NSControlStateValueOn;
            [menu addItem:item];
        }
    } else if (selection == 2) {
        for (unsigned i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *profile = &state->accounts.profiles[i];
            if (!profile->connected) continue;
            NSString *title = [NSString stringWithFormat:@"%s%@", profile->name, profile->state == MAAGE_ACCESS_VERIFIED ? @" ✓" : @""];
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(selectHomeAccount:) keyEquivalent:@""];
            item.target = self;
            item.representedObject = @(profile->uuid);
            item.state = !strcmp(profile->uuid, state->account) ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled = state->accounts.metadata_ready && !state->accounts.busy;
            [menu addItem:item];
        }
    } else {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Default Playspace" action:nil keyEquivalent:@""];
        item.state = NSControlStateValueOn;
        item.enabled = NO;
        [menu addItem:item];
    }
    if (!menu.numberOfItems) {
        NSMenuItem *empty = [[NSMenuItem alloc] initWithTitle:@"No selections available" action:nil keyEquivalent:@""];
        empty.enabled = NO;
        [menu addItem:empty];
    }
    [menu addItem:NSMenuItem.separatorItem];
    NSString *manageTitle = selection == 0 ? @"Manage Installations…" : selection == 1 ? @"Manage Client Builds…" : selection == 2 ? @"Manage Accounts…" : @"Manage Playspaces (Coming Soon)";
    SEL manageAction = selection == 2 ? @selector(manage:) : selection == 3 ? @selector(showPlayspacePlaceholder:) : @selector(showInstallations:);
    NSMenuItem *manage = [[NSMenuItem alloc] initWithTitle:manageTitle action:manageAction keyEquivalent:@""];
    manage.target = self;
    manage.enabled = selection != 3;
    [menu addItem:manage];
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSHeight(button.bounds)) inView:button];
}
- (void)selectHomeInstallation:(NSMenuItem *)sender {
    unsigned index = (unsigned) sender.tag;
    if (index >= self.state->count || self.state->client_job) return;
    if (!maage_status_failed(maage_launcher_installation_select(self.state, index))) {
        self.creatingInstallation = NO;
        self.hasEditedInstallation = YES;
        self.editedInstallationID = self.state->installations[index].id;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)selectHomeClient:(NSMenuItem *)sender {
    if (!self.state->catalog_ready || !self.state->clients_valid || self.state->client_job || self.state->launch_pending) return;
    NSString *client = sender.representedObject;
    if (!client.length || maage_clients_find(self.state->clients, client.UTF8String) < 0) return;
    for (unsigned i = 0; i < self.state->count; ++i) {
        maage_installation *installation = &self.state->installations[i];
        if (installation->id != self.state->selected) continue;
        if (strcmp(installation->client, client.UTF8String) && !maage_status_failed(maage_launcher_installation_write(self.state, (int) i, installation->name, client.UTF8String, installation->arguments))) [self reloadInstallations];
        break;
    }
    [self refresh:nil];
}
- (void)selectHomeAccount:(NSMenuItem *)sender {
    NSString *uuid = sender.representedObject;
    if (!uuid.length || self.state->accounts.busy || !self.state->accounts.metadata_ready) return;
    if (!maage_status_failed(maage_launcher_account_select(self.state, uuid.UTF8String))) self.accountRevision = 0;
    [self refresh:nil];
}
- (void)showPlayspacePlaceholder:(id)sender {
    /* This can be invoked by the management arrow or by a popup menu item. */
    NSView *anchor = [sender isKindOfClass:[NSView class]] ? (NSView *) sender : ((MaaGELauncherHomeCard *) self.homePlayspace.superview).manage;
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Playspaces"];
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Playspace management is coming later." action:nil keyEquivalent:@""];
    item.enabled = NO;
    [menu addItem:item];
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, NSHeight(anchor.bounds)) inView:anchor];
}
- (void)selectAccount:(id)sender {
    (void) sender;
    NSString *uuid = self.account.selectedItem.representedObject;
    if (uuid) maage_launcher_account_select(self.state, uuid.UTF8String);
    else if (self.account.selectedItem.tag == 1) [self manage:nil];
    self.accountRevision = 0;
    [self refresh:nil];
}
- (void)selectInstallation:(id)sender {
    (void) sender;
    NSInteger row = self.installation.indexOfSelectedItem;
    if (row < 0 || (unsigned) row >= self.state->count) return;
    if (!maage_status_failed(maage_launcher_installation_select(self.state, (unsigned) row))) {
        self.creatingInstallation = NO;
        self.hasEditedInstallation = YES;
        self.editedInstallationID = self.state->installations[row].id;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table {
    return table == self.sidebar ? 3 : table == self.settingsSidebar ? 2
                                   : table == self.accounts          ? self.state->accounts.count
                                   : table == self.clients           ? self.state->clients->count
                                                                     : self.state->count;
}
- (NSTableRowView *)tableView:(NSTableView *)table rowViewForRow:(NSInteger)row {
    (void) row;
    if (table == self.sidebar || table == self.settingsSidebar) return [[MaaGELauncherNavigationRow alloc] init];
    return table == self.installations ? [[MaaGELauncherInstallationRow alloc] init] : nil;
}
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void) column;
    if (table == self.sidebar || table == self.settingsSidebar) {
        if (table == self.settingsSidebar) row += MAAGE_PAGE_SETTINGS;
        NSArray *names = @[ @"Play", @"Installations", @"Accounts", @"Settings", @"About" ];
        NSArray *symbols = @[ @"play.fill", @"square.stack", @"person.crop.circle", @"gearshape", @"info.circle" ];
        NSStackView *line = maage_ui_stack(YES, 11);
        if (@available(macOS 11.0, *)) {
            NSImageView *icon = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:symbols[row] accessibilityDescription:names[row]]];
            icon.contentTintColor = NSColor.secondaryLabelColor;
            [icon.widthAnchor constraintEqualToConstant:18].active = YES;
            [line addArrangedSubview:icon];
        }
        [line addArrangedSubview:maage_ui_row_label(names[row], 13, NO)];
        return maage_ui_row(line, 10);
    }
    if (table == self.accounts) {
        maage_access_profile *profile = &self.state->accounts.profiles[row];
        NSStackView *line = maage_ui_stack(NO, 4);
        [line addArrangedSubview:maage_ui_row_label(@(profile->name), 14, YES)];
        NSTextField *detail = maage_ui_row_label(@(maage_access_state_name(profile->state)), 12, NO);
        detail.textColor = NSColor.secondaryLabelColor;
        [line addArrangedSubview:detail];
        return maage_ui_row(line, 10);
    }
    if (table == self.clients) {
        maage_client_build *build = &self.state->clients->builds[row];
        char title[384];
        maage_client_build_label(build, title);
        NSStackView *line = maage_ui_stack(NO, 3);
        [line addArrangedSubview:maage_ui_row_label(@(title), 12, YES)];
        [line addArrangedSubview:maage_ui_row_label([NSString stringWithFormat:@"%s · %s · %.12s", build->platform, build->architecture, build->id], 11, NO)];
        return maage_ui_row(line, 6);
    }
    maage_installation *item = &self.state->installations[row];
    if ([column.identifier isEqualToString:@"role"]) {
        NSString *role = item->id == self.state->selected && item->id == self.state->default_id ? @"Active / Default"
                         : item->id == self.state->selected                                     ? @"Active"
                         : item->id == self.state->default_id                                   ? @"Default"
                                                                                                : @"—";
        NSTextField *label = maage_ui_row_label(role, 11, YES);
        label.textColor = NSColor.secondaryLabelColor;
        return maage_ui_row(label, 6);
    }
    NSStackView *line = maage_ui_stack(NO, 3);
    [line addArrangedSubview:maage_ui_row_label(@(item->name), 13, YES)];
    NSTextField *summary = maage_ui_row_label(@(maage_launcher_installation_client(self.state, (unsigned) row)), 11, NO);
    summary.textColor = NSColor.secondaryLabelColor;
    [line addArrangedSubview:summary];
    return maage_ui_row(line, 8);
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    if (notification.object == self.accounts) {
        NSInteger row = self.accounts.selectedRow;
        if (row >= 0 && (unsigned) row < self.state->accounts.count && strcmp(self.state->account, self.state->accounts.profiles[row].uuid)) {
            maage_launcher_account_select(self.state, self.state->accounts.profiles[row].uuid);
            [self refresh:nil];
        }
        return;
    }
    if (notification.object == self.settingsSidebar) {
        NSInteger row = self.settingsSidebar.selectedRow;
        if (row >= 0) [self activatePage:(maage_launcher_page) (MAAGE_PAGE_SETTINGS + row)];
        return;
    }
    if (notification.object == self.sidebar) {
        NSInteger row = self.sidebar.selectedRow;
        if (row < 0 || row >= MAAGE_PAGE_SETTINGS) return;
        [self activatePage:(maage_launcher_page) row];
    }
    if (notification.object == self.installations) {
        if (self.updatingInstallationSelection) return;
        NSInteger row = self.installations.selectedRow;
        if (row >= 0 && (unsigned) row < self.state->count) {
            self.creatingInstallation = NO;
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[row].id;
            [self bindInstallationEditor];
        } else if (!self.creatingInstallation) [self reloadInstallationRoles];
    }
    [self refresh:nil];
}
- (NSInteger)editedInstallationRow {
    if (!self.hasEditedInstallation) return -1;
    for (unsigned i = 0; i < self.state->count; ++i)
        if (self.state->installations[i].id == self.editedInstallationID) return (NSInteger) i;
    return -1;
}
- (NSInteger)activeInstallationRow {
    for (unsigned i = 0; i < self.state->count; ++i)
        if (self.state->installations[i].id == self.state->selected) return (NSInteger) i;
    return self.state->count ? 0 : -1;
}
- (void)installationClientChoices:(NSString *)selectedID {
    [self.installationClient removeAllItems];
    [self.installationClient addItemWithTitle:@"Client selection required"];
    self.installationClient.lastItem.representedObject = @"";
    [self.installationClient selectItemAtIndex:0];
    for (unsigned i = 0; i < self.state->clients->count; ++i) {
        maage_client_build *build = &self.state->clients->builds[i];
        char label[384];
        maage_client_build_label(build, label);
        [self.installationClient addItemWithTitle:@(label)];
        self.installationClient.lastItem.representedObject = @(build->id);
        if ([selectedID isEqualToString:@(build->id)]) [self.installationClient selectItem:self.installationClient.lastItem];
    }
    if (selectedID.length && maage_clients_find(self.state->clients, selectedID.UTF8String) < 0) {
        [self.installationClient addItemWithTitle:@"Referenced Client missing (keep reference)"];
        self.installationClient.lastItem.representedObject = selectedID;
        [self.installationClient selectItem:self.installationClient.lastItem];
    }
}
- (void)updateInstallationEditor {
    NSInteger row = [self editedInstallationRow];
    BOOL editing = !self.creatingInstallation && row >= 0;
    BOOL enabled = self.state->catalog_ready && !self.state->client_job;
    NSString *hint = @"Select a row on the left to edit its configuration.";
    if (self.creatingInstallation) {
        self.installationHeading.stringValue = @"Create installation";
        hint = @"Enter a name and optional Client build, then choose Create.";
    } else if (editing) {
        maage_installation *item = &self.state->installations[row];
        self.installationHeading.stringValue = @"Edit installation";
        if (item->id == self.state->selected && item->id == self.state->default_id) hint = @"Active for Play and Default. Save changes before selecting another row.";
        else if (item->id == self.state->selected) hint = @"Active for Play. Save changes before selecting another row.";
        else if (item->id == self.state->default_id) hint = @"Default installation. Not active for Play.";
        else hint = @"Selected for editing. Choose Make Active for Play to launch this one.";
    } else self.installationHeading.stringValue = @"Select an installation";
    if (![self.installationHint.stringValue isEqualToString:hint]) self.installationHint.stringValue = hint;
    self.createInstallationButton.enabled = enabled && !self.creatingInstallation;
    self.installationName.enabled = enabled && (editing || self.creatingInstallation);
    self.installationClient.enabled = enabled && (editing || self.creatingInstallation);
    self.installationArguments.enabled = enabled && (editing || self.creatingInstallation);
    self.saveInstallationButton.enabled = enabled && (editing || self.creatingInstallation);
    self.saveInstallationButton.title = self.creatingInstallation ? @"Create" : @"Save Changes";
    self.duplicateInstallationButton.enabled = enabled && editing;
    self.removeInstallationButton.enabled = enabled && editing && self.state->count > 1;
    self.defaultInstallationButton.enabled = enabled && editing && self.state->installations[row].id != self.state->default_id;
    self.activateInstallationButton.enabled = enabled && editing && self.state->installations[row].id != self.state->selected;
    self.folderInstallationButton.enabled = editing;
    self.cancelInstallationButton.hidden = !self.creatingInstallation;
    self.removeInstallationButton.hidden = self.creatingInstallation;
}
- (void)bindInstallationEditor {
    if (self.creatingInstallation) return;
    NSInteger row = [self editedInstallationRow];
    if (row >= 0) {
        maage_installation *item = &self.state->installations[row];
        self.installationName.stringValue = @(item->name);
        self.installationArguments.stringValue = @(item->arguments);
        [self installationClientChoices:@(item->client)];
    } else {
        self.installationName.stringValue = @"";
        self.installationArguments.stringValue = @"";
        [self installationClientChoices:@""];
    }
    [self updateInstallationEditor];
}
- (void)reloadInstallationRoles {
    NSInteger row = self.creatingInstallation ? -1 : [self editedInstallationRow];
    self.updatingInstallationSelection = YES;
    [self.installations reloadData];
    if (row >= 0) [self.installations selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    else [self.installations deselectAll:nil];
    self.updatingInstallationSelection = NO;
    NSInteger active = [self activeInstallationRow];
    if (active >= 0) [self.installation selectItemAtIndex:active];
    [self updateInstallationEditor];
}
- (void)reloadInstallations {
    [self.installation removeAllItems];
    for (unsigned i = 0; i < self.state->count; ++i)
        [self.installation addItemWithTitle:@(self.state->installations[i].name)];
    if (!self.creatingInstallation) {
        NSInteger row = [self editedInstallationRow];
        if (row < 0) row = [self activeInstallationRow];
        self.updatingInstallationSelection = YES;
        [self.installations reloadData];
        if (row >= 0) {
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[row].id;
            [self.installations selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [self.installations scrollRowToVisible:row];
        } else [self.installations deselectAll:nil];
        self.updatingInstallationSelection = NO;
        [self bindInstallationEditor];
    } else [self.installations reloadData];
    NSInteger active = [self activeInstallationRow];
    if (active >= 0) [self.installation selectItemAtIndex:active];
    [self refresh:nil];
}
- (void)newInstallation:(id)sender {
    (void) sender;
    if (!self.state->catalog_ready || self.state->client_job) return;
    self.creatingInstallation = YES;
    self.updatingInstallationSelection = YES;
    [self.installations deselectAll:nil];
    self.updatingInstallationSelection = NO;
    self.installationName.stringValue = @"New Installation";
    self.installationArguments.stringValue = @"";
    [self installationClientChoices:@""];
    [self updateInstallationEditor];
    [self.window makeFirstResponder:self.installationName];
    [self.installationName selectText:nil];
}
- (void)cancelInstallation:(id)sender {
    (void) sender;
    self.creatingInstallation = NO;
    [self reloadInstallations];
}
- (void)saveInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    BOOL creating = self.creatingInstallation;
    if (self.state->client_job || (!creating && row < 0)) return;
    NSString *clientID = self.installationClient.selectedItem.representedObject ?: @"";
    maage_status result = maage_launcher_installation_write(self.state, creating ? -1 : (int) row, self.installationName.stringValue.UTF8String, clientID.UTF8String, self.installationArguments.stringValue.UTF8String);
    if (!maage_status_failed(result)) {
        self.creatingInstallation = NO;
        if (creating && self.state->count) {
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[self.state->count - 1].id;
        }
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)duplicateInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.state->client_job || self.creatingInstallation || row < 0) return;
    maage_installation *item = &self.state->installations[row];
    NSString *copyName = [@(item->name) stringByAppendingString:@" Copy"];
    NSString *clientID = @(item->client), *arguments = @(item->arguments);
    maage_status result = maage_launcher_installation_write(self.state, -1, copyName.UTF8String, clientID.UTF8String, arguments.UTF8String);
    if (!maage_status_failed(result) && self.state->count) {
        self.hasEditedInstallation = YES;
        self.editedInstallationID = self.state->installations[self.state->count - 1].id;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)activateInstallation:(id)sender {
    NSInteger row = [self editedInstallationRow];
    if (sender == self.installations) {
        row = self.installations.clickedRow;
        if (row < 0 || (unsigned) row >= self.state->count) return;
        if ([self editedInstallationRow] != row) {
            self.editedInstallationID = self.state->installations[row].id;
            self.hasEditedInstallation = YES;
            [self bindInstallationEditor];
        }
    }
    if (self.creatingInstallation || row < 0) return;
    if (!maage_status_failed(maage_launcher_installation_select(self.state, (unsigned) row))) [self reloadInstallationRoles];
    [self refresh:nil];
}
- (void)removeInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.creatingInstallation || row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Remove Installation?";
    alert.informativeText = @"This instance will be removed from the Launcher and its gameplay folder archived. Worlds and private files remain recoverable; shared resources are preserved.";
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn && !maage_status_failed(maage_launcher_installation_remove(self.state, (unsigned) row))) {
        self.hasEditedInstallation = NO;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)defaultInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.creatingInstallation || row < 0) return;
    if (!maage_status_failed(maage_launcher_installation_default(self.state, (unsigned) row))) [self reloadInstallationRoles];
    [self refresh:nil];
}
- (void)openInstallationFolder:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    char path[MAAGE_PATH_CAPACITY];
    if (row < 0) return;
    if ((!maage_status_failed(maage_launcher_installation_directory(self.state, (unsigned) row, path))) &&
        ![NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(path) isDirectory:YES]])
        strcpy(self.state->error, "Cannot open the installation folder in Finder.");
    [self refresh:nil];
}
- (void)applyAppearance {
    self.window.appearance = self.state->appearance ? [NSAppearance appearanceNamed:self.state->appearance == 1 ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua] : nil;
    self.window.backgroundColor = NSColor.windowBackgroundColor;
}
- (void)appearanceChanged:(NSPopUpButton *)sender {
    int previous = self.state->appearance;
    self.state->appearance = (int) sender.indexOfSelectedItem;
    if (maage_status_failed(maage_launcher_ui_save(self.state))) {
        self.state->appearance = previous;
        [sender selectItemAtIndex:previous];
    }
    [self applyAppearance];
    [self refresh:nil];
}
- (void)refreshSplash {
    maage_launcher_ui *state = self.state;
    BOOL preparing, accountStep, splash, progressActive, hasDeviceCode;
    NSString *message = @"Preparing the Launcher…", *detail = @"Loading installations and saved accounts.";
    if (!self.onboardingDecided && state->catalog_finished && state->accounts.restored) {
        self.onboardingDecided = YES;
        self.onboardingRequired = state->accounts.count == 0;
    }
    preparing = self.onboardingRequired && state->accounts.allowed;
    accountStep = self.onboardingRequired && !state->accounts.allowed;
    if (preparing && state->progress.stage == MAAGE_SETUP_COMPLETE && !state->progress.busy && !state->resources_pending) self.splashDismissed = YES;
    splash = !self.splashDismissed && (!state->catalog_finished || state->accounts.busy || (self.onboardingRequired && (accountStep || state->resources_pending || state->progress.stage != MAAGE_SETUP_COMPLETE || state->progress.busy)));
    if (!splash) self.splashDismissed = YES;
    if (splash && self.onboardingDecided && accountStep) {
        if (state->accounts.busy) {
            message = *state->accounts.device_code ? @"Waiting for Microsoft sign-in…" : @"Verifying Minecraft Java Edition access…";
            detail = @"Your account is checked securely in the background.";
        } else if (!state->services_ready) {
            message = @"Account services unavailable";
            detail = @"Network services did not initialize. Reopen MaaGE or continue to Launcher settings.";
        } else if (maage_status_failed(state->accounts.result)) {
            message = @"Sign-in was not completed";
            detail = @"Try Microsoft sign-in again, or use a device code.";
        } else {
            message = @"Connect your Minecraft account";
            detail = @"Sign in with Microsoft to verify Minecraft Java Edition access.";
        }
    } else if (splash && preparing) {
        message = @"Preparing Minecraft resources…";
        detail = @"Downloading and checking the files needed to play.";
        if (!state->services_ready) {
            message = @"Resource services unavailable";
            detail = @"Network services did not initialize. Reopen MaaGE or continue to Launcher settings.";
        } else if (state->progress.stage == MAAGE_SETUP_FAILED || state->progress.stage == MAAGE_SETUP_CANCELLED) {
            message = @"Resource preparation needs attention";
            detail = @"Retry preparation or open the Launcher to review the details.";
        } else if (state->resources_pending && !state->progress.busy) {
            detail = @"Waiting for background resource preparation to begin.";
        } else if (state->progress.busy) {
            switch (state->progress.stage) {
            case MAAGE_SETUP_DOWNLOADING:
            case MAAGE_SETUP_CLIENT_ARCHIVE:
                detail = @"Downloading verified Minecraft resources.";
                break;
            case MAAGE_SETUP_CLIENT_EXTRACT:
            case MAAGE_SETUP_RECONSTRUCTING:
                detail = @"Preparing Minecraft textures, sounds, and other files.";
                break;
            case MAAGE_SETUP_VERIFYING:
                detail = @"Checking file integrity.";
                break;
            default:
                detail = @"Inspecting and preparing Minecraft resources.";
                break;
            }
        }
    } else if (splash && state->accounts.busy) {
        message = @"Restoring saved accounts…";
        detail = @"Checking your existing Minecraft account information.";
    }
    hasDeviceCode = splash && accountStep && state->accounts.busy && *state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds();
    self.splashMessage.stringValue = message;
    self.splashDetail.stringValue = detail;
    self.splashActions.hidden = !splash || !accountStep || state->accounts.busy;
    self.splashSignIn.enabled = self.splashDeviceSignIn.enabled = state->services_ready && state->accounts.restored;
    self.splashCancelAccount.hidden = !splash || !accountStep || !state->accounts.busy;
    self.splashDeviceActions.hidden = !hasDeviceCode;
    if (hasDeviceCode) self.splashDeviceCode.stringValue = @(state->accounts.device_code);
    self.splashResourceRetry.hidden = !splash || !preparing || state->progress.busy || state->resources_pending || (state->progress.stage != MAAGE_SETUP_FAILED && state->progress.stage != MAAGE_SETUP_CANCELLED);
    self.splashResourceRetry.enabled = state->services_ready;
    self.splashSkip.hidden = !splash || !self.onboardingRequired;
    self.shell.hidden = splash || state->page == MAAGE_PAGE_PLAY;
    self.playHome.hidden = splash || state->page != MAAGE_PAGE_PLAY;
    self.playSplash.hidden = !splash;
    BOOL showingHome = !splash && state->page == MAAGE_PAGE_PLAY;
    [self setHomeWindowAppearance:showingHome];
    progressActive = splash && (!self.onboardingDecided || state->accounts.busy || (preparing && state->services_ready && (state->resources_pending || state->progress.busy)));
    maage_setup_progress *progress = &state->progress;
    double percent = -1, downloaded = (progress->downloaded_bytes + progress->client_downloaded_bytes) / 1048576.0;
    NSString *phase = @"Preparing resources";
    NSString *summary = @"Discovering the resources to prepare…";
    if (preparing && progress->busy) {
        if (progress->stage == MAAGE_SETUP_DOWNLOADING && progress->total_objects) {
            percent = (double) progress->verified_objects * 100.0 / progress->total_objects;
            phase = progress->stage == MAAGE_SETUP_DOWNLOADING ? @"Downloading assets" : @"Checking asset objects";
            summary = [NSString stringWithFormat:@"%@ · %lu / %lu objects · %.1f MiB downloaded", phase, progress->verified_objects, progress->total_objects, downloaded];
        } else if (progress->stage == MAAGE_SETUP_CLIENT_ARCHIVE && !progress->client_archive_ready) {
            phase = @"Preparing client archive";
            if (progress->client_total_bytes > 0 && progress->client_downloaded_bytes > 0) percent = progress->client_downloaded_bytes * 100.0 / progress->client_total_bytes;
            summary = [NSString stringWithFormat:@"%@ · %.1f / %.1f MiB transferred", phase, progress->client_downloaded_bytes / 1048576.0, progress->client_total_bytes / 1048576.0];
        } else if (progress->stage == MAAGE_SETUP_RECONSTRUCTING && progress->total_logical_assets) {
            percent = (double) progress->reconstructed_files * 100.0 / progress->total_logical_assets;
            phase = @"Reconstructing resources";
            summary = [NSString stringWithFormat:@"%@ · %lu / %lu files", phase, progress->reconstructed_files, progress->total_logical_assets];
        } else if (progress->stage == MAAGE_SETUP_CLIENT_EXTRACT && progress->client_resource_count) {
            BOOL reconstructingClient = progress->total_logical_assets && progress->reconstructed_files >= progress->total_logical_assets;
            unsigned long finished = reconstructingClient ? progress->client_resources_processed : progress->client_resources_checked;
            percent = (double) finished * 100.0 / progress->client_resource_count;
            phase = reconstructingClient ? @"Reconstructing client resources" : @"Inspecting client resources";
            summary = [NSString stringWithFormat:@"%@ · %lu / %lu files", phase, finished, progress->client_resource_count];
        } else {
            switch (progress->stage) {
            case MAAGE_SETUP_METADATA:
                phase = @"Loading resource metadata";
                break;
            case MAAGE_SETUP_VERIFYING:
                phase = progress->objects_ready ? @"Finishing verification" : @"Checking installed asset objects";
                break;
            case MAAGE_SETUP_CLIENT_EXTRACT:
                phase = @"Inspecting client resources";
                break;
            default:
                phase = @"Preparing Minecraft resources";
                break;
            }
            if (progress->percent >= 0) percent = progress->percent;
            summary = [NSString stringWithFormat:@"%@ · %.1f MiB downloaded", phase, downloaded];
        }
    } else if (preparing && progress->stage == MAAGE_SETUP_COMPLETE) {
        phase = @"Resources ready";
        summary = @"Resource verification complete";
        percent = 100;
    } else if (preparing && progress->stage == MAAGE_SETUP_FAILED) {
        phase = @"Resource preparation failed";
        summary = @"Resource preparation needs attention";
    } else if (preparing && state->resources_pending) {
        phase = @"Waiting for resource preparation";
        summary = @"Preparing to check Minecraft resources…";
    }
    self.splashProgressSummary.hidden = !splash || !preparing;
    self.splashProgressSummary.stringValue = summary;
    self.splashProgress.hidden = !progressActive;
    self.splashProgress.indeterminate = !preparing || percent < 0;
    if (progressActive && self.splashProgress.indeterminate) [self.splashProgress startAnimation:nil];
    else {
        [self.splashProgress stopAnimation:nil];
        if (percent >= 0) self.splashProgress.doubleValue = MIN(100.0, MAX(0.0, percent));
    }
    self.splashActivityLog.hidden = !splash || !preparing;
    if (splash && preparing) {
        /* Only publish bounded, nonsecret UI milestones. No log files, paths, URLs,
         * OAuth data or arbitrary worker activity strings enter the splash feed. */
        unsigned long step = progress->total_objects ? (unsigned long) ((double) progress->verified_objects * 20.0 / progress->total_objects) : 0;
        unsigned long transferred = (unsigned long) (downloaded / 4.0);
        int phaseStep = percent >= 0 ? (int) (percent / 5.0) : -1;
        NSString *key = [NSString stringWithFormat:@"%d:%lu:%lu:%d:%d", (int) progress->stage, step, transferred, phaseStep, progress->busy];
        if (![key isEqualToString:self.lastSplashActivityKey]) {
            self.lastSplashActivityKey = key;
            if (!self.splashActivityEntries) self.splashActivityEntries = [NSMutableArray array];
            NSString *time = [NSDateFormatter localizedStringFromDate:[NSDate date] dateStyle:NSDateFormatterNoStyle timeStyle:NSDateFormatterMediumStyle];
            [self.splashActivityEntries addObject:[NSString stringWithFormat:@"%@  %@", time, summary]];
            if (self.splashActivityEntries.count > 5) [self.splashActivityEntries removeObjectAtIndex:0];
            self.splashActivityLog.stringValue = [self.splashActivityEntries componentsJoinedByString:@"\n"];
        }
    }
    BOOL animate = splash && !NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
    if (animate != self.splashAnimating) {
        if (animate) {
            CABasicAnimation *breathing = [CABasicAnimation animationWithKeyPath:@"transform.scale"];
            breathing.fromValue = @(0.99);
            breathing.toValue = @(1.015);
            breathing.duration = 6.0;
            breathing.autoreverses = YES;
            breathing.repeatCount = HUGE_VALF;
            breathing.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
            [self.splashLogo.layer addAnimation:breathing forKey:@"breathing"];
        } else
            [self.splashLogo.layer removeAnimationForKey:@"breathing"];
        self.splashAnimating = animate;
    }
}
- (void)refresh:(NSTimer *)timer {
    (void) timer;
    maage_launcher_ui_refresh(self.state);
    maage_launcher_ui *state = self.state;
    if (self.displayedAppearance != state->appearance) {
        self.displayedAppearance = state->appearance;
        [self.appearance selectItemAtIndex:state->appearance];
        [self applyAppearance];
    }
    self.appearance.enabled = state->catalog_ready;
    if (self.accountRevision != state->accounts.revision || !self.account.numberOfItems) {
        [self.account removeAllItems];
        for (unsigned i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i];
            if (!p->connected) continue;
            [self.account addItemWithTitle:[NSString stringWithFormat:@"%s · %s", p->name, maage_access_state_name(p->state)]];
            self.account.lastItem.representedObject = @(p->uuid);
            if (!strcmp(state->account, p->uuid)) [self.account selectItem:self.account.lastItem];
        }
        if (!self.account.numberOfItems) [self.account addItemWithTitle:@"No connected Minecraft account"];
        NSMenuItem *chosen = self.account.selectedItem;
        [self.account.menu addItem:NSMenuItem.separatorItem];
        [self.account addItemWithTitle:@"Manage Accounts…"];
        self.account.lastItem.tag = 1;
        [self.account selectItem:chosen];
        self.account.autoenablesItems = NO;
        self.accountRevision = state->accounts.revision;
        [self.accounts reloadData];
        for (unsigned i = 0; i < state->accounts.count; ++i)
            if (!strcmp(state->account, state->accounts.profiles[i].uuid)) {
                [self.accounts selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
                break;
            }
    }
    if (self.clientRevision != state->client_revision) {
        self.clientRevision = state->client_revision;
        [self.clients reloadData];
        if (self.installation.numberOfItems != state->count) [self reloadInstallations];
        else [self reloadInstallationRoles];
        NSString *clientID = self.installationClient.selectedItem.representedObject ?: @"";
        [self installationClientChoices:clientID];
    }
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (state->client_confirmation) {
        state->client_confirmation = 0;
        maage_client_build *build = &state->client_candidate.build;
        NSAlert *confirmation = [[NSAlert alloc] init];
        confirmation.messageText = *state->replacing_client ? @"Replace Client?" : @"Import Local Client?";
        confirmation.informativeText = [NSString stringWithFormat:@"Client %s + %s\n%s · %s · %s\n\n%@\n\nImport copies the entire package. Only import code you trust. The original remains unchanged.%@", build->version, build->build, build->build_type, build->platform, build->architecture, @(state->client_candidate.source), *state->replacing_client ? @"\n\nAll installations using the selected Client will switch to this build, then the old managed package will be removed." : @""];
        [confirmation addButtonWithTitle:*state->replacing_client ? @"Replace" : @"Import"];
        [confirmation addButtonWithTitle:@"Cancel"];
        if ([confirmation runModal] == NSAlertFirstButtonReturn) maage_launcher_client_import(state);
        else
            maage_launcher_client_cancel_import(state);
    }
#endif
    [self refreshSplash];
    self.clientStatus.stringValue = @(state->client_status);
    self.launchStatus.stringValue = @(state->launch_status);
    self.activeSummary.stringValue = @(state->active_summary);
    self.play.enabled = state->can_play;
    self.play.title = @"Play MaaGE";
    self.play.toolTip = state->can_play ? @"Launch MaaGE" : @(state->readiness ? state->readiness : "Launcher is preparing.");
    [(MaaGELauncherHomeView *) self.playHome updateReadiness:state->can_play ? @"Ready to Play" : @(state->readiness ? state->readiness : "Initializing…")
                                                    symbol:state->can_play ? @"checkmark.circle.fill" : state->client_process ? @"pause.circle.fill" : @"clock.fill"
                                                      tint:state->can_play ? [NSColor colorWithSRGBRed:0.48 green:0.81 blue:0.35 alpha:1] : [NSColor colorWithWhite:1 alpha:0.78]];
    /* These are the saved values used by Play, not the editor's draft choices. */
    NSString *installationLabel = state->catalog_ready ? @"Choose Installation" : @"Loading installation…";
    NSString *versionLabel = state->clients_valid ? @"Select Client build" : @"Client registry unavailable";
    NSString *versionDetail = versionLabel;
    for (unsigned i = 0; i < state->count; ++i) {
        maage_installation *installation = &state->installations[i];
        if (installation->id != state->selected) continue;
        installationLabel = @(installation->name);
        if (*installation->client && state->clients) {
            int index = maage_clients_find(state->clients, installation->client);
            if (index >= 0) {
                maage_client_build *build = &state->clients->builds[index];
                char label[384];
                maage_client_build_label(build, label);
                versionLabel = [NSString stringWithFormat:@"%s · %s", build->version, build->build_type];
                versionDetail = @(label);
            } else versionLabel = versionDetail = @"Referenced Client build unavailable";
        }
        break;
    }
    maage_ui_home_set_value(self.homeInstallation, installationLabel);
    maage_ui_home_set_value(self.homeVersion, versionLabel);
    self.homeInstallation.toolTip = installationLabel;
    self.homeVersion.toolTip = versionDetail;
    NSString *accountLabel = @"No account selected";
    BOOL accountVerified = NO;
    for (unsigned i = 0; i < state->accounts.count; ++i)
        if (!strcmp(state->account, state->accounts.profiles[i].uuid)) {
            accountLabel = @(state->accounts.profiles[i].name);
            accountVerified = state->accounts.profiles[i].state == MAAGE_ACCESS_VERIFIED;
            break;
        }
    MaaGELauncherHomeCard *accountCard = (MaaGELauncherHomeCard *) self.homeAccount.superview;
    [accountCard setAccountName:accountLabel verified:accountVerified];
    self.homeAccount.toolTip = accountVerified ? [accountLabel stringByAppendingString:@" · Verified"] : accountLabel;
    self.account.enabled = state->accounts.metadata_ready;
    self.accountStatus.stringValue = @(state->account_status);
    self.authenticationStatus.stringValue = @(state->authentication_status);
    BOOL accountAvailable = !state->accounts.busy;
    self.verify.enabled = accountAvailable && (state->account[0] || !state->accounts.restored);
    self.signIn.enabled = self.deviceSignIn.enabled = accountAvailable && state->accounts.restored;
    self.signOut.enabled = self.removeAccountButton.enabled = accountAvailable && state->accounts.restored && state->account[0];
    self.cancelAccount.enabled = state->accounts.busy;
    self.deviceCodeButton.enabled = self.openDevice.enabled = *state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds();
    self.status.title = @(state->readiness);
    self.resourceStatus.stringValue = @(state->resource_status);
    BOOL resourceAvailable = !state->progress.busy && !state->client_process && state->client_operation != 4 && !state->launch_pending;
    self.repair.enabled = self.verifyResources.enabled = self.retryResources.enabled = resourceAvailable;
    self.cancelResources.enabled = state->progress.busy || state->resources_pending;
    self.error.stringValue = @(state->error);
    self.progress.hidden = !state->progress.busy && !state->accounts.busy && self.playSplash.hidden;
    self.progress.indeterminate = !state->progress.busy || state->progress.percent < 0;
    if (!self.progress.hidden && self.progress.indeterminate) [self.progress startAnimation:nil];
    else {
        [self.progress stopAnimation:nil];
        self.progress.doubleValue = state->progress.percent >= 0 ? state->progress.percent : 0;
    }
    [self updateInstallationEditor];
}
- (NSView *)playPage {
    /* The native selection cards reuse saved installations, Client builds and
     * accounts. Playspace configuration is not implemented yet. */
    NSView *page = [[NSView alloc] init];
    NSStackView *references = maage_ui_stack(NO, 6);
    self.installation = [[NSPopUpButton alloc] init];
    self.installation.target = self;
    self.installation.action = @selector(selectInstallation:);
    self.installation.accessibilityLabel = @"Installation for Play";
    self.clientStatus = maage_ui_label(@"Loading Client builds…", 12, NO);
    self.launchStatus = maage_ui_label(@"Starting Launcher services…", 12, NO);
    [references addArrangedSubview:self.installation];
    [references addArrangedSubview:self.clientStatus];
    [references addArrangedSubview:self.launchStatus];
    maage_ui_fill(references, page, 0);

    MaaGELauncherHomeView *home = [[MaaGELauncherHomeView alloc] init];
    self.playHome = home;
    maage_ui_fill(home, self.window.contentView, 0);
    NSString *logoPath = [NSBundle.mainBundle pathForResource:@"icon" ofType:@"png"];
    NSImage *artwork = logoPath ? [[NSImage alloc] initWithContentsOfFile:logoPath] : nil;
    home.brand = [NSImageView imageViewWithImage:artwork ?: NSApp.applicationIconImage];
    home.brand.imageScaling = NSImageScaleProportionallyUpOrDown;
    home.brand.wantsLayer = YES;
    home.brand.layer.shadowColor = NSColor.blackColor.CGColor;
    home.brand.layer.shadowOpacity = 0.4;
    home.brand.layer.shadowRadius = 13;
    home.brand.layer.shadowOffset = NSMakeSize(0, -5);
    [home addSubview:home.brand];

    home.heading = maage_ui_row_label(@"MaaGE", 44, YES);
    home.heading.textColor = NSColor.whiteColor;
    home.heading.alignment = NSTextAlignmentCenter;
    NSShadow *titleShadow = [[NSShadow alloc] init];
    titleShadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.46];
    titleShadow.shadowBlurRadius = 12;
    titleShadow.shadowOffset = NSMakeSize(0, -5);
    home.heading.shadow = titleShadow;
    [home addSubview:home.heading];

    home.tagline = maage_ui_row_label(@"MINECRAFT AS A GAME ENGINE", 18, NO);
    home.tagline.textColor = [NSColor colorWithWhite:1 alpha:0.95];
    home.tagline.alignment = NSTextAlignmentCenter;
    NSShadow *detailShadow = [[NSShadow alloc] init];
    detailShadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.4];
    detailShadow.shadowBlurRadius = 7;
    detailShadow.shadowOffset = NSMakeSize(0, -2);
    NSShadow *taglineShadow = [[NSShadow alloc] init];
    taglineShadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.78];
    taglineShadow.shadowBlurRadius = 19;
    taglineShadow.shadowOffset = NSMakeSize(0, -5);
    home.tagline.shadow = taglineShadow;
    [home addSubview:home.tagline];
    home.taglineLeftRule = [[NSView alloc] init];
    home.taglineRightRule = [[NSView alloc] init];
    for (NSView *rule in @[ home.taglineLeftRule, home.taglineRightRule ]) {
        rule.wantsLayer = YES;
        rule.layer.backgroundColor = [NSColor colorWithWhite:1 alpha:0.82].CGColor;
        [home addSubview:rule];
    }

    MaaGELauncherPlayButton *play = [[MaaGELauncherPlayButton alloc] init];
    self.play = play;
    self.play.title = @"Play MaaGE";
    self.play.target = self;
    self.play.action = @selector(playClient:);
    self.play.bordered = NO;
    self.play.enabled = NO;
    self.play.keyEquivalent = @"\r";
    self.play.accessibilityLabel = @"Play MaaGE";
    play.homeRestOpacity = 1;
    home.playButton = self.play;
    [home addSubview:self.play];

    self.homeReadiness = maage_ui_row_label(@"Initializing…", 13, YES);
    self.homeReadiness.textColor = NSColor.whiteColor;
    self.homeReadiness.alignment = NSTextAlignmentCenter;
    self.homeReadiness.shadow = detailShadow;
    home.readinessLabel = self.homeReadiness;
    [home addSubview:self.homeReadiness];
    [home updateReadiness:@"Initializing…" symbol:@"clock.fill" tint:NSColor.whiteColor];

    home.cards = @[
        maage_ui_home_card(@"cube", @"Loading installation…", self, @selector(showInstallations:), 0, NO, &_homeInstallation),
        maage_ui_home_card(@"arrow.triangle.2.circlepath", @"No Client build", self, @selector(showInstallations:), 1, NO, &_homeVersion),
        maage_ui_home_card(@"person.2", @"No account selected", self, @selector(manage:), 2, YES, &_homeAccount),
        maage_ui_home_card(@"wrench.and.screwdriver", @"Default Playspace", self, @selector(showPlayspacePlaceholder:), 3, NO, &_homePlayspace)
    ];
    for (NSView *card in home.cards) [home addSubview:card];

    MaaGELauncherConfigurationButton *configuration = [[MaaGELauncherConfigurationButton alloc] init];
    configuration.target = self;
    configuration.action = @selector(showSettings:);
    configuration.homeRestOpacity = 0.92;
    configuration.toolTip = @"Launcher settings and About";
    home.configuration = configuration;
    [home addSubview:configuration];
    [configuration refreshHomeInteraction];

    home.notice = maage_ui_row_label(@"MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.", 10, NO);
    home.notice.textColor = [NSColor colorWithWhite:1 alpha:0.63];
    home.notice.alignment = NSTextAlignmentCenter;
    home.notice.shadow = detailShadow;
    [home addSubview:home.notice];
    home.version = maage_ui_row_label(@"v" MAAGE_LAUNCHER_VERSION, 10, NO);
    home.version.alignment = NSTextAlignmentCenter;
    home.version.textColor = [NSColor colorWithWhite:1 alpha:0.70];
    home.version.shadow = detailShadow;
    home.version.toolTip = @"Launcher " MAAGE_LAUNCHER_VERSION;
    [home addSubview:home.version];
    /* The home owns the reference-coordinate layout at every window size. */
    for (NSView *part in home.subviews) part.translatesAutoresizingMaskIntoConstraints = NO;
    home.hidden = YES;
    self.playSplash = [[MaaGELauncherSplashView alloc] init];
    maage_ui_fill(self.playSplash, self.window.contentView, 0);
    NSStackView *splashContent = maage_ui_stack(NO, 22);
    splashContent.alignment = NSLayoutAttributeCenterX;
    self.splashLogo = maage_ui_macos_app_icon(144);
    [splashContent addArrangedSubview:self.splashLogo];
    self.splashMessage = maage_ui_label(@"Loading your installations…", 15, NO);
    self.splashMessage.textColor = NSColor.labelColor;
    self.splashMessage.alignment = NSTextAlignmentCenter;
    [splashContent addArrangedSubview:self.splashMessage];
    [self.splashMessage.widthAnchor constraintLessThanOrEqualToConstant:480].active = YES;
    self.splashDetail = maage_ui_label(@"Loading installations and saved accounts.", 13, NO);
    self.splashDetail.textColor = NSColor.secondaryLabelColor;
    self.splashDetail.alignment = NSTextAlignmentCenter;
    [splashContent addArrangedSubview:self.splashDetail];
    [self.splashDetail.widthAnchor constraintLessThanOrEqualToConstant:500].active = YES;
    [splashContent setCustomSpacing:8 afterView:self.splashMessage];
    self.splashActions = maage_ui_stack(YES, 12);
    self.splashSignIn = [self button:@"Sign In with Microsoft" action:@selector(onboardSignIn:)];
    self.splashSignIn.bezelColor = [NSColor colorWithSRGBRed:0.18 green:0.36 blue:0.20 alpha:1];
    self.splashDeviceSignIn = [self button:@"Use Device Code" action:@selector(onboardDeviceSignIn:)];
    [self.splashActions addArrangedSubview:self.splashSignIn];
    [self.splashActions addArrangedSubview:self.splashDeviceSignIn];
    [splashContent addArrangedSubview:self.splashActions];
    self.splashCancelAccount = [self button:@"Cancel Sign In" action:@selector(cancelAccount:)];
    [splashContent addArrangedSubview:self.splashCancelAccount];
    self.splashDeviceActions = maage_ui_stack(NO, 10);
    self.splashDeviceActions.alignment = NSLayoutAttributeCenterX;
    NSTextField *deviceHint = maage_ui_label(@"Enter this code at the Microsoft sign-in page:", 12, NO);
    deviceHint.textColor = NSColor.secondaryLabelColor;
    [self.splashDeviceActions addArrangedSubview:deviceHint];
    self.splashDeviceCode = maage_ui_label(@"", 18, YES);
    self.splashDeviceCode.selectable = YES;
    [self.splashDeviceActions addArrangedSubview:self.splashDeviceCode];
    NSStackView *deviceButtons = maage_ui_stack(YES, 10);
    [deviceButtons addArrangedSubview:[self button:@"Copy Code" action:@selector(copyCode:)]];
    [deviceButtons addArrangedSubview:[self button:@"Open Microsoft Sign In" action:@selector(openDevice:)]];
    [self.splashDeviceActions addArrangedSubview:deviceButtons];
    [splashContent addArrangedSubview:self.splashDeviceActions];
    self.splashResourceRetry = [self button:@"Retry Resource Preparation" action:@selector(onboardRetryResources:)];
    [splashContent addArrangedSubview:self.splashResourceRetry];
    self.splashSkip = [self button:@"Continue to Launcher" action:@selector(onboardSkip:)];
    self.splashSkip.bordered = NO;
    self.splashSkip.font = [NSFont systemFontOfSize:12];
    [splashContent addArrangedSubview:self.splashSkip];
    splashContent.translatesAutoresizingMaskIntoConstraints = NO;
    [self.playSplash addSubview:splashContent];
    [NSLayoutConstraint activateConstraints:@[
        [splashContent.centerXAnchor constraintEqualToAnchor:self.playSplash.centerXAnchor],
        [splashContent.centerYAnchor constraintEqualToAnchor:self.playSplash.centerYAnchor],
        [splashContent.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.playSplash.leadingAnchor constant:18],
        [splashContent.trailingAnchor constraintLessThanOrEqualToAnchor:self.playSplash.trailingAnchor constant:-18]
    ]];
    self.splashProgress = [[NSProgressIndicator alloc] init];
    self.splashProgress.style = NSProgressIndicatorStyleBar;
    self.splashProgress.controlSize = NSControlSizeMini;
    self.splashProgress.indeterminate = YES;
    self.splashProgress.minValue = 0;
    self.splashProgress.maxValue = 100;
    self.splashProgress.accessibilityLabel = @"Onboarding and resource preparation progress";
    self.splashProgress.translatesAutoresizingMaskIntoConstraints = NO;
    [self.playSplash addSubview:self.splashProgress];
    [NSLayoutConstraint activateConstraints:@[
        [self.splashProgress.leadingAnchor constraintEqualToAnchor:self.playSplash.leadingAnchor constant:28],
        [self.splashProgress.trailingAnchor constraintEqualToAnchor:self.playSplash.trailingAnchor constant:-28],
        [self.splashProgress.bottomAnchor constraintEqualToAnchor:self.playSplash.bottomAnchor constant:-16],
        [self.splashProgress.heightAnchor constraintEqualToConstant:6]
    ]];
    self.splashProgressSummary = maage_ui_label(@"", 11, NO);
    self.splashProgressSummary.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    self.splashProgressSummary.textColor = NSColor.secondaryLabelColor;
    self.splashProgressSummary.lineBreakMode = NSLineBreakByTruncatingTail;
    self.splashProgressSummary.translatesAutoresizingMaskIntoConstraints = NO;
    [self.playSplash addSubview:self.splashProgressSummary];
    [NSLayoutConstraint activateConstraints:@[
        [self.splashProgressSummary.leadingAnchor constraintEqualToAnchor:self.splashProgress.leadingAnchor],
        [self.splashProgressSummary.trailingAnchor constraintLessThanOrEqualToAnchor:self.splashProgress.trailingAnchor],
        [self.splashProgressSummary.bottomAnchor constraintEqualToAnchor:self.splashProgress.topAnchor constant:-10]
    ]];
    self.splashActivityLog = maage_ui_label(@"", 10, NO);
    self.splashActivityLog.font = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    self.splashActivityLog.textColor = NSColor.tertiaryLabelColor;
    self.splashActivityLog.maximumNumberOfLines = 5;
    self.splashActivityLog.translatesAutoresizingMaskIntoConstraints = NO;
    [self.playSplash addSubview:self.splashActivityLog];
    [NSLayoutConstraint activateConstraints:@[
        [self.splashActivityLog.leadingAnchor constraintEqualToAnchor:self.splashProgress.leadingAnchor],
        [self.splashActivityLog.widthAnchor constraintLessThanOrEqualToAnchor:self.splashProgress.widthAnchor multiplier:0.75],
        [self.splashActivityLog.bottomAnchor constraintEqualToAnchor:self.splashProgressSummary.topAnchor constant:-8]
    ]];
    self.playSplash.hidden = YES;
    return page;
}
- (NSView *)accountsPage {
    NSStackView *groups = maage_ui_stack(NO, 14);
    [groups addArrangedSubview:maage_ui_label(@"Accounts", 24, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Select a Minecraft profile. Only verified Java Edition access enables Play.", 13, NO)];
    self.accounts = [[NSTableView alloc] init];
    self.accounts.headerView = nil;
    self.accounts.rowHeight = 54;
    self.accounts.delegate = self;
    self.accounts.dataSource = self;
    self.accounts.accessibilityLabel = @"Minecraft accounts";
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"profile"];
    column.width = 560;
    [self.accounts addTableColumn:column];
    self.accounts.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    NSScrollView *list = [[NSScrollView alloc] init];
    list.hasVerticalScroller = YES;
    list.documentView = self.accounts;
    [list.heightAnchor constraintEqualToConstant:130].active = YES;
    [groups addArrangedSubview:list];
    [list.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    self.accountStatus = maage_ui_label(@"", 13, NO);
    [groups addArrangedSubview:self.accountStatus];
    [self.accountStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    NSStackView *actions = maage_ui_stack(YES, 8);
    self.signIn = [self button:@"Sign In with Microsoft" action:@selector(addAccount:)];
    self.deviceSignIn = [self button:@"Use Device Code" action:@selector(deviceSignIn:)];
    self.verify = [self button:@"Refresh / Retry" action:@selector(verifyAccount:)];
    [actions addArrangedSubview:self.signIn];
    [actions addArrangedSubview:self.deviceSignIn];
    [actions addArrangedSubview:self.verify];
    [groups addArrangedSubview:actions];
    actions = maage_ui_stack(YES, 8);
    self.signOut = [self button:@"Sign Out" action:@selector(signOutAccount:)];
    self.removeAccountButton = [self button:@"Remove Account" action:@selector(removeAccount:)];
    self.cancelAccount = [self button:@"Cancel Sign In" action:@selector(cancelAccount:)];
    [actions addArrangedSubview:self.signOut];
    [actions addArrangedSubview:self.removeAccountButton];
    [actions addArrangedSubview:self.cancelAccount];
    [groups addArrangedSubview:actions];
    self.authenticationStatus = maage_ui_label(@"", 13, NO);
    self.authenticationStatus.selectable = YES;
    [groups addArrangedSubview:self.authenticationStatus];
    [self.authenticationStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    actions = maage_ui_stack(YES, 8);
    self.deviceCodeButton = [self button:@"Copy Code" action:@selector(copyCode:)];
    self.openDevice = [self button:@"Open Microsoft Sign In" action:@selector(openDevice:)];
    [actions addArrangedSubview:self.deviceCodeButton];
    [actions addArrangedSubview:self.openDevice];
    [groups addArrangedSubview:actions];
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (NSView *)installationsPage {
    NSStackView *page = maage_ui_stack(NO, 14);
    [page addArrangedSubview:maage_ui_label(@"Installations", 24, YES)];
    [page addArrangedSubview:maage_ui_label(@"Installed Clients", 15, YES)];
    self.clients = [[NSTableView alloc] init];
    self.clients.headerView = nil;
    NSTableColumn *clientColumn = [[NSTableColumn alloc] initWithIdentifier:@"client"];
    clientColumn.width = 640;
    [self.clients addTableColumn:clientColumn];
    self.clients.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    self.clients.delegate = self;
    self.clients.dataSource = self;
    self.clients.rowHeight = 45;
    self.clients.accessibilityLabel = @"Installed Client builds";
    NSScrollView *clientScroll = [[NSScrollView alloc] init];
    clientScroll.hasVerticalScroller = YES;
    clientScroll.borderType = NSBezelBorder;
    clientScroll.documentView = self.clients;
    [page addArrangedSubview:clientScroll];
    [clientScroll.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;
    [clientScroll.heightAnchor constraintEqualToConstant:82].active = YES;
    NSStackView *clientActions = maage_ui_stack(YES, 8);
#if MAAGE_LAUNCHER_DEVELOPMENT
    [clientActions addArrangedSubview:[self button:@"Import Local Client…" action:@selector(importClient:)]];
    NSButton *replace = [self button:@"Replace Client…" action:@selector(importClient:)];
    replace.tag = 1;
    [clientActions addArrangedSubview:replace];
#endif
    [clientActions addArrangedSubview:[self button:@"Remove Client…" action:@selector(removeClient:)]];
    [page addArrangedSubview:clientActions];

    NSStackView *workspace = maage_ui_stack(YES, 18);
    workspace.alignment = NSLayoutAttributeTop;
    NSStackView *left = maage_ui_stack(NO, 10);
    NSStackView *listHeading = maage_ui_stack(YES, 10);
    [listHeading addArrangedSubview:maage_ui_label(@"Installations", 17, YES)];
    [listHeading addArrangedSubview:[[NSView alloc] init]];
    self.createInstallationButton = [self button:@"+ New" action:@selector(newInstallation:)];
    [listHeading addArrangedSubview:self.createInstallationButton];
    [left addArrangedSubview:listHeading];
    [listHeading.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    NSTextField *selectionHint = maage_ui_label(@"Select a row to edit · Double-click to activate", 11, NO);
    selectionHint.textColor = NSColor.secondaryLabelColor;
    [left addArrangedSubview:selectionHint];
    [selectionHint.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    self.installations = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 350, 300)];
    self.installations.autoresizingMask = NSViewWidthSizable;
    self.installations.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
    self.installations.allowsEmptySelection = YES;
    self.installations.usesAlternatingRowBackgroundColors = YES;
    self.installations.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    self.installations.rowHeight = 46;
    self.installations.intercellSpacing = NSMakeSize(0, 3);
    self.installations.delegate = self;
    self.installations.dataSource = self;
    self.installations.target = self;
    self.installations.doubleAction = @selector(activateInstallation:);
    self.installations.accessibilityLabel = @"Installations — select a row to edit";
    NSTableColumn *nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"installation"];
    nameColumn.title = @"Installation";
    nameColumn.width = 210;
    [self.installations addTableColumn:nameColumn];
    NSTableColumn *roleColumn = [[NSTableColumn alloc] initWithIdentifier:@"role"];
    roleColumn.title = @"Role";
    roleColumn.width = 124;
    [self.installations addTableColumn:roleColumn];
    NSScrollView *listScroll = [[NSScrollView alloc] init];
    listScroll.hasVerticalScroller = YES;
    listScroll.borderType = NSBezelBorder;
    listScroll.documentView = self.installations;
    [left addArrangedSubview:listScroll];
    [listScroll.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    [listScroll.heightAnchor constraintEqualToConstant:364].active = YES;
    [workspace addArrangedSubview:left];

    NSStackView *editor = maage_ui_stack(NO, 9);
    self.installationHeading = maage_ui_label(@"Select an installation", 17, YES);
    [editor addArrangedSubview:self.installationHeading];
    self.installationHint = maage_ui_label(@"Select a row on the left to edit its configuration.", 12, NO);
    self.installationHint.textColor = NSColor.secondaryLabelColor;
    [editor addArrangedSubview:self.installationHint];
    [self.installationHint.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [editor setCustomSpacing:17 afterView:self.installationHint];
    [editor addArrangedSubview:maage_ui_label(@"Installation name", 12, YES)];
    self.installationName = [[NSTextField alloc] init];
    self.installationName.accessibilityLabel = @"Installation name";
    [editor addArrangedSubview:self.installationName];
    [editor addArrangedSubview:maage_ui_label(@"Client build", 12, YES)];
    self.installationClient = [[NSPopUpButton alloc] init];
    self.installationClient.accessibilityLabel = @"Client build";
    [editor addArrangedSubview:self.installationClient];
    [editor addArrangedSubview:maage_ui_label(@"Client CLI arguments", 12, YES)];
    self.installationArguments = [[NSTextField alloc] init];
    self.installationArguments.accessibilityLabel = @"Client CLI arguments";
    [editor addArrangedSubview:self.installationArguments];
    NSTextField *argumentsHint = maage_ui_label(@"Quote values containing spaces.", 11, NO);
    argumentsHint.textColor = NSColor.secondaryLabelColor;
    [editor addArrangedSubview:argumentsHint];

    NSStackView *primaryActions = maage_ui_stack(YES, 7);
    self.saveInstallationButton = [self button:@"Save Changes" action:@selector(saveInstallation:)];
    self.duplicateInstallationButton = [self button:@"Duplicate" action:@selector(duplicateInstallation:)];
    self.removeInstallationButton = [self button:@"Remove" action:@selector(removeInstallation:)];
    self.cancelInstallationButton = [self button:@"Cancel" action:@selector(cancelInstallation:)];
    [primaryActions addArrangedSubview:self.saveInstallationButton];
    [primaryActions addArrangedSubview:self.duplicateInstallationButton];
    [primaryActions addArrangedSubview:self.removeInstallationButton];
    [primaryActions addArrangedSubview:self.cancelInstallationButton];
    [editor addArrangedSubview:primaryActions];

    NSStackView *roleActions = maage_ui_stack(YES, 7);
    self.defaultInstallationButton = [self button:@"Set as Default" action:@selector(defaultInstallation:)];
    self.activateInstallationButton = [self button:@"Make Active for Play" action:@selector(activateInstallation:)];
    [roleActions addArrangedSubview:self.defaultInstallationButton];
    [roleActions addArrangedSubview:self.activateInstallationButton];
    [editor addArrangedSubview:roleActions];
    self.folderInstallationButton = [self button:@"Open installation folder" action:@selector(openInstallationFolder:)];
    [editor addArrangedSubview:self.folderInstallationButton];
    [workspace addArrangedSubview:editor];
    [left.widthAnchor constraintEqualToAnchor:workspace.widthAnchor multiplier:0.44].active = YES;
    [editor.widthAnchor constraintEqualToAnchor:workspace.widthAnchor multiplier:0.56 constant:-18].active = YES;
    [self.installationName.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [self.installationClient.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [self.installationArguments.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [page addArrangedSubview:workspace];
    [workspace.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    MaaGELauncherTopDocumentView *document = [[MaaGELauncherTopDocumentView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(page, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    [document.heightAnchor constraintGreaterThanOrEqualToAnchor:scroll.contentView.heightAnchor].active = YES;
    return scroll;
}
#if MAAGE_LAUNCHER_DEVELOPMENT
- (void)importClient:(id)sender {
    BOOL replacing = [sender tag] == 1;
    NSInteger row = self.clients.selectedRow;
    if (self.state->client_job || (replacing && row < 0)) return;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = replacing ? @"Replace Client" : @"Import Local Client";
    panel.message = @"Choose maage-client with its adjacent maage-client.json manifest. Import copies only those two files without running the Client.";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK) {
        if (replacing) maage_launcher_client_replace(self.state, (unsigned) row, panel.URL.path.UTF8String);
        else
            maage_launcher_client_probe(self.state, panel.URL.path.UTF8String);
    }
}
#endif
- (void)removeClient:(id)sender {
    (void) sender;
    NSInteger row = self.clients.selectedRow;
    if (row < 0 || self.state->client_job) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Remove Managed Client?";
    alert.informativeText = @"Unused local Client files will be deleted. Developer originals and shared Minecraft resources are preserved. Builds referenced by installations cannot be removed.";
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) maage_launcher_client_remove(self.state, (unsigned) row);
}
- (NSView *)settingsPage {
    NSStackView *groups = maage_ui_stack(NO, 12);
    [groups addArrangedSubview:maage_ui_label(@"Settings", 24, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Accounts and resources prepare in the background while you use the Launcher.", 13, NO)];
    [groups addArrangedSubview:maage_ui_label(@"Resources", 15, YES)];
    NSTextField *path = maage_ui_label(@(self.state->resources), 12, NO);
    path.selectable = YES;
    [path.widthAnchor constraintLessThanOrEqualToConstant:560].active = YES;
    [groups addArrangedSubview:path];
    self.resourceStatus = maage_ui_label(@"", 13, NO);
    [groups addArrangedSubview:self.resourceStatus];
    [self.resourceStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    NSStackView *maintenance = maage_ui_stack(YES, 8);
    self.verifyResources = [self button:@"Verify Resources" action:@selector(verifyResources:)];
    self.repair = [self button:@"Repair Resources" action:@selector(repairAssets:)];
    [maintenance addArrangedSubview:self.verifyResources];
    [maintenance addArrangedSubview:self.repair];
    [groups addArrangedSubview:maintenance];
    maintenance = maage_ui_stack(YES, 8);
    self.retryResources = [self button:@"Retry" action:@selector(retryResources:)];
    self.cancelResources = [self button:@"Cancel" action:@selector(cancelResources:)];
    [maintenance addArrangedSubview:self.retryResources];
    [maintenance addArrangedSubview:self.cancelResources];
    [groups addArrangedSubview:maintenance];
    [groups addArrangedSubview:maage_ui_label(@"Verify checks file contents. Repair also restores missing or damaged resources.", 12, NO)];
    [groups addArrangedSubview:maage_ui_label(@"Appearance", 15, YES)];
    NSPopUpButton *appearance = [[NSPopUpButton alloc] init];
    self.appearance = appearance;
    [appearance addItemsWithTitles:@[ @"System", @"Light", @"Dark" ]];
    [appearance selectItemAtIndex:self.state->appearance];
    appearance.target = self;
    appearance.action = @selector(appearanceChanged:);
    appearance.accessibilityLabel = @"Launcher appearance";
    [groups addArrangedSubview:appearance];
    [groups addArrangedSubview:maage_ui_label(@"About", 15, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Version, project information, and license notices.", 12, NO)];
    [groups addArrangedSubview:[self button:@"About MaaGE…" action:@selector(showAbout:)]];
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (NSView *)aboutPage {
    NSStackView *groups = maage_ui_stack(NO, 18);
    [groups addArrangedSubview:maage_ui_label(@"About MaaGE", 24, YES)];
    NSStackView *identity = maage_ui_stack(YES, 16);
    NSImageView *icon = maage_ui_macos_app_icon(64);
    [identity addArrangedSubview:icon];
    NSStackView *name = maage_ui_stack(NO, 5);
    [name addArrangedSubview:maage_ui_label(@"MaaGE", 28, YES)];
    NSTextField *subtitle = maage_ui_label(@"Minecraft as a Game Engine", 14, NO);
    subtitle.textColor = NSColor.secondaryLabelColor;
    [name addArrangedSubview:subtitle];
    [identity addArrangedSubview:name];
    [groups addArrangedSubview:identity];
    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [groups addArrangedSubview:separator];
    [separator.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:maage_ui_label(@"Launcher build", 16, YES)];
    NSTextField *version = maage_ui_label(@"Version " MAAGE_LAUNCHER_SEMANTIC_VERSION, 13, NO);
    NSTextField *revision = maage_ui_label(@"Git revision " MAAGE_LAUNCHER_GIT_HASH, 12, NO);
    version.selectable = YES;
    revision.selectable = YES;
    revision.textColor = NSColor.secondaryLabelColor;
    [groups addArrangedSubview:version];
    [groups addArrangedSubview:revision];
    NSBox *licenseSeparator = [[NSBox alloc] init];
    licenseSeparator.boxType = NSBoxSeparator;
    [groups addArrangedSubview:licenseSeparator];
    [licenseSeparator.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:maage_ui_label(@"Open source & attribution", 16, YES)];
    [groups addArrangedSubview:maage_ui_label(@"MaaGE's original source code is licensed under the Apache License, Version 2.0. Minecraft game resources have separate ownership and licensing.", 13, NO)];
    NSTextField *notice = maage_ui_label(@"MaaGE is an independent project and is not affiliated with or endorsed by Mojang Studios or Microsoft.", 12, NO);
    notice.textColor = NSColor.secondaryLabelColor;
    [groups addArrangedSubview:notice];
    NSStackView *actions = maage_ui_stack(YES, 10);
    [actions addArrangedSubview:[self button:@"View MaaGE on GitHub ↗" action:@selector(openProject:)]];
    [actions addArrangedSubview:[self button:@"Back to Settings" action:@selector(showSettings:)]];
    [groups addArrangedSubview:actions];
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (void)show {
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1000, 670) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MaaGE Launcher";
    self.window.minSize = NSMakeSize(700, 500);
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    [self applyAppearance];
    /* Replace the stark split-view divider with an integrated, fixed-width sidebar. */
    NSView *shell = [[NSView alloc] init];
    self.shell = shell;
    maage_ui_fill(shell, self.window.contentView, 0);
    NSView *sidebar = [[NSView alloc] init];
    NSView *right = [[NSView alloc] init];
    sidebar.translatesAutoresizingMaskIntoConstraints = NO;
    right.translatesAutoresizingMaskIntoConstraints = NO;
    [shell addSubview:sidebar];
    [shell addSubview:right];
    [NSLayoutConstraint activateConstraints:@[
        [sidebar.leadingAnchor constraintEqualToAnchor:shell.leadingAnchor],
        [sidebar.topAnchor constraintEqualToAnchor:shell.topAnchor],
        [sidebar.bottomAnchor constraintEqualToAnchor:shell.bottomAnchor],
        [sidebar.widthAnchor constraintEqualToConstant:194],
        [right.leadingAnchor constraintEqualToAnchor:sidebar.trailingAnchor],
        [right.trailingAnchor constraintEqualToAnchor:shell.trailingAnchor],
        [right.topAnchor constraintEqualToAnchor:shell.topAnchor],
        [right.bottomAnchor constraintEqualToAnchor:shell.bottomAnchor]
    ]];
    NSVisualEffectView *material = [[NSVisualEffectView alloc] init];
    material.material = NSVisualEffectMaterialSidebar;
    material.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    maage_ui_fill(material, sidebar, 0);
    self.sidebar = maage_ui_navigation_table();
    self.sidebar.dataSource = self;
    self.sidebar.delegate = self;
    self.sidebar.accessibilityLabel = @"Launcher navigation";

    NSImageView *mark = maage_ui_macos_app_icon(34);
    NSStackView *brandText = maage_ui_stack(NO, 2);
    [brandText addArrangedSubview:maage_ui_row_label(@"MaaGE", 16, YES)];
    NSTextField *descriptor = maage_ui_row_label(@"LAUNCHER", 10, YES);
    descriptor.textColor = NSColor.tertiaryLabelColor;
    [brandText addArrangedSubview:descriptor];
    NSStackView *brand = maage_ui_stack(YES, 10);
    [brand addArrangedSubview:mark];
    [brand addArrangedSubview:brandText];

    NSStackView *nav = maage_ui_stack(NO, 9);
    [nav addArrangedSubview:brand];
    [nav setCustomSpacing:26 afterView:brand];
    NSTextField *primaryTitle = maage_ui_row_label(@"MAIN", 10, YES);
    primaryTitle.textColor = NSColor.tertiaryLabelColor;
    [nav addArrangedSubview:primaryTitle];
    NSScrollView *navigation = [[NSScrollView alloc] init];
    navigation.drawsBackground = NO;
    navigation.documentView = self.sidebar;
    navigation.hasVerticalScroller = NO;
    navigation.hasHorizontalScroller = NO;
    [nav addArrangedSubview:navigation];
    [navigation.widthAnchor constraintEqualToAnchor:nav.widthAnchor].active = YES;
    self.settingsSidebar = maage_ui_navigation_table();
    self.settingsSidebar.dataSource = self;
    self.settingsSidebar.delegate = self;
    self.settingsSidebar.accessibilityLabel = @"Settings navigation";
    NSTextField *secondaryTitle = maage_ui_row_label(@"GENERAL", 10, YES);
    secondaryTitle.textColor = NSColor.tertiaryLabelColor;
    [nav addArrangedSubview:secondaryTitle];
    NSScrollView *settings = [[NSScrollView alloc] init];
    settings.drawsBackground = NO;
    settings.documentView = self.settingsSidebar;
    settings.hasVerticalScroller = NO;
    settings.hasHorizontalScroller = NO;
    [nav addArrangedSubview:settings];
    [settings.widthAnchor constraintEqualToAnchor:nav.widthAnchor].active = YES;
    [settings.heightAnchor constraintEqualToConstant:90].active = YES;
    maage_ui_fill(nav, sidebar, 18);
    NSStackView *content = maage_ui_stack(NO, 14);
    maage_ui_fill(content, right, 26);
    NSStackView *context = maage_ui_stack(YES, 14);
    self.activeSummary = maage_ui_row_label(@"Starting Launcher…", 12, YES);
    [context addArrangedSubview:self.activeSummary];
    [self.activeSummary setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [context addArrangedSubview:[[NSView alloc] init]];
    self.account = [[NSPopUpButton alloc] init];
    self.account.target = self;
    self.account.action = @selector(selectAccount:);
    self.account.accessibilityLabel = @"Active Minecraft account";
    [self.account.widthAnchor constraintLessThanOrEqualToConstant:270].active = YES;
    [context addArrangedSubview:self.account];
    [content addArrangedSubview:context];
    [context.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.body = [[NSView alloc] init];
    [content addArrangedSubview:self.body];
    [self.body.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.pages = @[ [self playPage], [self installationsPage], [self accountsPage], [self settingsPage], [self aboutPage] ];
    for (NSView *page in self.pages) {
        maage_ui_fill(page, self.body, 0);
        page.hidden = YES;
    }
    self.progress = [[NSProgressIndicator alloc] init];
    self.progress.style = NSProgressIndicatorStyleBar;
    self.progress.controlSize = NSControlSizeMini;
    self.progress.minValue = 0;
    self.progress.maxValue = 100;
    self.progress.accessibilityLabel = @"Background preparation progress";
    [content addArrangedSubview:self.progress];
    [self.progress.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    [self.progress.heightAnchor constraintEqualToConstant:6].active = YES;
    self.error = maage_ui_label(@"", 12, NO);
    self.error.textColor = NSColor.systemRedColor;
    [content addArrangedSubview:self.error];
    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [content addArrangedSubview:separator];
    [separator.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    NSStackView *footer = maage_ui_stack(YES, 12);
    NSTextField *version = maage_ui_label(@"Development build · " MAAGE_LAUNCHER_VERSION, 11, NO);
    version.textColor = NSColor.secondaryLabelColor;
    [footer addArrangedSubview:version];
    [footer addArrangedSubview:[[NSView alloc] init]];
    self.status = [self button:@"" action:@selector(showReadiness:)];
    self.status.bordered = NO;
    self.status.font = [NSFont systemFontOfSize:11];
    [footer addArrangedSubview:self.status];
    [content addArrangedSubview:footer];
    [footer.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    [self.sidebar reloadData];
    [self.settingsSidebar reloadData];
    [self activatePage:self.state->page];
    [self reloadInstallations];
    [self refresh:nil];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.25 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window displayIfNeeded];
    maage_launcher_ui_presented(self.state);
}
@end
maage_status maage_launcher_native_run(maage_launcher_ui *state) {
    if (!state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGELauncherController *controller = [[MaaGELauncherController alloc] init];
        controller.state = state;
        id previous = NSApp.delegate;
        NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init];
        NSMenuItem *item = [[NSMenuItem alloc] init];
        [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:@"About MaaGE" action:@selector(showAbout:) keyEquivalent:@""];
        about.target = controller;
        [application addItem:about];
        [application addItem:NSMenuItem.separatorItem];
        NSMenuItem *settings = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@","];
        settings.target = controller;
        [application addItem:settings];
        [application addItem:NSMenuItem.separatorItem];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(quit:) keyEquivalent:@"q"];
        quit.target = controller;
        [application addItem:quit];
        item.submenu = application;
        NSMenuItem *editItem = [[NSMenuItem alloc] init];
        [menu addItem:editItem];
        NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
        editItem.submenu = edit;
        [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
        [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
        [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
        [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        NSApp.mainMenu = menu;
        double windowStarted = maage_time_monotonic_seconds();
        [controller show];
        MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "AppKit main window creation/presentation: %.3f s.", maage_time_monotonic_seconds() - windowStarted));
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        NSApp.delegate = previous;
        NSApp.mainMenu = nil;
        controller.window.delegate = nil;
        [controller.window close];
        return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    }
}
