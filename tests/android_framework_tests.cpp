#include "astra/android_framework.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void Expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using astra::android::Activity;
    using astra::android::Color;
    using astra::android::DrawKind;
    using astra::android::LifecycleState;
    using astra::android::MotionAction;
    using astra::android::MotionEvent;
    using astra::android::View;
    using astra::android::ViewKind;

    int failures = 0;
    Activity activity;
    bool invalid_transition_rejected = false;
    try {
        activity.Resume();
    } catch (const std::logic_error&) {
        invalid_transition_rejected = true;
    }
    Expect(invalid_transition_rejected,
           "invalid lifecycle transition was accepted", failures);

    activity.Attach();
    activity.Create();

    auto root = std::make_unique<View>(ViewKind::linear_layout, L"root");
    root->SetPaddingDp(8).SetBackground(Color{248, 249, 252});
    auto title = std::make_unique<View>(ViewKind::text, L"title");
    title->SetText(L"Framework test").SetPreferredHeightDp(40);
    int click_count = 0;
    auto button = std::make_unique<View>(ViewKind::button, L"action");
    button->SetText(L"Tap")
        .SetPreferredHeightDp(52)
        .SetBackground(Color{34, 103, 210})
        .SetTextColor(Color{255, 255, 255})
        .SetOnClick([&click_count]() { ++click_count; });
    View* button_view = button.get();
    auto body = std::make_unique<View>(ViewKind::text, L"body");
    body->SetText(L"Weighted body").SetWeight(1);
    root->AddChild(std::move(title));
    root->AddChild(std::move(body));
    root->AddChild(std::move(button));
    activity.SetContentView(std::move(root));
    activity.Resize(400, 800, 2.0F);
    activity.Start();
    activity.Resume();

    Expect(activity.State() == LifecycleState::resumed,
           "Activity did not reach resumed state", failures);
    const auto* content = activity.ContentView();
    Expect(content && content->Bounds().Width() == 400 &&
               content->Bounds().Height() == 800 && content->ChildCount() == 3,
           "content View was not laid out to the surface", failures);
    Expect(button_view->Bounds().Height() == 104,
           "density-aware button height is incorrect", failures);
    Expect(button_view->Bounds().bottom <= 784,
           "child View escaped padded content bounds", failures);

    const auto display_list = activity.BuildDisplayList();
    std::size_t text_commands = 0;
    for (const auto& command : display_list) {
        if (command.kind == DrawKind::text) {
            ++text_commands;
        }
    }
    Expect(display_list.size() == 7 && text_commands == 3,
           "View tree did not produce the expected display list", failures);

    const int button_x = button_view->Bounds().left + 2;
    const int button_y = button_view->Bounds().top + 2;
    Expect(activity.DispatchTouchEvent(
               {MotionAction::down, button_x, button_y}) &&
               button_view->Pressed(),
           "pointer down did not capture and press the button", failures);
    Expect(activity.DispatchTouchEvent(
               {MotionAction::up, button_x, button_y}) &&
               !button_view->Pressed() && click_count == 1,
           "pointer up did not click and release the button", failures);

    activity.DispatchTouchEvent({MotionAction::down, button_x, button_y});
    activity.DispatchTouchEvent({MotionAction::up, 0, 0});
    Expect(click_count == 1,
           "pointer release outside the button incorrectly clicked", failures);

    activity.Pause();
    Expect(!activity.DispatchTouchEvent(
               {MotionAction::down, button_x, button_y}),
           "paused Activity accepted input", failures);
    activity.Stop();
    activity.Start();
    activity.Resume();
    activity.Pause();
    activity.Stop();
    activity.Destroy();
    Expect(activity.State() == LifecycleState::destroyed &&
               activity.ContentView() == nullptr,
           "Activity resources were not released on destroy", failures);

    if (failures == 0) {
        std::cout << "All Astra Android framework tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
