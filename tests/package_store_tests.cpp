#include "astra/package_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    Expect(astra::IsSupportedPackageExtension(L"sample.apk"),
           "APK extension should be supported");
    Expect(astra::IsSupportedPackageExtension(L"sample.APKM"),
           "extension matching should be case-insensitive");
    Expect(!astra::IsSupportedPackageExtension(L"sample.exe"),
           "EXE must not be accepted as a package");
    Expect(astra::SanitizeId(L" hello/package ") == L"hello-package-",
           "unsafe identifier characters should be replaced");
    Expect(astra::SanitizeId(L"...") == L"package",
           "empty sanitized identifiers should use a safe fallback");

    const auto temp = std::filesystem::temp_directory_path() /
                      L"astra-container-signature-test.apk";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        const unsigned char bytes[] = {'P', 'K', 3, 4, 0, 0, 0, 0};
        output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
    Expect(astra::HasZipContainerSignature(temp),
           "ZIP local-header signature should be recognized");
    std::filesystem::remove(temp);

    if (failures == 0) {
        std::cout << "All Astra package-store tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
