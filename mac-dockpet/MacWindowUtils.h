#pragma once

#pragma once

#ifdef __APPLE__
// Keep this header C/C++ friendly: do NOT include Cocoa headers here to avoid
// Objective-C symbols leaking into C++ translation units. The .mm implementation
// will include Cocoa.
extern "C" {
    // nswin is an opaque pointer (NSWindow*) returned from sf::Window::getSystemHandle()
    void mac_configure_window(void *nswin);
    void mac_set_ignores_mouse_events(void *nswin, bool ignores);
}
#endif
