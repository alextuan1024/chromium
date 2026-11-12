// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REMOTE_COCOA_APP_SHIM_BORDERLESS_FULLSCREEN_CONTROLLER_H_
#define COMPONENTS_REMOTE_COCOA_APP_SHIM_BORDERLESS_FULLSCREEN_CONTROLLER_H_

#import <AppKit/AppKit.h>

#include "components/remote_cocoa/app_shim/remote_cocoa_app_shim_export.h"

// Fullscreen on the current desktop, including the display's camera strip.
REMOTE_COCOA_APP_SHIM_EXPORT
@interface BorderlessFullscreenController : NSObject
- (instancetype)initWithWindow:(NSWindow*)window;
@property(nonatomic, readonly) BOOL active;
- (void)enter;
- (void)exit;
@end

#endif  // COMPONENTS_REMOTE_COCOA_APP_SHIM_BORDERLESS_FULLSCREEN_CONTROLLER_H_
