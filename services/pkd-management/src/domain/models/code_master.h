#pragma once

/**
 * @file code_master.h
 * @brief CodeMaster domain model — centralized code/status/enum management
 */

#include <string>
#include <optional>

namespace domain {
namespace models {

struct CodeMaster {
    std::string id;
    std::string category;
    std::string code;
    std::string nameKo;
    std::optional<std::string> nameEn;
    std::optional<std::string> description;
    std::optional<std::string> severity;
    int sortOrder = 0;
    bool isActive = true;
    std::optional<std::string> metadata;  // JSON string
    std::string createdAt;
    std::string updatedAt;
};

} // namespace models
} // namespace domain
