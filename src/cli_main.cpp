#include "astra/package_store.hpp"
#include "astra/google_services.hpp"
#include "astra/android_vm.hpp"
#include "astra/dex_executor.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage() {
    std::wcout
        << L"Astra package utility\n\n"
        << L"  astra-cli install <package.apk|apkm|apks|xapk>\n"
        << L"  astra-cli list\n"
        << L"  astra-cli services\n"
        << L"  astra-cli dex-execute <file.dex> <class-descriptor> <method>\n"
        << L"  astra-cli vm-status\n"
        << L"  astra-cli vm-start\n"
        << L"  astra-cli vm-run <installed-app-id>\n";
}

bool NarrowAscii(const wchar_t* value, std::string& output) {
    output.clear();
    for (; *value != 0; ++value) {
        if (static_cast<unsigned int>(*value) > 0x7fU) {
            return false;
        }
        output.push_back(static_cast<char>(*value));
    }
    return true;
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    if (argument_count < 2) {
        PrintUsage();
        return 1;
    }

    const std::wstring command = arguments[1];
    if (command == L"install") {
        if (argument_count != 3) {
            PrintUsage();
            return 1;
        }
        const auto result = astra::InstallPackage(arguments[2]);
        std::wcout << result.message << L'\n';
        if (!result.ok) {
            return 2;
        }
        std::wcout << L"ID: " << result.app.id << L'\n'
                   << L"Android package: " << result.app.package_name << L'\n'
                   << L"Launcher Activity: " << result.app.launcher_activity << L'\n'
                   << L"Google APIs required: "
                   << result.app.google_services_required << L'\n'
                   << L"Service provider: "
                   << result.app.google_services_provider << L'\n'
                   << L"Provider state: "
                   << result.app.google_services_state << L'\n'
                   << L"Package file: " << result.app.package_path.wstring() << L'\n'
                   << L"Data: " << result.app.data_path.wstring() << L'\n'
                   << L"SHA-256: " << result.app.container_sha256 << L'\n';
        return 0;
    }

    if (command == L"list") {
        const auto apps = astra::ListInstalledApps();
        for (const auto& app : apps) {
            std::wcout << app.id << L'\t' << app.package_name << L'\t'
                       << app.launcher_activity << L'\t'
                       << app.google_services_provider << L'\t'
                       << app.google_services_state << L'\t'
                       << app.package_path.wstring() << L'\n';
        }
        return 0;
    }

    if (command == L"services") {
        std::wcout << astra::GoogleServicesPolicySummary();
        return 0;
    }

    if (command == L"vm-status") {
        if (argument_count != 2) {
            PrintUsage();
            return 1;
        }
        const auto status = astra::InspectAndroidVm();
        std::wcout << status.message << L'\n'
                   << L"Runtime root: " << status.runtime_root.wstring() << L'\n'
                   << L"SDK: " << (status.sdk_ready ? L"ready" : L"missing") << L'\n'
                   << L"AVD: " << (status.avd_ready ? L"ready" : L"missing") << L'\n'
                   << L"Firmware virtualization: "
                   << (status.firmware_virtualization_ready ? L"ready" : L"unavailable") << L'\n'
                   << L"WHPX acceleration: "
                   << (status.acceleration_ready ? L"ready" : L"unavailable") << L'\n'
                   << L"Android device: " << (status.device_ready ? L"running" : L"stopped") << L'\n';
        return status.sdk_ready && status.avd_ready && status.acceleration_ready ? 0 : 4;
    }

    if (command == L"vm-start") {
        if (argument_count != 2) {
            PrintUsage();
            return 1;
        }
        const auto result = astra::StartAndroidVm();
        std::wcout << result.message << L'\n';
        return result.ok ? 0 : 4;
    }

    if (command == L"vm-run") {
        if (argument_count != 3) {
            PrintUsage();
            return 1;
        }
        const auto apps = astra::ListInstalledApps();
        for (const auto& app : apps) {
            if (app.id == arguments[2]) {
                const auto result = astra::InstallAndRunInAndroidVm(app);
                std::wcout << result.message << L'\n';
                return result.ok ? 0 : 4;
            }
        }
        std::wcerr << L"No installed Astra app has that ID. Run astra-cli list first.\n";
        return 2;
    }

    if (command == L"dex-execute") {
        if (argument_count != 5) {
            PrintUsage();
            return 1;
        }
        std::string class_descriptor;
        std::string method_name;
        if (!NarrowAscii(arguments[3], class_descriptor) ||
            !NarrowAscii(arguments[4], method_name)) {
            std::wcerr <<
                L"DEX class descriptors and method names must be ASCII.\n";
            return 1;
        }
        const auto result = astra::ExecuteStaticIntMethod(
            arguments[2], class_descriptor, method_name);
        std::wcout << result.message << L'\n';
        return result.ok ? 0 : 3;
    }

    PrintUsage();
    return 1;
}
