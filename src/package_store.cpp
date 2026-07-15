#include "astra/package_store.hpp"

#include "astra/package_bundle.hpp"

#include "astra/sha256.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace astra {
namespace {

constexpr std::uintmax_t kMaximumPackageBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

std::wstring ReadIni(const std::filesystem::path& path,
                     const wchar_t* key) {
    std::array<wchar_t, 32768> value{};
    GetPrivateProfileStringW(L"app", key, L"", value.data(),
                             static_cast<DWORD>(value.size()), path.c_str());
    return value.data();
}

void WriteIni(const std::filesystem::path& path,
              const wchar_t* key,
              const std::wstring& value) {
    if (!WritePrivateProfileStringW(L"app", key, value.c_str(), path.c_str())) {
        throw std::runtime_error("Unable to write application metadata");
    }
}

std::filesystem::path LocalAppData() {
    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed == 0) {
        throw std::runtime_error("LOCALAPPDATA is unavailable");
    }

    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (written == 0 || written >= value.size()) {
        throw std::runtime_error("Unable to read LOCALAPPDATA");
    }
    value.resize(written);
    return value;
}

}  // namespace

std::filesystem::path AppStoreRoot() {
    return LocalAppData() / L"AstraRuntime" / L"apps";
}

bool IsSupportedPackageExtension(const std::filesystem::path& path) {
    static const std::set<std::wstring> extensions = {
        L".apk", L".apkm", L".apks", L".xapk"};
    return extensions.contains(Lower(path.extension().wstring()));
}

bool HasZipContainerSignature(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 4> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        return false;
    }

    return header[0] == 'P' && header[1] == 'K' &&
           ((header[2] == 3 && header[3] == 4) ||
            (header[2] == 5 && header[3] == 6) ||
            (header[2] == 7 && header[3] == 8));
}

std::wstring SanitizeId(std::wstring value) {
    for (auto& character : value) {
        const bool allowed = std::iswalnum(character) || character == L'-' ||
                             character == L'_' || character == L'.';
        if (!allowed) {
            character = L'-';
        }
    }

    while (!value.empty() && (value.front() == L'.' || value.front() == L'-')) {
        value.erase(value.begin());
    }
    if (value.empty()) {
        return L"package";
    }
    if (value.size() > 80) {
        value.resize(80);
    }
    return value;
}

InstallResult InstallPackage(const std::filesystem::path& source) {
    InstallResult result;
    try {
        if (!std::filesystem::is_regular_file(source)) {
            result.message = L"The selected package does not exist.";
            return result;
        }
        if (!IsSupportedPackageExtension(source)) {
            result.message = L"Choose an APK, APKM, APKS, or XAPK file.";
            return result;
        }

        const auto size = std::filesystem::file_size(source);
        if (size == 0 || size > kMaximumPackageBytes) {
            result.message = L"The package is empty or exceeds the 8 GB safety limit.";
            return result;
        }
        if (!HasZipContainerSignature(source)) {
            result.message = L"The selected file is not a valid ZIP-based Android package container.";
            return result;
        }

        const std::wstring sha256 = Sha256File(source);
        const std::wstring display_name = source.stem().wstring();
        const std::wstring id = SanitizeId(display_name) + L"-" + sha256.substr(0, 12);
        const auto app_root = AppStoreRoot() / id;
        const auto data_root = app_root / L"data";
        const auto package_name = L"package" + Lower(source.extension().wstring());
        const auto destination = app_root / package_name;

        std::filesystem::create_directories(data_root);
        if (!std::filesystem::exists(destination)) {
            std::filesystem::copy_file(source, destination,
                                       std::filesystem::copy_options::none);
        }

        const auto provision = ProvisionPackageSplits(destination, app_root);
        if (!provision.ok) {
            result.message = provision.message;
            return result;
        }

        const auto metadata = app_root / L"app.ini";
        WriteIni(metadata, L"id", id);
        WriteIni(metadata, L"display_name", display_name);
        WriteIni(metadata, L"package_path", destination.wstring());
        WriteIni(metadata, L"data_path", data_root.wstring());
        WriteIni(metadata, L"container_sha256", sha256);
        WriteIni(metadata, L"validation", L"container-indexed-crc-verified");
        WriteIni(metadata, L"selected_split_count",
                 std::to_wstring(provision.selected_apks.size()));

        result.ok = true;
        result.message = provision.message +
                         L" Android v2/v3 signature verification is the next trust gate.";
        result.app = {id, display_name, destination, data_root, sha256};
        return result;
    } catch (const std::exception&) {
        result.message = L"Installation failed while validating or copying the package.";
        return result;
    }
}

std::vector<InstalledApp> ListInstalledApps() {
    std::vector<InstalledApp> apps;
    try {
        const auto root = AppStoreRoot();
        if (!std::filesystem::exists(root)) {
            return apps;
        }

        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto metadata = entry.path() / L"app.ini";
            if (!std::filesystem::is_regular_file(metadata)) {
                continue;
            }

            InstalledApp app;
            app.id = ReadIni(metadata, L"id");
            app.display_name = ReadIni(metadata, L"display_name");
            app.package_path = ReadIni(metadata, L"package_path");
            app.data_path = ReadIni(metadata, L"data_path");
            app.container_sha256 = ReadIni(metadata, L"container_sha256");
            if (!app.id.empty() && std::filesystem::exists(app.package_path)) {
                apps.push_back(std::move(app));
            }
        }
    } catch (const std::exception&) {
        return {};
    }

    std::sort(apps.begin(), apps.end(), [](const auto& left, const auto& right) {
        return Lower(left.display_name) < Lower(right.display_name);
    });
    return apps;
}

}  // namespace astra
