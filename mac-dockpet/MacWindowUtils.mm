#include "MacWindowUtils.h"
#include <Cocoa/Cocoa.h>

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
