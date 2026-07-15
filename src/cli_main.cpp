#include "astra/package_store.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage() {
    std::wcout
        << L"Astra package utility\n\n"
        << L"  astra-cli install <package.apk|apkm|apks|xapk>\n"
        << L"  astra-cli list\n";
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
                   << L"Package: " << result.app.package_path.wstring() << L'\n'
                   << L"Data: " << result.app.data_path.wstring() << L'\n'
                   << L"SHA-256: " << result.app.container_sha256 << L'\n';
        return 0;
    }

    if (command == L"list") {
        const auto apps = astra::ListInstalledApps();
        for (const auto& app : apps) {
            std::wcout << app.id << L'\t' << app.display_name << L'\t'
                       << app.package_path.wstring() << L'\n';
        }
        return 0;
    }

    PrintUsage();
    return 1;
}
