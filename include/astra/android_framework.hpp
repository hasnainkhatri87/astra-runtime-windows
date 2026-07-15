#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace astra::android {

struct Rect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    [[nodiscard]] bool Contains(int x, int y) const noexcept;
};

struct Color {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

enum class ViewKind {
    linear_layout,
    text,
    button,
};

enum class Orientation {
    vertical,
    horizontal,
};

enum class DrawKind {
    fill_rectangle,
    text,
};

struct DrawCommand {
    DrawKind kind{DrawKind::fill_rectangle};
    Rect bounds;
    Color color;
    std::wstring text;
    int text_size_px{};
};

enum class MotionAction {
    down,
    up,
    cancel,
};

struct MotionEvent {
    MotionAction action{MotionAction::cancel};
    int x{};
    int y{};
};

class View {
public:
    explicit View(ViewKind kind, std::wstring id = {});

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) = delete;
    View& operator=(View&&) = delete;

    View& SetText(std::wstring text);
    View& SetTextColor(Color color) noexcept;
    View& SetBackground(Color color) noexcept;
    View& SetTextSizeDp(int size_dp);
    View& SetPaddingDp(int padding_dp);
    View& SetPreferredHeightDp(int height_dp);
    View& SetWeight(int weight);
    View& SetOrientation(Orientation orientation) noexcept;
    View& SetOnClick(std::function<void()> callback);
    View& AddChild(std::unique_ptr<View> child);

    [[nodiscard]] const std::wstring& Id() const noexcept;
    [[nodiscard]] const std::wstring& Text() const noexcept;
    [[nodiscard]] const Rect& Bounds() const noexcept;
    [[nodiscard]] bool Pressed() const noexcept;
    [[nodiscard]] std::size_t ChildCount() const noexcept;
    [[nodiscard]] const View& ChildAt(std::size_t index) const;

private:
    friend class Activity;

    void Layout(Rect bounds, float density);
    void BuildDisplayList(float density,
                          std::vector<DrawCommand>& commands) const;
    View* HitTest(int x, int y) noexcept;
    void SetPressed(bool pressed) noexcept;
    void Click();

    ViewKind kind_;
    std::wstring id_;
    std::wstring text_;
    Color text_color_{32, 36, 44};
    Color background_{255, 255, 255};
    int text_size_dp_{16};
    int padding_dp_{12};
    int preferred_height_dp_{0};
    int weight_{0};
    Orientation orientation_{Orientation::vertical};
    Rect bounds_;
    bool pressed_{false};
    std::function<void()> on_click_;
    std::vector<std::unique_ptr<View>> children_;
};

enum class LifecycleState {
    detached,
    attached,
    created,
    started,
    resumed,
    paused,
    stopped,
    destroyed,
};

class Activity {
public:
    Activity() = default;

    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;

    void Attach();
    void Create();
    void Start();
    void Resume();
    void Pause();
    void Stop();
    void Destroy();

    void SetContentView(std::unique_ptr<View> root);
    void Resize(int width_px, int height_px, float density);
    [[nodiscard]] std::vector<DrawCommand> BuildDisplayList() const;
    [[nodiscard]] bool DispatchTouchEvent(const MotionEvent& event);

    [[nodiscard]] LifecycleState State() const noexcept;
    [[nodiscard]] const View* ContentView() const noexcept;

private:
    void RequireState(LifecycleState expected, const char* operation) const;

    LifecycleState state_{LifecycleState::detached};
    std::unique_ptr<View> content_;
    View* touch_target_{};
    int width_px_{};
    int height_px_{};
    float density_{1.0F};
};

}  // namespace astra::android
