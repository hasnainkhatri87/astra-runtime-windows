#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace astra {

struct ApkSignatureResult {
    bool ok{false};
    std::wstring message;
    std::vector<std::wstring> signer_sha256;
};

ApkSignatureResult VerifyApkV2Signature(const std::filesystem::path& apk_path);

}  // namespace astra
