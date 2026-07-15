#include "astra/package_bundle.hpp"

#include "astra/zip_archive.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <initializer_list>
#include <fstream>
#include <set>
#include <stdexcept>

namespace astra {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool EndsWith(const std::wstring& value, const std::wstring& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::wstring BaseName(const std::wstring& path) {
    const auto slash = path.find_last_of(L'/');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool ContainsAny(const std::wstring& value,
                 std::initializer_list<const wchar_t*> values) {
    for (const auto* candidate : values) {
        if (value.find(candidate) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool IsBase(const std::wstring& lower_name) {
    const auto base = BaseName(lower_name);
    return base == L"base.apk" || base == L"base-master.apk" ||
           base == L"master.apk" || base == L"standalone.apk";
}

bool IsWrongAbi(const std::wstring& value) {
    if (value.find(L"x86_64") != std::wstring::npos ||
        value.find(L"x86-64") != std::wstring::npos) {
        return false;
    }
    return ContainsAny(value, {L"arm64", L"armeabi", L"arm_v7", L"arm-v7",
                               L"mips", L"config.x86.apk", L"-x86.apk",
                               L"_x86.apk"});
}

bool IsWrongDensity(const std::wstring& value) {
    if (value.find(L"xhdpi") != std::wstring::npos &&
        value.find(L"xxhdpi") == std::wstring::npos) {
        return false;
    }
    return ContainsAny(value, {L"ldpi", L"mdpi", L"hdpi", L"xxhdpi",
                               L"xxxhdpi", L"tvdpi"});
}

bool IsWrongLanguageConfig(const std::wstring& value) {
    const auto marker = value.find(L"split_config.");
    if (marker == std::wstring::npos) {
        return false;
    }
    const auto token_start = marker + std::wstring(L"split_config.").size();
    const auto token_end = value.find(L'.', token_start);
    if (token_end == std::wstring::npos) {
        return false;
    }
    const auto token = value.substr(token_start, token_end - token_start);
    if (token == L"en" || token.rfind(L"en-", 0) == 0) {
        return false;
    }
    if (token.size() == 2 && std::iswalpha(token[0]) && std::iswalpha(token[1])) {
        return true;
    }
    return token.size() >= 5 && token[2] == L'-';
}

std::wstring SafeOutputName(std::size_t index, const std::wstring& entry_name) {
    std::wstring base = BaseName(entry_name);
    for (auto& character : base) {
        if (!(std::iswalnum(character) || character == L'.' ||
              character == L'-' || character == L'_')) {
            character = L'-';
        }
    }
    if (base.empty()) {
        base = L"split.apk";
    }
    wchar_t prefix[16]{};
    swprintf_s(prefix, L"%03zu-", index);
    return std::wstring(prefix) + base;
}

}  // namespace

ProvisionResult ProvisionPackageSplits(const std::filesystem::path& source,
                                       const std::filesystem::path& app_root) {
    ProvisionResult result;
    try {
        const auto split_root = app_root / L"splits";
        std::filesystem::create_directories(split_root);

        if (Lower(source.extension().wstring()) == L".apk") {
            const auto destination = split_root / L"000-base.apk";
            if (!std::filesystem::exists(destination)) {
                std::filesystem::copy_file(source, destination,
                                           std::filesystem::copy_options::none);
            }
            result.selected_apks.push_back(destination);
            result.ok = true;
            result.message = L"Single APK provisioned.";
            return result;
        }

        const auto archive = ZipArchive::Open(source);
        std::vector<const ZipEntry*> candidates;
        for (const auto& entry : archive.entries()) {
            if (!entry.is_directory && EndsWith(Lower(entry.name), L".apk")) {
                candidates.push_back(&entry);
            }
        }
        if (candidates.empty()) {
            result.message = L"The package bundle contains no APK splits.";
            return result;
        }

        std::vector<const ZipEntry*> selected;
        for (const auto* entry : candidates) {
            const auto lower_name = Lower(entry->name);
            if (IsBase(lower_name)) {
                selected.insert(selected.begin(), entry);
                continue;
            }
            if (IsWrongAbi(lower_name) || IsWrongDensity(lower_name) ||
                IsWrongLanguageConfig(lower_name)) {
                continue;
            }
            selected.push_back(entry);
        }
        if (selected.empty() || !IsBase(Lower(selected.front()->name))) {
            const auto largest = *std::max_element(
                candidates.begin(), candidates.end(),
                [](const ZipEntry* left, const ZipEntry* right) {
                    return left->uncompressed_size < right->uncompressed_size;
                });
            selected.insert(selected.begin(), largest);
        }

        std::set<std::wstring> selected_names;
        std::vector<const ZipEntry*> unique;
        for (const auto* entry : selected) {
            if (selected_names.insert(Lower(entry->name)).second) {
                unique.push_back(entry);
            }
        }

        std::ofstream manifest(split_root / L"selected-splits.txt",
                               std::ios::binary | std::ios::trunc);
        if (!manifest) {
            throw std::runtime_error("Unable to create split selection manifest");
        }
        for (std::size_t index = 0; index < unique.size(); ++index) {
            const auto destination = split_root / SafeOutputName(index, unique[index]->name);
            if (!std::filesystem::exists(destination)) {
                archive.Extract(*unique[index], destination);
            }
            result.selected_apks.push_back(destination);
            const auto line = unique[index]->name + L"\t" + destination.filename().wstring() + L"\r\n";
            const int bytes_needed = WideCharToMultiByte(
                CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<std::size_t>(bytes_needed), '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                utf8.data(), bytes_needed, nullptr, nullptr);
            manifest.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        }
        manifest.flush();
        if (!manifest) {
            throw std::runtime_error("Unable to save split selection manifest");
        }

        result.ok = true;
        result.message = L"Selected and CRC-verified " +
                         std::to_wstring(result.selected_apks.size()) +
                         L" APK split(s) for x86-64 Windows.";
        return result;
    } catch (const std::exception&) {
        result.message = L"The package bundle failed structural or extraction validation.";
        return result;
    }
}

}  // namespace astra
