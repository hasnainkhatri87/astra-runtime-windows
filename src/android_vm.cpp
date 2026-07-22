#include "astra/android_vm.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra {
namespace {

constexpr wchar_t kAvdName[] = L"Astra_Play_API_36";
constexpr DWORD kShortCommandTimeoutMs = 15U * 1000U;
constexpr DWORD kBootTimeoutMs = 180U * 1000U;
constexpr std::size_t kMaximumSplitCount = 128;
constexpr std::size_t kMaximumCapturedOutputBytes = 1024U * 1024U;

std::wstring GetEnvironment(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        name, value.data(), static_cast<DWORD>(value.size()));
    if (written == 0 || written >= value.size()) {
        return {};
    }
    value.resize(written);
    return value;
}

std::filesystem::path LocalAppData() {
    const std::wstring value = GetEnvironment(L"LOCALAPPDATA");
    if (value.empty()) {
        throw std::runtime_error("LOCALAPPDATA is unavailable");
    }
    return value;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(towlower(character));
                   });
    return value;
}

std::wstring QuoteArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(slashes * 2U + 1U, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(slashes, L'\\');
            quoted.push_back(character);
        }
        slashes = 0;
    }
    quoted.append(slashes * 2U, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring CommandLine(const std::filesystem::path& executable,
                         const std::vector<std::wstring>& arguments) {
    std::wstring command = QuoteArgument(executable.wstring());
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += QuoteArgument(argument);
    }
    return command;
}

std::wstring DecodeOutput(const std::vector<char>& bytes) {
    if (bytes.empty()) {
        return {};
    }
    const auto decode = [&bytes](UINT code_page, DWORD flags) {
        return MultiByteToWideChar(code_page, flags, bytes.data(),
                                   static_cast<int>(bytes.size()), nullptr, 0);
    };
    int count = decode(CP_UTF8, MB_ERR_INVALID_CHARS);
    UINT code_page = CP_UTF8;
    if (count == 0) {
        code_page = CP_ACP;
        count = decode(code_page, 0);
    }
    if (count <= 0) {
        return L"(process produced undecodable output)";
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                        bytes.data(), static_cast<int>(bytes.size()),
                        result.data(), count);
    return result;
}

std::wstring ReadOutputFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return L"(unable to read process output)";
    }
    const auto size = input.tellg();
    if (size < 0) {
        return L"(unable to size process output)";
    }
    const auto limited_size = static_cast<std::size_t>(std::min<std::streamoff>(
        size, static_cast<std::streamoff>(kMaximumCapturedOutputBytes)));
    input.seekg(0);
    std::vector<char> bytes(limited_size);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    std::wstring output = DecodeOutput(bytes);
    if (size > static_cast<std::streamoff>(kMaximumCapturedOutputBytes)) {
        output += L"\r\n(process output truncated)";
    }
    return output;
}

std::wstring Win32Message(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length == 0 ? L"Windows error " + std::to_wstring(error)
                                       : std::wstring(buffer, length);
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

struct ProcessResult {
    bool started{false};
    bool timed_out{false};
    DWORD exit_code{std::numeric_limits<DWORD>::max()};
    std::wstring output;
};

ProcessResult RunCapture(const std::filesystem::path& executable,
                         const std::vector<std::wstring>& arguments,
                         DWORD timeout_ms) {
    ProcessResult result;
    const auto logs = AndroidVmRoot() / L"logs";
    try {
        std::filesystem::create_directories(logs);
    } catch (const std::exception&) {
        result.output = L"Unable to create Astra VM log directory.";
        return result;
    }

    wchar_t temporary[MAX_PATH]{};
    if (GetTempFileNameW(logs.c_str(), L"cmd", 0, temporary) == 0) {
        result.output = L"Unable to allocate temporary command output: " +
                        Win32Message(GetLastError());
        return result;
    }
    const std::filesystem::path output_path = temporary;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE output = CreateFileW(output_path.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ, &attributes, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        DeleteFileW(output_path.c_str());
        result.output = L"Unable to create command output: " +
                        Win32Message(GetLastError());
        return result;
    }

    std::wstring command = CommandLine(executable, arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, AndroidVmRoot().c_str(), &startup, &process);
    if (!created) {
        CloseHandle(output);
        result.output = L"Unable to start " + executable.filename().wstring() +
                        L": " + Win32Message(GetLastError());
        DeleteFileW(output_path.c_str());
        return result;
    }
    result.started = true;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
    } else if (wait != WAIT_OBJECT_0) {
        result.output = L"Unable to wait for " + executable.filename().wstring() +
                        L": " + Win32Message(GetLastError());
    }
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(output);
    result.output += ReadOutputFile(output_path);
    DeleteFileW(output_path.c_str());
    return result;
}

bool StartVisible(const std::filesystem::path& executable,
                  const std::vector<std::wstring>& arguments,
                  std::wstring& message) {
    std::wstring command = CommandLine(executable, arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
                        AndroidVmRoot().c_str(), &startup, &process)) {
        message = L"Unable to start Android virtualization: " +
                  Win32Message(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void ApplyVmEnvironment() {
    const auto sdk = AndroidSdkRoot().wstring();
    const auto avd = AndroidAvdHome().wstring();
    SetEnvironmentVariableW(L"ANDROID_SDK_ROOT", sdk.c_str());
    SetEnvironmentVariableW(L"ANDROID_HOME", sdk.c_str());
    SetEnvironmentVariableW(L"ANDROID_AVD_HOME", avd.c_str());
    const auto user_home = (AndroidVmRoot() / L"android-user").wstring();
    SetEnvironmentVariableW(L"ANDROID_EMULATOR_HOME", user_home.c_str());
    SetEnvironmentVariableW(L"ANDROID_USER_HOME", user_home.c_str());
}

std::filesystem::path EmulatorPath() {
    return AndroidSdkRoot() / L"emulator" / L"emulator.exe";
}

std::filesystem::path AdbPath() {
    return AndroidSdkRoot() / L"platform-tools" / L"adb.exe";
}

std::filesystem::path AvdConfigPath() {
    return AndroidAvdHome() / (std::wstring(kAvdName) + L".avd") / L"config.ini";
}

bool IsCommandSuccessful(const ProcessResult& result) {
    return result.started && !result.timed_out && result.exit_code == 0;
}

bool ContainsDevice(const std::wstring& output) {
    const auto lower = Lower(output);
    return lower.find(L"device") != std::wstring::npos &&
           lower.find(L"offline") == std::wstring::npos;
}

std::wstring CompactOutput(std::wstring output) {
    constexpr std::size_t kLimit = 1400;
    while (!output.empty() && (output.back() == L'\r' || output.back() == L'\n' ||
                               output.back() == L' ' || output.back() == L'\t')) {
        output.pop_back();
    }
    if (output.size() > kLimit) {
        output.resize(kLimit);
        output += L"...";
    }
    return output;
}

AndroidVmResult WaitForBoot() {
    const auto adb = AdbPath();
    const auto wait = RunCapture(adb, {L"-e", L"wait-for-device"}, kBootTimeoutMs);
    if (!IsCommandSuccessful(wait)) {
        return {false, L"Android VM did not connect through ADB: " +
                           CompactOutput(wait.output)};
    }
    const auto deadline = GetTickCount64() + kBootTimeoutMs;
    while (GetTickCount64() < deadline) {
        const auto probe = RunCapture(adb,
                                      {L"-e", L"shell", L"getprop", L"sys.boot_completed"},
                                      kShortCommandTimeoutMs);
        if (IsCommandSuccessful(probe) && probe.output.find(L"1") != std::wstring::npos) {
            return {true, L"Android 16 Google Play VM is ready."};
        }
        Sleep(1000);
    }
    return {false, L"Android VM connected but did not finish booting within three minutes."};
}

std::vector<std::filesystem::path> SelectedApks(const InstalledApp& app) {
    const auto split_root = app.package_path.parent_path() / L"splits";
    if (!std::filesystem::is_directory(split_root)) {
        throw std::runtime_error("selected split directory is missing");
    }
    std::vector<std::filesystem::path> apks;
    for (const auto& entry : std::filesystem::directory_iterator(split_root)) {
        if (entry.is_regular_file() && Lower(entry.path().extension().wstring()) == L".apk") {
            apks.push_back(entry.path());
        }
    }
    std::sort(apks.begin(), apks.end());
    if (apks.empty() || apks.size() > kMaximumSplitCount) {
        throw std::runtime_error("selected split count is invalid");
    }
    return apks;
}


}  // namespace

std::filesystem::path AndroidVmRoot() {
    const std::wstring override_root = GetEnvironment(L"ASTRA_VM_ROOT");
    if (!override_root.empty()) {
        return std::filesystem::path(override_root);
    }
    return LocalAppData() / L"AstraRuntime" / L"vm";
}

std::filesystem::path AndroidSdkRoot() {
    return AndroidVmRoot() / L"sdk";
}

std::filesystem::path AndroidAvdHome() {
    return AndroidVmRoot() / L"avd";
}

AndroidVmStatus InspectAndroidVm() {
    AndroidVmStatus status;
    status.runtime_root = AndroidVmRoot();
    status.sdk_root = AndroidSdkRoot();
    status.avd_home = AndroidAvdHome();
    status.firmware_virtualization_ready =
        IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE;
    const auto emulator = EmulatorPath();
    const auto adb = AdbPath();
    status.sdk_ready = std::filesystem::is_regular_file(emulator) &&
                       std::filesystem::is_regular_file(adb);
    status.avd_ready = std::filesystem::is_regular_file(AvdConfigPath());
    if (!status.sdk_ready) {
        status.message = L"Android VM runtime is not installed. Run the license-gated "
                         L"bootstrap-android-vm.ps1 script shipped with Astra.";
        return status;
    }

    ApplyVmEnvironment();
    const auto acceleration = RunCapture(emulator, {L"-accel-check"},
                                         kShortCommandTimeoutMs);
    status.acceleration_ready = IsCommandSuccessful(acceleration);
    // An active Windows hypervisor can mask the firmware probe. A successful
    // emulator acceleration check is authoritative evidence that virtualization
    // is available to this process.
    if (status.acceleration_ready) {
        status.firmware_virtualization_ready = true;
    }
    const auto device = RunCapture(adb, {L"-e", L"get-state"}, 5000);
    status.device_ready = IsCommandSuccessful(device) && ContainsDevice(device.output);
    if (!status.avd_ready) {
        status.message = L"Android SDK is installed, but the Astra Play AVD is missing. "
                         L"Run bootstrap-android-vm.ps1 again.";
    } else if (!status.acceleration_ready) {
        status.message = L"Windows Hypervisor Platform is unavailable: " +
                         CompactOutput(acceleration.output);
    } else if (status.device_ready) {
        status.message = L"Android 16 Google Play VM is running and connected.";
    } else {
        status.message = L"Android 16 Google Play VM is installed and ready to start.";
    }
    return status;
}

AndroidVmResult StartAndroidVm() {
    const auto status = InspectAndroidVm();
    if (!status.sdk_ready || !status.avd_ready || !status.acceleration_ready) {
        return {false, status.message};
    }
    if (status.device_ready) {
        return {true, L"Android 16 Google Play VM is already running."};
    }
    ApplyVmEnvironment();
    std::wstring launch_message;
    if (!StartVisible(EmulatorPath(),
                      {L"-avd", kAvdName, L"-memory", L"1536", L"-cores", L"2",
                       L"-gpu", L"auto", L"-no-boot-anim", L"-camera-back", L"none",
                       L"-camera-front", L"none", L"-noaudio", L"-netdelay", L"none",
                       L"-netspeed", L"full"},
                      launch_message)) {
        return {false, launch_message};
    }
    return WaitForBoot();
}

AndroidVmResult InstallAndRunInAndroidVm(const InstalledApp& app) {
    if (!std::filesystem::is_regular_file(app.package_path)) {
        return {false, L"The installed Android package file is missing."};
    }
    const auto boot = StartAndroidVm();
    if (!boot.ok) {
        return boot;
    }
    try {
        ApplyVmEnvironment();
        std::vector<std::wstring> install = {L"-e", L"install-multiple", L"-r"};
        const auto apks = SelectedApks(app);
        install.reserve(install.size() + apks.size());
        for (const auto& apk : apks) {
            install.push_back(apk.wstring());
        }
        const auto installed = RunCapture(AdbPath(), install, kBootTimeoutMs);
        if (!IsCommandSuccessful(installed)) {
            return {false, L"ADB split installation failed: " +
                               CompactOutput(installed.output)};
        }
        // Use Android's normal launcher-intent path. A manifest can contain
        // internal, non-exported launcher-like activities that shell cannot
        // start directly; the package launcher resolves the exported entry.
        const auto launched = RunCapture(
            AdbPath(), {L"-e", L"shell", L"monkey", L"-p", app.package_name,
                        L"-c", L"android.intent.category.LAUNCHER", L"1"},
            kShortCommandTimeoutMs);
        if (!IsCommandSuccessful(launched)) {
            return {false, L"Android Activity launch failed: " +
                               CompactOutput(launched.output)};
        }
        return {true, L"Installed " + std::to_wstring(apks.size()) +
                          L" verified APK split(s) and sent the package's normal "
                          L"launcher intent. " + CompactOutput(launched.output)};
    } catch (const std::exception&) {
        return {false, L"Astra could not prepare the selected APK splits for ADB installation."};
    }
}

}  // namespace astra
