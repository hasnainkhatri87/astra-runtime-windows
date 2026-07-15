#include "astra/android_framework.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace astra::android {
namespace {

int Dp(int value, float density) {
    return std::max(0, static_cast<int>(std::lround(value * density)));
}

const char* StateName(LifecycleState state) {
    switch (state) {
        case LifecycleState::detached: return "detached";
        case LifecycleState::attached: return "attached";
        case LifecycleState::created: return "created";
        case LifecycleState::started: return "started";
        case LifecycleState::resumed: return "resumed";
        case LifecycleState::paused: return "paused";
        case LifecycleState::stopped: return "stopped";
        case LifecycleState::destroyed: return "destroyed";
    }
    return "unknown";
}

}  // namespace

int Rect::Width() const noexcept {
    return std::max(0, right - left);
}

int Rect::Height() const noexcept {
    return std::max(0, bottom - top);
}

bool Rect::Contains(int x, int y) const noexcept {
    return x >= left && x < right && y >= top && y < bottom;
}

View::View(ViewKind kind, std::wstring id)
    : kind_(kind), id_(std::move(id)) {}

View& View::SetText(std::wstring text) {
    text_ = std::move(text);
    return *this;
}

View& View::SetTextColor(Color color) noexcept {
    text_color_ = color;
    return *this;
}

View& View::SetBackground(Color color) noexcept {
    background_ = color;
    return *this;
}

View& View::SetTextSizeDp(int size_dp) {
    if (size_dp <= 0 || size_dp > 256) {
        throw std::invalid_argument("View text size is outside the supported range");
    }
    text_size_dp_ = size_dp;
    return *this;
}

View& View::SetPaddingDp(int padding_dp) {
    if (padding_dp < 0 || padding_dp > 1024) {
        throw std::invalid_argument("View padding is outside the supported range");
    }
    padding_dp_ = padding_dp;
    return *this;
}

View& View::SetPreferredHeightDp(int height_dp) {
    if (height_dp < 0 || height_dp > 16384) {
        throw std::invalid_argument("View height is outside the supported range");
    }
    preferred_height_dp_ = height_dp;
    return *this;
}

View& View::SetWeight(int weight) {
    if (weight < 0 || weight > 1000) {
        throw std::invalid_argument("View weight is outside the supported range");
    }
    weight_ = weight;
    return *this;
}

View& View::SetOrientation(Orientation orientation) noexcept {
    orientation_ = orientation;
    return *this;
}

View& View::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
    return *this;
}

View& View::AddChild(std::unique_ptr<View> child) {
    if (kind_ != ViewKind::linear_layout) {
        throw std::logic_error("Only a layout View can contain children");
    }
    if (!child) {
        throw std::invalid_argument("Cannot add a null child View");
    }
    if (children_.size() >= 4096) {
        throw std::length_error("Layout View has too many children");
    }
    children_.push_back(std::move(child));
    return *this;
}

const std::wstring& View::Id() const noexcept {
    return id_;
}

const std::wstring& View::Text() const noexcept {
    return text_;
}

const Rect& View::Bounds() const noexcept {
    return bounds_;
}

bool View::Pressed() const noexcept {
    return pressed_;
}

std::size_t View::ChildCount() const noexcept {
    return children_.size();
}

const View& View::ChildAt(std::size_t index) const {
    if (index >= children_.size()) {
        throw std::out_of_range("View child index is invalid");
    }
    return *children_[index];
}

void View::Layout(Rect bounds, float density) {
    bounds_ = bounds;
    if (kind_ != ViewKind::linear_layout || children_.empty()) {
        return;
    }

    const int padding = Dp(padding_dp_, density);
    const Rect inner{
        std::min(bounds.right, bounds.left + padding),
        std::min(bounds.bottom, bounds.top + padding),
        std::max(bounds.left, bounds.right - padding),
        std::max(bounds.top, bounds.bottom - padding),
    };
    const int available = orientation_ == Orientation::vertical ?
        inner.Height() : inner.Width();
    int fixed = 0;
    int total_weight = 0;
    for (const auto& child : children_) {
        if (child->weight_ > 0) {
            total_weight += child->weight_;
        } else {
            const int fallback = child->kind_ == ViewKind::button ? 52 : 44;
            fixed += Dp(child->preferred_height_dp_ > 0 ?
                        child->preferred_height_dp_ : fallback, density);
        }
    }
    const int flexible = std::max(0, available - fixed);
    int cursor = orientation_ == Orientation::vertical ? inner.top : inner.left;
    int remaining_weight = total_weight;
    int remaining_flexible = flexible;
    for (const auto& child : children_) {
        int extent = 0;
        if (child->weight_ > 0 && remaining_weight > 0) {
            extent = remaining_flexible * child->weight_ / remaining_weight;
            remaining_flexible -= extent;
            remaining_weight -= child->weight_;
        } else {
            const int fallback = child->kind_ == ViewKind::button ? 52 : 44;
            extent = Dp(child->preferred_height_dp_ > 0 ?
                        child->preferred_height_dp_ : fallback, density);
        }
        Rect child_bounds;
        if (orientation_ == Orientation::vertical) {
            child_bounds = {inner.left, cursor, inner.right,
                            std::min(inner.bottom, cursor + extent)};
        } else {
            child_bounds = {cursor, inner.top,
                            std::min(inner.right, cursor + extent), inner.bottom};
        }
        child->Layout(child_bounds, density);
        cursor += extent;
    }
}

void View::BuildDisplayList(float density,
                            std::vector<DrawCommand>& commands) const {
    Color background = background_;
    if (pressed_ && kind_ == ViewKind::button) {
        background.red = static_cast<std::uint8_t>(background.red * 4U / 5U);
        background.green = static_cast<std::uint8_t>(background.green * 4U / 5U);
        background.blue = static_cast<std::uint8_t>(background.blue * 4U / 5U);
    }
    commands.push_back({DrawKind::fill_rectangle, bounds_, background, {}, 0});
    if (!text_.empty()) {
        const int padding = Dp(padding_dp_, density);
        Rect text_bounds{
            std::min(bounds_.right, bounds_.left + padding),
            std::min(bounds_.bottom, bounds_.top + padding),
            std::max(bounds_.left, bounds_.right - padding),
            std::max(bounds_.top, bounds_.bottom - padding),
        };
        commands.push_back({DrawKind::text, text_bounds, text_color_, text_,
                            Dp(text_size_dp_, density)});
    }
    for (const auto& child : children_) {
        child->BuildDisplayList(density, commands);
    }
}

View* View::HitTest(int x, int y) noexcept {
    if (!bounds_.Contains(x, y)) {
        return nullptr;
    }
    for (auto iterator = children_.rbegin(); iterator != children_.rend();
         ++iterator) {
        if (View* hit = (*iterator)->HitTest(x, y)) {
            return hit;
        }
    }
    return on_click_ ? this : nullptr;
}

void View::SetPressed(bool pressed) noexcept {
    pressed_ = pressed;
}

void View::Click() {
    if (on_click_) {
        on_click_();
    }
}

void Activity::RequireState(LifecycleState expected,
                            const char* operation) const {
    if (state_ != expected) {
        throw std::logic_error(std::string(operation) + " requires state " +
                               StateName(expected) + ", current state is " +
                               StateName(state_));
    }
}

void Activity::Attach() {
    RequireState(LifecycleState::detached, "attach");
    state_ = LifecycleState::attached;
}

void Activity::Create() {
    RequireState(LifecycleState::attached, "create");
    state_ = LifecycleState::created;
}

void Activity::Start() {
    if (state_ != LifecycleState::created && state_ != LifecycleState::stopped) {
        throw std::logic_error("start requires created or stopped state");
    }
    state_ = LifecycleState::started;
}

void Activity::Resume() {
    if (state_ != LifecycleState::started && state_ != LifecycleState::paused) {
        throw std::logic_error("resume requires started or paused state");
    }
    state_ = LifecycleState::resumed;
}

void Activity::Pause() {
    RequireState(LifecycleState::resumed, "pause");
    state_ = LifecycleState::paused;
    if (touch_target_) {
        touch_target_->SetPressed(false);
        touch_target_ = nullptr;
    }
}

void Activity::Stop() {
    RequireState(LifecycleState::paused, "stop");
    state_ = LifecycleState::stopped;
}

void Activity::Destroy() {
    if (state_ != LifecycleState::created &&
        state_ != LifecycleState::stopped) {
        throw std::logic_error("destroy requires created or stopped state");
    }
    state_ = LifecycleState::destroyed;
    touch_target_ = nullptr;
    content_.reset();
}

void Activity::SetContentView(std::unique_ptr<View> root) {
    if (state_ == LifecycleState::detached ||
        state_ == LifecycleState::destroyed) {
        throw std::logic_error("content View requires an attached Activity");
    }
    if (!root) {
        throw std::invalid_argument("Activity content View cannot be null");
    }
    touch_target_ = nullptr;
    content_ = std::move(root);
    if (width_px_ > 0 && height_px_ > 0) {
        content_->Layout({0, 0, width_px_, height_px_}, density_);
    }
}

void Activity::Resize(int width_px, int height_px, float density) {
    if (width_px < 0 || height_px < 0 || !std::isfinite(density) ||
        density <= 0.0F || density > 16.0F) {
        throw std::invalid_argument("Activity surface metrics are invalid");
    }
    width_px_ = width_px;
    height_px_ = height_px;
    density_ = density;
    if (content_) {
        content_->Layout({0, 0, width_px_, height_px_}, density_);
    }
}

std::vector<DrawCommand> Activity::BuildDisplayList() const {
    std::vector<DrawCommand> commands;
    if (content_ && width_px_ > 0 && height_px_ > 0) {
        content_->BuildDisplayList(density_, commands);
    }
    return commands;
}

bool Activity::DispatchTouchEvent(const MotionEvent& event) {
    if (state_ != LifecycleState::resumed || !content_) {
        return false;
    }
    if (event.action == MotionAction::down) {
        if (touch_target_) {
            touch_target_->SetPressed(false);
        }
        touch_target_ = content_->HitTest(event.x, event.y);
        if (touch_target_) {
            touch_target_->SetPressed(true);
            return true;
        }
        return false;
    }
    if (!touch_target_) {
        return false;
    }
    View* target = touch_target_;
    touch_target_ = nullptr;
    target->SetPressed(false);
    if (event.action == MotionAction::up &&
        target->Bounds().Contains(event.x, event.y)) {
        target->Click();
    }
    return true;
}

LifecycleState Activity::State() const noexcept {
    return state_;
}

const View* Activity::ContentView() const noexcept {
    return content_.get();
}

}  // namespace astra::android
