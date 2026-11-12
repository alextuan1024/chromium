// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/remote_cocoa/app_shim/borderless_fullscreen_controller.h"

#include <optional>

namespace {
NSHashTable<BorderlessFullscreenController*>* Controllers() {
  static NSHashTable<BorderlessFullscreenController*>* controllers =
      [NSHashTable weakObjectsHashTable];
  return controllers;
}
std::optional<NSApplicationPresentationOptions> saved_presentation_options;
}  // namespace

@implementation BorderlessFullscreenController {
  __weak NSWindow* _window;
  BOOL _active;
  NSRect _windowedFrame;
  NSWindowStyleMask _windowedStyle;
  BOOL _windowedShadow;
}

- (instancetype)initWithWindow:(NSWindow*)window {
  if ((self = [super init])) {
    _window = window;
    [Controllers() addObject:self];
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name in @[
           NSWindowDidBecomeMainNotification, NSWindowDidResignMainNotification
         ]) {
      [center addObserver:self
                 selector:@selector(updatePresentationOptions:)
                     name:name
                   object:nil];
    }
    [center addObserver:self
               selector:@selector(screenParametersChanged:)
                   name:NSApplicationDidChangeScreenParametersNotification
                 object:nil];
    [center addObserver:self
               selector:@selector(windowWillClose:)
                   name:NSWindowWillCloseNotification
                 object:window];
  }
  return self;
}

- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
  _active = NO;
  [self updatePresentationOptions:nil];
}

- (BOOL)active {
  return _active;
}

- (void)updatePresentationOptions:(NSNotification*)notification {
  if (!saved_presentation_options) {
    return;
  }
  BOOL any_fullscreen = NO;
  BOOL main_fullscreen = NO;
  for (BorderlessFullscreenController* controller in Controllers()) {
    if (controller.active) {
      any_fullscreen = YES;
      main_fullscreen |= controller->_window.isMainWindow;
    }
  }
  NSApplicationPresentationOptions options = *saved_presentation_options;
  if (main_fullscreen) {
    options &= ~(NSApplicationPresentationHideDock |
                 NSApplicationPresentationHideMenuBar);
    options |= NSApplicationPresentationAutoHideDock |
               NSApplicationPresentationAutoHideMenuBar;
  }
  NSApp.presentationOptions = options;
  if (!any_fullscreen) {
    saved_presentation_options.reset();
  }
}

- (void)enter {
  NSWindow* window = _window;
  NSScreen* screen = window.screen;
  if (_active || !screen) {
    return;
  }
  _windowedFrame = window.frame;
  _windowedStyle = window.styleMask;
  _windowedShadow = window.hasShadow;
  if (!saved_presentation_options) {
    saved_presentation_options = NSApp.presentationOptions;
  }
  _active = YES;
  window.styleMask = NSWindowStyleMaskBorderless;
  window.hasShadow = NO;
  [window setFrame:screen.frame display:YES];
  [window makeKeyAndOrderFront:nil];
  [self updatePresentationOptions:nil];
}

- (void)exit {
  if (!_active) {
    return;
  }
  _active = NO;
  NSWindow* window = _window;
  window.styleMask = _windowedStyle;
  window.hasShadow = _windowedShadow;
  [window setFrame:_windowedFrame display:YES];
  [self updatePresentationOptions:nil];
}

- (void)screenParametersChanged:(NSNotification*)notification {
  if (_active) {
    NSWindow* window = _window;
    NSScreen* screen = window.screen ?: NSScreen.mainScreen;
    if (screen) {
      [window setFrame:screen.frame display:YES];
    }
  }
}

- (void)windowWillClose:(NSNotification*)notification {
  _active = NO;
  [self updatePresentationOptions:nil];
}

@end
