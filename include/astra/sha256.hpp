#pragma once

#include <filesystem>
#include <string>

namespace astra {

std::wstring Sha256File(const std::filesystem::path& path);

}  // namespace astra
