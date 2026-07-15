#include "astra/package_store.hpp"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"AstraLauncherWindow";
constexpr int kInstallButton = 1001;
constexpr int kRunButton = 1002;
constexpr int kRefreshButton = 1003;
constexpr int kAppList = 1004;
constexpr int kStatusText = 1005;

struct LauncherState {
    HWND list{};
    HWND status{};
    HWND install{};
    HWND run{};
    HWND refresh{};
    std::vector<astra::InstalledApp> apps;
};

LauncherState* State(HWND window) {
    return reinterpret_cast<LauncherState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
}

void ApplyDefaultFont(HWND control) {
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

void SetStatus(LauncherState& state, const std::wstring& text) {
    SetWindowTextW(state.status, text.c_str());
}

void RefreshApps(LauncherState& state) {
    state.apps = astra::ListInstalledApps();
    SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
    for (const auto& app : state.apps) {
        SendMessageW(state.list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(app.display_name.c_str()));
    }
    if (!state.apps.empty()) {
        SendMessageW(state.list, LB_SETCURSEL, 0, 0);
        EnableWindow(state.run, TRUE);
        SetStatus(state, std::to_wstring(state.apps.size()) +
                             L" locally installed package(s). Select one and click Run.");
    } else {
        EnableWindow(state.run, FALSE);
        SetStatus(state, L"No packages installed. Click Install package to begin.");
    }
}

std::filesystem::path SelectPackage(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"Android application packages", L"*.apk;*.apkm;*.apks;*.xapk"},
        {L"APK", L"*.apk"},
        {L"APK bundle containers", L"*.apkm;*.apks;*.xapk"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetDefaultExtension(L"apk");
    dialog->SetTitle(L"Install an application package into Astra");

    std::filesystem::path selected;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR raw_path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path))) {
                selected = raw_path;
                CoTaskMemFree(raw_path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

void InstallSelected(HWND window, LauncherState& state) {
    const auto source = SelectPackage(window);
    if (source.empty()) {
        return;
    }

    SetStatus(state, L"Validating and installing package locally...");
    const auto result = astra::InstallPackage(source);
    MessageBoxW(window, result.message.c_str(),
                result.ok ? L"Astra installation" : L"Astra installation failed",
                result.ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
    RefreshApps(state);
}

void RunSelected(HWND window, LauncherState& state) {
    const LRESULT selection = SendMessageW(state.list, LB_GETCURSEL, 0, 0);
    if (selection == LB_ERR || static_cast<std::size_t>(selection) >= state.apps.size()) {
        MessageBoxW(window, L"Select an installed package first.", L"Astra",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    const auto& app = state.apps[static_cast<std::size_t>(selection)];
    const auto runtime = ExecutableDirectory() / L"astra-runtime.exe";
    if (!std::filesystem::exists(runtime)) {
        MessageBoxW(window, L"astra-runtime.exe is missing from the portable folder.",
                    L"Astra runtime error", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring command = QuoteArgument(runtime.wstring()) +
                           L" --package " + QuoteArgument(app.package_path.wstring()) +
                           L" --data " + QuoteArgument(app.data_path.wstring()) +
                           L" --name " + QuoteArgument(app.display_name);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(runtime.c_str(), mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                        runtime.parent_path().c_str(), &startup, &process)) {
        MessageBoxW(window, L"Windows could not start the Astra runtime host.",
                    L"Astra runtime error", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    SetStatus(state, L"Runtime host started for " + app.display_name + L".");
}

void Layout(HWND window, LauncherState& state) {
    RECT client{};
    GetClientRect(window, &client);
    const int padding = 16;
    const int button_height = 34;
    const int button_width = 140;
    const int status_height = 44;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    MoveWindow(state.install, padding, padding, button_width, button_height, TRUE);
    MoveWindow(state.run, padding + button_width + 10, padding,
               button_width, button_height, TRUE);
    MoveWindow(state.refresh, padding + (button_width + 10) * 2, padding,
               100, button_height, TRUE);
    MoveWindow(state.list, padding, padding + button_height + 12,
               width - padding * 2,
               height - button_height - status_height - padding * 3, TRUE);
    MoveWindow(state.status, padding, height - status_height - padding,
               width - padding * 2, status_height, TRUE);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
                                 WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            auto* state = new LauncherState();
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(state));
            state->install = CreateWindowW(L"BUTTON", L"Install package",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(kInstallButton), nullptr, nullptr);
            state->run = CreateWindowW(L"BUTTON", L"Run",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(kRunButton), nullptr, nullptr);
            state->refresh = CreateWindowW(L"BUTTON", L"Refresh",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(kRefreshButton), nullptr, nullptr);
            state->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(kAppList), nullptr, nullptr);
            state->status = CreateWindowW(L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(kStatusText), nullptr, nullptr);
            ApplyDefaultFont(state->install);
            ApplyDefaultFont(state->run);
            ApplyDefaultFont(state->refresh);
            ApplyDefaultFont(state->list);
            ApplyDefaultFont(state->status);
            RefreshApps(*state);
            return 0;
        }
        case WM_SIZE:
            if (auto* state = State(window)) {
                Layout(window, *state);
            }
            return 0;
        case WM_COMMAND: {
            auto* state = State(window);
            if (!state) {
                return 0;
            }
            switch (LOWORD(wparam)) {
                case kInstallButton:
                    InstallSelected(window, *state);
                    break;
                case kRunButton:
                    RunSelected(window, *state);
                    break;
                case kRefreshButton:
                    RefreshApps(*state);
                    break;
                case kAppList:
                    if (HIWORD(wparam) == LBN_DBLCLK) {
                        RunSelected(window, *state);
                    }
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            delete State(window);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) {
        return 1;
    }

    HWND window = CreateWindowExW(0, kWindowClass,
        L"Astra — compatibility runtime for Android application packages",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 2;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}
