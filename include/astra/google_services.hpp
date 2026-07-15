#pragma once

#include "astra/dex_catalog.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace astra {

struct GoogleServiceRequirements {
    bool analysis_ok{false};
    bool core{false};
    bool maps{false};
    bool location{false};
    bool authentication{false};
    bool cloud_messaging{false};
    bool billing{false};
    bool play_integrity{false};
    std::size_t strings_inspected{};
};

struct GoogleServicesPlan {
    GoogleServiceRequirements required;
    std::wstring required_services;
    std::wstring provider_id;
    std::wstring provider_state;
    std::wstring message;
    bool play_integrity_available{false};
};

GoogleServicesPlan AnalyzeGoogleServiceRequirements(
    const std::vector<DexInfo>& verified_dex_files);

std::wstring GoogleServicesPolicySummary();

}  // namespace astra
