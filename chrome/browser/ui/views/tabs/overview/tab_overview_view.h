// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_VIEW_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"

class TabStripModel;

namespace gfx {
class ImageSkia;
class SlideAnimation;
}  // namespace gfx

namespace views {
class ImageView;
class ScrollView;
}  // namespace views

// A scrollable grid of tab thumbnails covering the page area of a browser
// window. It holds no pointers into the browser beyond tab handles; all
// actions are reported through the callbacks.
class TabOverviewView : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(TabOverviewView, views::View)

 public:
  using TabCallback = base::RepeatingCallback<void(tabs::TabHandle)>;

  TabOverviewView(TabCallback on_tab_chosen,
                  TabCallback on_tab_close_requested,
                  base::RepeatingClosure on_dismiss_requested);
  TabOverviewView(const TabOverviewView&) = delete;
  TabOverviewView& operator=(const TabOverviewView&) = delete;
  ~TabOverviewView() override;

  // Replaces the tiles with one per tab in `model`, keeping keyboard focus on
  // the same tab where possible.
  void SetTabs(TabStripModel* model);

  // Refreshes the title and favicon of `tab`'s tile.
  void UpdateTab(tabs::TabInterface* tab);

  // Moves keyboard focus to the active tab's tile.
  void FocusActiveTile();

  // The transition in which the page shrinks into its tile. `page_snapshot`
  // is a capture of the active tab's page, the same size as this view.
  // Progress 0 shows the page filling the view, 1 the grid at rest. The view
  // must be in a widget.
  void BeginTransition(const gfx::ImageSkia& page_snapshot);
  void SetTransitionProgress(float progress);
  // Animates from the current progress to `target`, then runs `done`.
  void AnimateTransitionTo(float target, base::OnceClosure done);
  // Drops the page snapshot overlay, leaving the grid at rest.
  void EndTransition();

  std::vector<tabs::TabHandle> GetTabsForTesting() const;
  int GetScrollableHeightForTesting() const;
  views::View* GetTileForTesting(tabs::TabHandle tab);
  views::View* GetCloseButtonForTesting(tabs::TabHandle tab);

  // views::View:
  void Layout(PassKey) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  class Grid;
  class Tile;

  Tile* FindTile(tabs::TabHandle tab) const;

  TabCallback on_tab_chosen_;
  TabCallback on_tab_close_requested_;
  base::RepeatingClosure on_dismiss_requested_;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<Grid> grid_ = nullptr;

  // Transition state. `page_clip_` clips `page_`, the snapshot of the page.
  raw_ptr<views::View> page_clip_ = nullptr;
  raw_ptr<views::ImageView> page_ = nullptr;
  // Where the active tile's thumbnail sits when the grid is at rest.
  gfx::RectF transition_tile_rect_;
  float transition_progress_ = 0.0f;
  float transition_start_ = 0.0f;
  float transition_target_ = 0.0f;
  base::OnceClosure transition_done_;
  std::unique_ptr<gfx::SlideAnimation> transition_animation_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_OVERVIEW_TAB_OVERVIEW_VIEW_H_
