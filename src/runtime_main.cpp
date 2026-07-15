#include "astra/package_store.hpp"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

constexpr wchar_t kWindowClass[] = L"AstraRuntimeHostWindow";

struct RuntimeState {
    std::filesystem::path package;
    std::filesystem::path data;
    std::wstring name{L"Application"};
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
        }
    }
    LocalFree(arguments);
    return state;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_NCCREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(32, 36, 44));

            const auto* state = reinterpret_cast<RuntimeState*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            std::wstring text =
                L"Astra native application surface\r\n\r\n";
            if (state) {
                text += L"Installed package: " + state->name + L"\r\n";
                text += L"Package: " + state->package.wstring() + L"\r\n";
                text += L"Private data: " + state->data.wstring() + L"\r\n\r\n";
            }
            text +=
                L"The Windows host, install flow, and process boundary are active.\r\n"
                L"ART/DEX execution and Android framework services are not connected in this bootstrap build.\r\n"
                L"This message prevents the prototype from pretending that the Android activity is already running.";
            RECT text_rect{24, 24, client.right - 24, client.bottom - 24};
            DrawTextW(dc, text.c_str(), -1, &text_rect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
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

    const std::wstring title = state.name + L" — Astra";
    HWND window = CreateWindowExW(0, kWindowClass, title.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 760,
        nullptr, nullptr, instance, &state);
    if (!window) {
        return 4;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
