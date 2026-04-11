#include "MacWindowUtils.h"
#include <Cocoa/Cocoa.h>

// 由 C++ 侧在 run_dual_pets.cpp 中实现，用于接收窗口运动学信息
extern "C" void OnWindowKinematics(bool dragging, double px, double py,
                                   double vx, double vy, double ax, double ay);

#if defined(__APPLE__)

// 简单的窗口移动跟踪器：监听 NSWindowDidMove 并以固定频率计算速度，调用 C++ 回调
@interface WindowMoveTracker : NSObject
// 在当前工程中使用的是手动引用计数（非 ARC），weak 属性不被支持，改为 assign
@property(nonatomic, assign) NSWindow *window;
@property(nonatomic) NSPoint lastPos;
@property(nonatomic) CFAbsoluteTime lastTime;
@property(nonatomic) BOOL dragging;
@property(nonatomic, strong) NSTimer *pollTimer;
@end

@implementation WindowMoveTracker

- (instancetype)initWithWindow:(NSWindow *)window {
    self = [super init];
    if (!self) return nil;
    _window = window;
    _lastPos = window.frame.origin;
    _lastTime = CFAbsoluteTimeGetCurrent();
    _dragging = NO;

    // 监听窗口移动事件：用来标记“当前处于拖动中”
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(windowMoved:)
                                                 name:NSWindowDidMoveNotification
                                               object:window];

    // 以固定帧率轮询位置，计算速度并调用 C++ 回调
    _pollTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                  target:self
                                                selector:@selector(pollKinematics)
                                                userInfo:nil
                                                 repeats:YES];
    return self;
}

- (void)windowMoved:(NSNotification *)note {
    // 标记当前这一小段时间内窗口正在被用户拖动
    _dragging = YES;
}

- (void)pollKinematics {
    if (self.window == nil) return;

    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    CFAbsoluteTime dt = now - _lastTime;
    if (dt <= 0) return;

    NSPoint p = self.window.frame.origin;
    double vx = (p.x - _lastPos.x) / dt;
    double vy = (p.y - _lastPos.y) / dt;

    // 将窗口当前位置和速度传递给 C++，目前加速度先传 0
    OnWindowKinematics(_dragging, p.x, p.y, vx, vy, 0.0, 0.0);

    _lastPos = p;
    _lastTime = now;

    // 如果这一帧之后没有再次触发 windowMoved，则认为拖动结束
    _dragging = NO;
}

@end

#endif // __APPLE__

void mac_configure_window(void *nswin) {
#if defined(__APPLE__)
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow*)nswin;
        if (!win) return;
        // Make transparent, titled (so controls appear), allow background drag and resizing
        // Use fullSizeContentView so the content view extends under the titlebar and can be transparent.
        [win setTitlebarAppearsTransparent:YES];
        [win setTitleVisibility:NSWindowTitleHidden];
        // Request full-size content view so our content can extend under the titlebar area
        [win setStyleMask:([win styleMask] | NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView)];
        [win setOpaque:NO];
        [win setBackgroundColor:[NSColor clearColor]];
        // Use normal window level so the window behaves like a regular app window
        [win setLevel:NSNormalWindowLevel];
        // Do not force the window to join all Spaces; use default collection behavior so it can be covered/cover others normally
        // allow moving by dragging background (useful when window has transparent regions)
        [win setMovableByWindowBackground:YES];
        // Remove shadow so the transparent edges are truly transparent
        [win setHasShadow:NO];
        // Ensure content view is layer-backed and transparent
        NSView *cv = [win contentView];
        // Prefer a full-size content view so transparency under the titlebar is respected
        [win setContentView:cv];
        // Make sure the content view's layer is allowed to show through the titlebar area
        if (@available(macOS 10.10, *)) {
            [win setTitlebarAppearsTransparent:YES];
        }

        // Helper: recursively make a view and its subviews layer-backed and non-opaque
        void (^makeTransparentRecursive)(NSView*) = ^(NSView* view) {
            if (!view) return;
            [view setWantsLayer:YES];
            if (view.layer) {
                view.layer.backgroundColor = [[NSColor clearColor] CGColor];
                view.layer.opaque = NO;
            }
            [view setOpaque:NO];
            for (NSView *sv in [view subviews]) {
                makeTransparentRecursive(sv);
            }
        };

        makeTransparentRecursive(cv);
        // By default, don't ignore mouse events (we will manage per-pixel later)
        [win setIgnoresMouseEvents:NO];

        // 创建并持有一个 WindowMoveTracker，用于向 C++ 侧报告窗口运动学信息
        static WindowMoveTracker *s_tracker = nil;
        if (!s_tracker) {
            s_tracker = [[WindowMoveTracker alloc] initWithWindow:win];
        }
    }
#endif
}

void mac_set_ignores_mouse_events(void *nswin, bool ignores) {
#if defined(__APPLE__)
    @autoreleasepool {
        NSWindow *win = (__bridge NSWindow*)nswin;
        if (!win) return;
        [win setIgnoresMouseEvents: ignores ? YES : NO];
    }
#endif
}
