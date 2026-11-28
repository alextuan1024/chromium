// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/overview/tab_overview_controller.h"

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/overview/tab_overview_gesture_detector.h"
#include "chrome/browser/ui/views/tabs/overview/tab_overview_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

class TabOverviewControllerBrowserTest : public InProcessBrowserTest {
 protected:
  TabOverviewController* controller() {
    return TabOverviewController::From(browser());
  }

  TabStripModel* model() { return browser()->tab_strip_model(); }

  void AddTab() {
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, /*foreground=*/true);
  }

  void Click(views::View* button) {
    views::test::ButtonTestApi(static_cast<views::Button*>(button))
        .NotifyClick(ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(),
                                    gfx::Point(), ui::EventTimeForNow(),
                                    ui::EF_LEFT_MOUSE_BUTTON,
                                    ui::EF_LEFT_MOUSE_BUTTON));
  }

  // Reports one step of a trackpad pinch on the active tab, as the renderer
  // acks it.
  void AckPinch(blink::WebInputEvent::Type type,
                float scale,
                bool consumed_by_page) {
    blink::WebGestureEvent event(type, blink::WebInputEvent::kNoModifiers,
                                 base::TimeTicks::Now(),
                                 blink::WebGestureDevice::kTouchpad);
    if (type == blink::WebInputEvent::Type::kGesturePinchUpdate) {
      event.data.pinch_update.scale = scale;
    }
    event.SetNeedsWheelEvent(true);
    controller()->OnInputEventAck(
        *model()
             ->GetActiveWebContents()
             ->GetPrimaryMainFrame()
             ->GetRenderWidgetHost(),
        blink::mojom::InputEventResultSource::kMainThread,
        consumed_by_page ? blink::mojom::InputEventResultState::kConsumed
                         : blink::mojom::InputEventResultState::kNotConsumed,
        event);
  }
};

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest,
                       ChoosingTileActivatesTab) {
  AddTab();
  ASSERT_EQ(1, model()->active_index());
  ASSERT_TRUE(controller());

  controller()->Show();
  ASSERT_TRUE(controller()->IsShowing());
  TabOverviewView* view = controller()->view_for_testing();
  EXPECT_EQ(2u, view->GetTabsForTesting().size());

  Click(view->GetTileForTesting(model()->GetTabAtIndex(0)->GetHandle()));
  EXPECT_EQ(0, model()->active_index());
  EXPECT_FALSE(controller()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest,
                       ClosingTabKeepsOverviewOpen) {
  AddTab();
  AddTab();
  controller()->Show();
  ASSERT_TRUE(controller()->IsShowing());

  const tabs::TabHandle closed = model()->GetTabAtIndex(0)->GetHandle();
  Click(controller()->view_for_testing()->GetCloseButtonForTesting(closed));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return controller()->IsShowing() &&
           controller()->view_for_testing()->GetTabsForTesting().size() == 2u;
  }));
  EXPECT_EQ(2, model()->count());
  TabOverviewView* view = controller()->view_for_testing();
  EXPECT_FALSE(view->GetTileForTesting(closed));
}

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest,
                       GridScrollsWhenTilesExceedTheWindow) {
  for (int i = 0; i < 24; ++i) {
    AddTab();
  }
  controller()->Show();
  ASSERT_TRUE(controller()->IsShowing());
  TabOverviewView* view = controller()->view_for_testing();
  view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_GT(view->GetScrollableHeightForTesting(), view->height());
}

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest, PinchInOpensOverview) {
  AckPinch(blink::WebInputEvent::Type::kGesturePinchBegin, 1.0f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchUpdate, 0.9f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchUpdate, 0.6f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchEnd, 1.0f, false);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return controller()->IsInteractive(); }));
}

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest,
                       ShortPinchLeavesThePage) {
  AckPinch(blink::WebInputEvent::Type::kGesturePinchBegin, 1.0f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchUpdate, 0.9f, false);
  // Let the overview appear under the pinch if the page snapshot arrives.
  base::RunLoop().RunUntilIdle();
  AckPinch(blink::WebInputEvent::Type::kGesturePinchEnd, 1.0f, false);
  EXPECT_TRUE(base::test::RunUntil([&] { return !controller()->IsShowing(); }));
  EXPECT_FALSE(controller()->IsInteractive());
}

IN_PROC_BROWSER_TEST_F(TabOverviewControllerBrowserTest,
                       PinchHandledByPageDoesNotOpenOverview) {
  AckPinch(blink::WebInputEvent::Type::kGesturePinchBegin, 1.0f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchUpdate, 0.5f,
           /*consumed_by_page=*/true);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchUpdate, 0.5f, false);
  AckPinch(blink::WebInputEvent::Type::kGesturePinchEnd, 1.0f, false);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(controller()->IsShowing());
}
