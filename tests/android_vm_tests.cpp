#include "astra/android_vm.hpp"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const char* message, int& failures) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class EnvironmentOverride {
public:
    EnvironmentOverride(const wchar_t* name, const std::wstring& value)
        : name_(name) {
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (needed != 0) {
            original_.resize(needed);
            const DWORD written = GetEnvironmentVariableW(
                name, original_.data(), static_cast<DWORD>(original_.size()));
            if (written != 0 && written < original_.size()) {
                original_.resize(written);
                had_original_ = true;
            }
        }
        SetEnvironmentVariableW(name, value.c_str());
    }

    ~EnvironmentOverride() {
        SetEnvironmentVariableW(name_, had_original_ ? original_.c_str() : nullptr);
    }

private:
    const wchar_t* name_;
    std::wstring original_;
    bool had_original_{false};
};

}  // namespace

int main() {
    int failures = 0;
    const auto root = std::filesystem::temp_directory_path() / L"astra-vm-tests";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    EnvironmentOverride override_root(L"ASTRA_VM_ROOT", root.wstring());

    Expect(astra::AndroidVmRoot() == root,
           "ASTRA_VM_ROOT did not override the VM root", failures);
    Expect(astra::AndroidSdkRoot() == root / L"sdk" &&
               astra::AndroidAvdHome() == root / L"avd",
           "VM child paths are not deterministic", failures);
    const auto status = astra::InspectAndroidVm();
    Expect(!status.sdk_ready && !status.avd_ready && !status.device_ready,
           "empty VM root incorrectly appeared usable", failures);
    Expect(status.message.find(L"bootstrap-android-vm.ps1") != std::wstring::npos,
           "missing runtime status did not explain bootstrap path", failures);

    astra::InstalledApp absent;
    absent.package_path = root / L"missing.apkm";
    const auto missing = astra::InstallAndRunInAndroidVm(absent);
    Expect(!missing.ok && missing.message.find(L"missing") != std::wstring::npos,
           "missing package was not rejected before VM launch", failures);

    std::filesystem::remove_all(root, ignored);
    if (failures == 0) {
        std::cout << "All Astra Android VM tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
