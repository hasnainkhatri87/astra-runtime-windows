#include "astra/android_framework.hpp"
#include "astra/package_store.hpp"

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kWindowClass[] = L"AstraRuntimeHostWindow";

struct RuntimeState {
    std::filesystem::path package;
    std::filesystem::path data;
    std::wstring name{L"Application"};
    std::wstring launcher;
    std::wstring services;
    std::wstring service_state;
    HWND window{};
    std::unique_ptr<astra::android::Activity> activity;
    astra::android::View* status_view{};
};

RuntimeState ParseArguments() {
    RuntimeState state;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        return state;
    }
    for (int index = 1; index + 1 < count; index += 2) {
        const std::wstring key = arguments[index];
        const std::wstring value = arguments[index + 1];
        if (key == L"--package") {
            state.package = value;
        } else if (key == L"--data") {
            state.data = value;
        } else if (key == L"--name") {
            state.name = value;
        } else if (key == L"--launcher") {
            state.launcher = value;
        } else if (key == L"--services") {
            state.services = value;
        } else if (key == L"--service-state") {
            state.service_state = value;
        }
    }
    LocalFree(arguments);
    return state;
}

std::unique_ptr<astra::android::View> TextView(
    const wchar_t* id, std::wstring text, int height_dp, int text_size_dp,
    astra::android::Color background) {
    auto view = std::make_unique<astra::android::View>(
        astra::android::ViewKind::text, id);
    view->SetText(std::move(text))
        .SetPreferredHeightDp(height_dp)
        .SetTextSizeDp(text_size_dp)
        .SetBackground(background);
    return view;
}

void BuildFrameworkSurface(RuntimeState& state) {
    using astra::android::Activity;
    using astra::android::Color;
    using astra::android::View;
    using astra::android::ViewKind;

    state.activity = std::make_unique<Activity>();
    state.activity->Attach();
    state.activity->Create();

    auto root = std::make_unique<View>(ViewKind::linear_layout, L"root");
    root->SetPaddingDp(16).SetBackground(Color{245, 247, 251});
    root->AddChild(TextView(
        L"title", L"Astra Android framework diagnostic", 72, 24,
        Color{229, 236, 249}));

    std::wstring package_text =
        L"Installed application: " + state.name + L"\r\n" +
        L"Declared launcher: " + state.launcher + L"\r\n" +
        L"Google APIs requested: " + state.services + L"\r\n" +
        L"Service bridge: " + state.service_state;
    auto details = TextView(L"package", std::move(package_text), 0, 15,
                            Color{255, 255, 255});
    details->SetWeight(1);
    root->AddChild(std::move(details));

    root->AddChild(TextView(
        L"boundary",
        L"This surface is drawn by the new lifecycle/View/graphics layer. "
        L"Foodpanda's Java Activity is not rendered yet: object-capable DEX "
        L"invocation, Android services, JNI/Bionic, and Google API execution "
        L"remain required.",
        168, 15, Color{255, 244, 220}));

    auto status = TextView(
        L"input-status", L"Input backend ready. Press the button to test it.",
        64, 15, Color{236, 248, 240});
    state.status_view = status.get();
    root->AddChild(std::move(status));

    auto button = std::make_unique<View>(ViewKind::button, L"input-test");
    button->SetText(L"Run framework input test")
        .SetPreferredHeightDp(56)
        .SetTextSizeDp(16)
        .SetTextColor(Color{255, 255, 255})
        .SetBackground(Color{30, 96, 190})
        .SetOnClick([&state]() {
            if (state.status_view) {
                state.status_view->SetText(
                    L"Pointer event delivered through the View tree.");
            }
            if (state.window) {
                InvalidateRect(state.window, nullptr, FALSE);
            }
        });
    root->AddChild(std::move(button));
    state.activity->SetContentView(std::move(root));
}

void DrawFrameworkSurface(HDC dc, const RuntimeState& state) {
    if (!state.activity) {
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    for (const auto& command : state.activity->BuildDisplayList()) {
        const COLORREF color = RGB(command.color.red, command.color.green,
                                   command.color.blue);
        if (command.kind == astra::android::DrawKind::fill_rectangle) {
            const RECT rectangle{command.bounds.left, command.bounds.top,
                                 command.bounds.right, command.bounds.bottom};
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &rectangle, brush);
            DeleteObject(brush);
            continue;
        }
        SetTextColor(dc, color);
        HFONT font = CreateFontW(
            -std::max(1, command.text_size_px), 0, 0, 0, FW_NORMAL, FALSE,
            FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
            L"Segoe UI");
        HGDIOBJ previous = SelectObject(dc, font);
        RECT rectangle{command.bounds.left, command.bounds.top,
                       command.bounds.right, command.bounds.bottom};
        DrawTextW(dc, command.text.c_str(), -1, &rectangle,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous);
        DeleteObject(font);
    }
}

void FinishActivity(RuntimeState* state) {
    if (!state || !state->activity) {
        return;
    }
    using astra::android::LifecycleState;
    if (state->activity->State() == LifecycleState::resumed) {
        state->activity->Pause();
    }
    if (state->activity->State() == LifecycleState::paused) {
        state->activity->Stop();
    }
    if (state->activity->State() == LifecycleState::stopped ||
        state->activity->State() == LifecycleState::created) {
        state->activity->Destroy();
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RuntimeState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            state = reinterpret_cast<RuntimeState*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(state));
            if (state) {
                state->window = window;
            }
            return TRUE;
        }
        case WM_SIZE:
            if (state && state->activity) {
                const UINT dpi = GetDpiForWindow(window);
                const float density =
                    static_cast<float>(dpi == 0 ? 96 : dpi) / 96.0F;
                state->activity->Resize(LOWORD(lparam), HIWORD(lparam), density);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (state) {
                DrawFrameworkSurface(dc, *state);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (state && state->activity && state->activity->DispatchTouchEvent(
                    {astra::android::MotionAction::down,
                     GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
                SetCapture(window);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (state && state->activity) {
                state->activity->DispatchTouchEvent(
                    {astra::android::MotionAction::up,
                     GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (state && state->activity) {
                state->activity->DispatchTouchEvent(
                    {astra::android::MotionAction::cancel, 0, 0});
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_DESTROY:
            FinishActivity(state);
            if (state) {
                state->window = nullptr;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    RuntimeState state = ParseArguments();
    if (state.package.empty() || !std::filesystem::is_regular_file(state.package) ||
        !astra::IsSupportedPackageExtension(state.package) ||
        !astra::HasZipContainerSignature(state.package)) {
        MessageBoxW(nullptr, L"The installed package is missing or invalid.",
                    L"Astra runtime", MB_OK | MB_ICONERROR);
        return 2;
    }
    std::filesystem::create_directories(state.data);
    BuildFrameworkSurface(state);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) {
        return 3;
    }

    const std::wstring title = state.name + L" - Astra framework diagnostic";
    HWND window = CreateWindowExW(
        0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 800, nullptr, nullptr, instance,
        &state);
    if (!window) {
        return 4;
    }

    state.activity->Start();
    state.activity->Resume();
    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
