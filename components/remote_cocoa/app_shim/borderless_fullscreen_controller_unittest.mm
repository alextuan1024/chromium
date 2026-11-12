// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/remote_cocoa/app_shim/borderless_fullscreen_controller.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

@interface BorderlessFullscreenTestWindow : NSWindow
@property(nonatomic) BOOL mainForTesting;
@end

@implementation BorderlessFullscreenTestWindow
@synthesize mainForTesting = _mainForTesting;
- (BOOL)isMainWindow {
  return _mainForTesting;
}
- (BOOL)canBecomeKeyWindow {
  return YES;
}
- (BOOL)canBecomeMainWindow {
  return YES;
}
@end

namespace {
// Focus cannot be acquired reliably by a background unit-test process.
void SetMainWindow(BorderlessFullscreenTestWindow* window, BOOL main) {
  window.mainForTesting = main;
  [NSNotificationCenter.defaultCenter
      postNotificationName:main ? NSWindowDidBecomeMainNotification
                                : NSWindowDidResignMainNotification
                    object:window];
}

class BorderlessFullscreenControllerTest : public PlatformTest {
 protected:
  void SetUp() override {
    PlatformTest::SetUp();
    window_ = [[BorderlessFullscreenTestWindow alloc]
        initWithContentRect:NSMakeRect(100, 100, 640, 480)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window_.releasedWhenClosed = NO;
    controller_ =
        [[BorderlessFullscreenController alloc] initWithWindow:window_];
  }

  void TearDown() override {
    [controller_ exit];
    [window_ close];
    controller_ = nil;
    window_ = nil;
    PlatformTest::TearDown();
  }

  BorderlessFullscreenTestWindow* window_;
  BorderlessFullscreenController* controller_;
};

TEST_F(BorderlessFullscreenControllerTest, CoversDisplayAndRestoresWindow) {
  const NSRect frame = window_.frame;
  const NSWindowStyleMask style = window_.styleMask;
  const BOOL shadow = window_.hasShadow;
  const NSApplicationPresentationOptions options = NSApp.presentationOptions;

  [controller_ enter];
  ASSERT_TRUE(controller_.active);
  EXPECT_TRUE(NSEqualRects(window_.frame, window_.screen.frame));
  EXPECT_TRUE(
      NSEqualSizes(window_.contentView.bounds.size, window_.screen.frame.size));
  EXPECT_FALSE(window_.hasShadow);
  // Repeated entry must not overwrite the saved windowed geometry.
  [controller_ enter];
  [controller_ exit];
  [controller_ exit];

  EXPECT_FALSE(controller_.active);
  EXPECT_TRUE(NSEqualRects(window_.frame, frame));
  EXPECT_EQ(window_.styleMask, style);
  EXPECT_EQ(window_.hasShadow, shadow);
  EXPECT_EQ(NSApp.presentationOptions, options);
}

TEST_F(BorderlessFullscreenControllerTest, ClosingClearsFullscreenState) {
  const NSApplicationPresentationOptions options = NSApp.presentationOptions;
  [controller_ enter];
  ASSERT_TRUE(controller_.active);
  [window_ close];
  EXPECT_FALSE(controller_.active);
  EXPECT_EQ(NSApp.presentationOptions, options);
}

TEST_F(BorderlessFullscreenControllerTest, ScreenChangeUpdatesFullscreenFrame) {
  [controller_ enter];
  [window_ setFrame:NSMakeRect(100, 100, 640, 480) display:NO];
  [NSNotificationCenter.defaultCenter
      postNotificationName:NSApplicationDidChangeScreenParametersNotification
                    object:NSApp];
  EXPECT_TRUE(NSEqualRects(window_.frame, window_.screen.frame));
}

TEST_F(BorderlessFullscreenControllerTest, PresentationFollowsMainWindow) {
  const NSApplicationPresentationOptions options = NSApp.presentationOptions;
  [controller_ enter];
  SetMainWindow(window_, YES);
  EXPECT_TRUE(NSApp.presentationOptions &
              NSApplicationPresentationAutoHideMenuBar);

  BorderlessFullscreenTestWindow* other =
      [[BorderlessFullscreenTestWindow alloc]
          initWithContentRect:NSMakeRect(200, 200, 400, 300)
                    styleMask:NSWindowStyleMaskTitled
                      backing:NSBackingStoreBuffered
                        defer:NO];
  other.releasedWhenClosed = NO;
  [other makeKeyAndOrderFront:nil];
  SetMainWindow(window_, NO);
  SetMainWindow(other, YES);
  EXPECT_EQ(NSApp.presentationOptions, options);
  BorderlessFullscreenController* other_controller =
      [[BorderlessFullscreenController alloc] initWithWindow:other];
  [other_controller enter];
  [controller_ exit];
  EXPECT_TRUE(NSApp.presentationOptions &
              NSApplicationPresentationAutoHideMenuBar);
  [other close];
  EXPECT_EQ(NSApp.presentationOptions, options);
}
TEST_F(BorderlessFullscreenControllerTest, ExitBeforeEntryDoesNotChangeWindow) {
  const NSRect frame = window_.frame;
  const NSWindowStyleMask style = window_.styleMask;
  const NSApplicationPresentationOptions options = NSApp.presentationOptions;
  [controller_ exit];
  EXPECT_FALSE(controller_.active);
  EXPECT_TRUE(NSEqualRects(window_.frame, frame));
  EXPECT_EQ(window_.styleMask, style);
  EXPECT_EQ(NSApp.presentationOptions, options);
}

TEST_F(BorderlessFullscreenControllerTest,
       InactiveScreenChangeKeepsWindowFrame) {
  const NSRect frame = window_.frame;
  [NSNotificationCenter.defaultCenter
      postNotificationName:NSApplicationDidChangeScreenParametersNotification
                    object:NSApp];
  EXPECT_TRUE(NSEqualRects(window_.frame, frame));
}

TEST_F(BorderlessFullscreenControllerTest, DestructionRestoresPresentation) {
  const NSApplicationPresentationOptions options = NSApp.presentationOptions;
  @autoreleasepool {
    [controller_ enter];
    SetMainWindow(window_, YES);
    ASSERT_TRUE(NSApp.presentationOptions &
                NSApplicationPresentationAutoHideMenuBar);
    controller_ = nil;
  }
  EXPECT_EQ(NSApp.presentationOptions, options);
}

TEST_F(BorderlessFullscreenControllerTest, CoversEachConnectedDisplay) {
  for (NSScreen* screen in NSScreen.screens) {
    [window_ setFrame:NSMakeRect(NSMinX(screen.frame) + 100,
                                 NSMinY(screen.frame) + 100, 640, 480)
              display:NO];
    ASSERT_EQ(window_.screen, screen);
    const NSRect windowed_frame = window_.frame;
    [controller_ enter];
    EXPECT_TRUE(NSEqualRects(window_.frame, screen.frame));
    EXPECT_TRUE(
        NSEqualSizes(window_.contentView.bounds.size, screen.frame.size));
    [controller_ exit];
    EXPECT_TRUE(NSEqualRects(window_.frame, windowed_frame));
  }
}

}  // namespace
