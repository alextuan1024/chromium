// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/overview/tab_overview_view.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kGridPadding = 32;
constexpr int kTileGap = 24;
constexpr int kMinTileWidth = 240;
constexpr int kMaxTileWidth = 360;
constexpr int kCaptionHeight = 24;
constexpr int kCaptionGap = 8;
constexpr int kTileInset = 4;
constexpr int kThumbnailRadius = 8;
constexpr int kFaviconSize = 16;
constexpr int kCloseButtonSize = 20;

// Thumbnails are shown at 16:10, the same shape as the tiles' image area.
constexpr gfx::Size kThumbnailAspect(160, 100);

int ThumbnailHeightForWidth(int width) {
  return width * kThumbnailAspect.height() / kThumbnailAspect.width();
}

}  // namespace

// One tab: a thumbnail above a favicon, title and close button.
class TabOverviewView::Tile : public views::Button {
  METADATA_HEADER(Tile, views::Button)

 public:
  Tile(tabs::TabInterface* tab,
       bool active,
       TabOverviewView::TabCallback on_chosen,
       TabOverviewView::TabCallback on_close_requested)
      : views::Button(base::BindRepeating(on_chosen, tab->GetHandle())),
        tab_(tab->GetHandle()),
        active_(active) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetBorder(views::CreateRoundedRectBorder(
        2, kThumbnailRadius + kTileInset / 2,
        active ? ui::kColorFocusableBorderFocused : ui::kColorBubbleBorder));
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    GetViewAccessibility().SetIsSelected(active);

    thumbnail_ = AddChildView(std::make_unique<views::ImageView>());
    thumbnail_->SetCornerRadius(kThumbnailRadius);
    thumbnail_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSubtleEmphasisBackground, kThumbnailRadius));
    thumbnail_->SetCanProcessEventsWithinSubtree(false);

    favicon_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    favicon_->SetCanProcessEventsWithinSubtree(false);

    title_ = AddChildView(std::make_unique<views::Label>());
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetCanProcessEventsWithinSubtree(false);

    close_button_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(on_close_requested, tab_), vector_icons::kCloseIcon,
        12));
    // shortcut: prototype labels are English-only; localize before shipping.
    close_button_->GetViewAccessibility().SetName(u"Close tab");
    close_button_->SetTooltipText(u"Close tab");

    Update(tab);

    if (auto* helper = ThumbnailTabHelper::From(tab)) {
      scoped_refptr<ThumbnailImage> thumbnail = helper->thumbnail();
      subscription_ = thumbnail->Subscribe();
      subscription_->SetSizeHint(kThumbnailAspect);
      subscription_->SetUncompressedImageCallback(base::BindRepeating(
          &Tile::OnThumbnailAvailable, base::Unretained(this)));
      thumbnail->RequestThumbnailImage();
    }
  }

  tabs::TabHandle tab() const { return tab_; }
  bool active() const { return active_; }
  views::View* thumbnail_view() { return thumbnail_; }
  views::ImageButton* close_button() { return close_button_; }

  // Shows `snapshot`, a capture of the whole page, in place of the cached
  // thumbnail.
  void SetSnapshot(const gfx::ImageSkia& snapshot) {
    has_snapshot_ = true;
    ShowThumbnail(gfx::ImageSkiaOperations::ExtractSubset(
        snapshot,
        gfx::Rect(snapshot.width(),
                  std::min(snapshot.height(),
                           ThumbnailHeightForWidth(snapshot.width())))));
  }

  void Update(tabs::TabInterface* tab) {
    content::WebContents* contents = tab->GetContents();
    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
    }
    title_->SetText(title);
    GetViewAccessibility().SetName(title);
    SetTooltipText(title);

    ui::ImageModel favicon = favicon::GetDefaultFaviconModel();
    auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents);
    if (driver && !driver->GetFavicon().IsEmpty()) {
      favicon = ui::ImageModel::FromImage(driver->GetFavicon());
    }
    favicon_->SetImage(favicon);
    if (!has_thumbnail_) {
      // The favicon stands in until a page thumbnail arrives (discarded tabs
      // may never get one).
      thumbnail_->SetImageSize(gfx::Size(32, 32));
      thumbnail_->SetImage(favicon);
    }
  }

  // views::Button:
  void Layout(PassKey) override {
    gfx::Rect bounds = GetContentsBounds();
    bounds.Inset(kTileInset);
    const int thumbnail_height = ThumbnailHeightForWidth(bounds.width());
    thumbnail_->SetBounds(bounds.x(), bounds.y(), bounds.width(),
                          thumbnail_height);
    if (has_thumbnail_) {
      thumbnail_->SetImageSize(thumbnail_->size());
    }
    const int caption_y = bounds.y() + thumbnail_height + kCaptionGap;
    favicon_->SetBounds(bounds.x(),
                        caption_y + (kCaptionHeight - kFaviconSize) / 2,
                        kFaviconSize, kFaviconSize);
    close_button_->SetBounds(
        bounds.right() - kCloseButtonSize,
        caption_y + (kCaptionHeight - kCloseButtonSize) / 2, kCloseButtonSize,
        kCloseButtonSize);
    const int title_x = favicon_->bounds().right() + 8;
    title_->SetBounds(title_x, caption_y,
                      std::max(0, close_button_->x() - 8 - title_x),
                      kCaptionHeight);
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_LEFT ||
        event.key_code() == ui::VKEY_RIGHT) {
      GetFocusManager()->AdvanceFocus(event.key_code() == ui::VKEY_LEFT);
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }

  // Height of a tile that is `width` wide.
  static int HeightForTileWidth(int width) {
    const int inner_width = width - 2 * (kTileInset + 2);
    return ThumbnailHeightForWidth(inner_width) + kCaptionGap + kCaptionHeight +
           2 * (kTileInset + 2);
  }

 private:
  void OnThumbnailAvailable(gfx::ImageSkia image) {
    // A page snapshot is sharper than any cached thumbnail.
    if (!has_snapshot_) {
      ShowThumbnail(image);
    }
  }

  void ShowThumbnail(const gfx::ImageSkia& image) {
    has_thumbnail_ = true;
    thumbnail_->SetImageSize(thumbnail_->size());
    thumbnail_->SetImage(ui::ImageModel::FromImageSkia(image));
  }

  const tabs::TabHandle tab_;
  const bool active_;
  bool has_thumbnail_ = false;
  bool has_snapshot_ = false;
  raw_ptr<views::ImageView> thumbnail_ = nullptr;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  std::unique_ptr<ThumbnailImage::Subscription> subscription_;
};

BEGIN_METADATA(TabOverviewView, Tile)
END_METADATA

// Lays tiles out in equal columns, as many as fit the available width.
class TabOverviewView::Grid : public views::View {
  METADATA_HEADER(Grid, views::View)

 public:
  explicit Grid(base::RepeatingClosure on_background_pressed)
      : on_background_pressed_(std::move(on_background_pressed)) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
    // shortcut: prototype labels are English-only; localize before shipping.
    GetViewAccessibility().SetName(u"Tab overview");
  }

  void TilesChanged() { PreferredSizeChanged(); }

  std::vector<Tile*> tiles() const {
    std::vector<Tile*> tiles;
    for (views::View* child : children()) {
      tiles.push_back(static_cast<Tile*>(child));
    }
    return tiles;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int preferred_width = available_size.width().is_bounded()
                                    ? available_size.width().value()
                                    : width();
    return gfx::Size(preferred_width, HeightForWidth(preferred_width));
  }

  int HeightForWidth(int available_width) const {
    const Metrics metrics = GetMetrics(available_width);
    const int rows =
        (static_cast<int>(children().size()) + metrics.columns - 1) /
        metrics.columns;
    return 2 * kGridPadding + rows * metrics.tile_height +
           std::max(0, rows - 1) * kTileGap;
  }

  void Layout(PassKey) override {
    const Metrics metrics = GetMetrics(width());
    int index = 0;
    for (views::View* child : children()) {
      const int column = index % metrics.columns;
      const int row = index / metrics.columns;
      child->SetBounds(metrics.left + column * (metrics.tile_width + kTileGap),
                       kGridPadding + row * (metrics.tile_height + kTileGap),
                       metrics.tile_width, metrics.tile_height);
      ++index;
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    on_background_pressed_.Run();
    return true;
  }

 private:
  struct Metrics {
    int columns;
    int tile_width;
    int tile_height;
    int left;
  };

  Metrics GetMetrics(int available_width) const {
    const int count = std::max<int>(1, children().size());
    const int usable =
        std::max(kMinTileWidth, available_width - 2 * kGridPadding);
    const int columns =
        std::clamp((usable + kTileGap) / (kMinTileWidth + kTileGap), 1, count);
    const int tile_width =
        std::min(kMaxTileWidth, (usable - (columns - 1) * kTileGap) / columns);
    const int used = columns * tile_width + (columns - 1) * kTileGap;
    return {columns, tile_width, Tile::HeightForTileWidth(tile_width),
            std::max(kGridPadding, (available_width - used) / 2)};
  }

  base::RepeatingClosure on_background_pressed_;
};

BEGIN_METADATA(TabOverviewView, Grid)
END_METADATA

TabOverviewView::TabOverviewView(TabCallback on_tab_chosen,
                                 TabCallback on_tab_close_requested,
                                 base::RepeatingClosure on_dismiss_requested)
    : on_tab_chosen_(std::move(on_tab_chosen)),
      on_tab_close_requested_(std::move(on_tab_close_requested)),
      on_dismiss_requested_(std::move(on_dismiss_requested)) {
  SetBackground(views::CreateSolidBackground(ui::kColorWindowBackground));
  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetDrawOverflowIndicator(false);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // Bounding the height makes the scroll view size the grid to the height
  // its tiles need at the viewport's width.
  scroll_view_->ClipHeightTo(0, std::numeric_limits<int>::max());
  grid_ =
      scroll_view_->SetContents(std::make_unique<Grid>(on_dismiss_requested_));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

TabOverviewView::~TabOverviewView() = default;

void TabOverviewView::SetTabs(TabStripModel* model) {
  tabs::TabHandle focused;
  for (Tile* tile : grid_->tiles()) {
    if (tile->HasFocus() || tile->close_button()->HasFocus()) {
      focused = tile->tab();
    }
  }
  grid_->RemoveAllChildViews();
  tabs::TabInterface* active = model->GetActiveTab();
  for (tabs::TabInterface* tab : *model) {
    grid_->AddChildView(std::make_unique<Tile>(
        tab, tab == active, on_tab_chosen_, on_tab_close_requested_));
  }
  grid_->TilesChanged();
  InvalidateLayout();
  Tile* tile = FindTile(focused);
  if (tile) {
    tile->RequestFocus();
  } else if (GetWidget()) {
    FocusActiveTile();
  }
}

void TabOverviewView::UpdateTab(tabs::TabInterface* tab) {
  if (Tile* tile = FindTile(tab->GetHandle())) {
    tile->Update(tab);
  }
}

void TabOverviewView::FocusActiveTile() {
  for (Tile* tile : grid_->tiles()) {
    if (tile->active()) {
      tile->RequestFocus();
      tile->ScrollViewToVisible();
      return;
    }
  }
}

std::vector<tabs::TabHandle> TabOverviewView::GetTabsForTesting() const {
  std::vector<tabs::TabHandle> tabs;
  for (Tile* tile : grid_->tiles()) {
    tabs.push_back(tile->tab());
  }
  return tabs;
}

views::View* TabOverviewView::GetTileForTesting(tabs::TabHandle tab) {
  return FindTile(tab);
}

views::View* TabOverviewView::GetCloseButtonForTesting(tabs::TabHandle tab) {
  Tile* tile = FindTile(tab);
  return tile ? tile->close_button() : nullptr;
}

int TabOverviewView::GetScrollableHeightForTesting() const {
  return grid_->height();
}

void TabOverviewView::BeginTransition(const gfx::ImageSkia& page_snapshot) {
  Tile* active = nullptr;
  for (Tile* tile : grid_->tiles()) {
    if (tile->active()) {
      active = tile;
    }
  }
  if (!active || !GetWidget()) {
    return;
  }
  // The transition needs to know where the active tile ends up.
  GetWidget()->LayoutRootViewIfNecessary();
  active->ScrollViewToVisible();
  active->SetSnapshot(page_snapshot);
  transition_tile_rect_ = gfx::RectF(
      active->thumbnail_view()->bounds() + active->bounds().OffsetFromOrigin() -
      scroll_view_->GetVisibleRect().OffsetFromOrigin());

  // The page snapshot is drawn once at full size and then only transformed
  // and clipped, so following the pinch costs no repaints.
  page_clip_ = AddChildView(std::make_unique<views::View>());
  page_clip_->SetPaintToLayer(ui::LAYER_NOT_DRAWN);
  page_clip_->layer()->SetMasksToBounds(true);
  page_clip_->SetCanProcessEventsWithinSubtree(false);
  auto page = std::make_unique<views::ImageView>(
      ui::ImageModel::FromImageSkia(page_snapshot));
  page->SetImageSize(size());
  page->SetBoundsRect(GetLocalBounds());
  page->SetPaintToLayer();
  page_ = page_clip_->AddChildView(std::move(page));
  SetTransitionProgress(0.0f);
}

void TabOverviewView::SetTransitionProgress(float progress) {
  if (!page_clip_ || transition_tile_rect_.IsEmpty() || size().IsEmpty()) {
    return;
  }
  transition_progress_ = progress;
  // The rectangle the page occupies: the whole view at 0, its tile at 1.
  const gfx::RectF page_rect = gfx::Tween::RectFValueBetween(
      progress, gfx::RectF(GetLocalBounds()), transition_tile_rect_);

  // The grid is scaled about its tile so that the tile stays under the page.
  const float grid_scale = page_rect.width() / transition_tile_rect_.width();
  gfx::Transform grid_transform;
  grid_transform.Translate(
      page_rect.x() - transition_tile_rect_.x() * grid_scale,
      page_rect.y() - transition_tile_rect_.y() * grid_scale);
  grid_transform.Scale(grid_scale, grid_scale);
  scroll_view_->SetTransform(grid_transform);

  page_clip_->SetBoundsRect(gfx::ToEnclosingRect(page_rect));
  page_clip_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(progress * kThumbnailRadius));
  const float page_scale = page_rect.width() / width();
  gfx::Transform page_transform;
  page_transform.Scale(page_scale, page_scale);
  page_->SetTransform(page_transform);
}

void TabOverviewView::AnimateTransitionTo(float target,
                                          base::OnceClosure done) {
  transition_start_ = transition_progress_;
  transition_target_ = target;
  transition_done_ = std::move(done);
  transition_animation_ = std::make_unique<gfx::SlideAnimation>(this);
  transition_animation_->SetSlideDuration(base::Milliseconds(220));
  transition_animation_->SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
  transition_animation_->Show();
}

void TabOverviewView::EndTransition() {
  transition_done_.Reset();
  if (views::View* clip = page_clip_) {
    page_ = nullptr;
    page_clip_ = nullptr;
    RemoveChildViewT(clip);
  }
  scroll_view_->SetTransform(gfx::Transform());
}

void TabOverviewView::Layout(PassKey) {
  scroll_view_->SetBoundsRect(GetLocalBounds());
}

void TabOverviewView::AnimationProgressed(const gfx::Animation* animation) {
  SetTransitionProgress(gfx::Tween::FloatValueBetween(
      animation->GetCurrentValue(), transition_start_, transition_target_));
}

void TabOverviewView::AnimationEnded(const gfx::Animation* animation) {
  SetTransitionProgress(transition_target_);
  if (transition_done_) {
    std::move(transition_done_).Run();
  }
}

bool TabOverviewView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  on_dismiss_requested_.Run();
  return true;
}

bool TabOverviewView::OnMousePressed(const ui::MouseEvent& event) {
  on_dismiss_requested_.Run();
  return true;
}

TabOverviewView::Tile* TabOverviewView::FindTile(tabs::TabHandle tab) const {
  for (Tile* tile : grid_->tiles()) {
    if (tile->tab() == tab) {
      return tile;
    }
  }
  return nullptr;
}

BEGIN_METADATA(TabOverviewView)
END_METADATA
