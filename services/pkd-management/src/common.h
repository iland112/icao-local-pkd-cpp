#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>

/**
 * @brief LDIF Entry structure
 */
struct LdifEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;

    bool hasAttribute(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }

    std::string getFirstAttribute(const std::string& name) const {
        auto it = attributes.find(name);
        if (it != attributes.end() && !it->second.empty()) {
            return it->second[0];
        }
        return "";
    }
};

/**
 * @brief Validation statistics tracking
 */
struct ValidationStats {
    int validCount = 0;
    int invalidCount = 0;
    int pendingCount = 0;
    int errorCount = 0;
    int trustChainValidCount = 0;
    int trustChainInvalidCount = 0;
    int cscaNotFoundCount = 0;
    int expiredCount = 0;
    int revokedCount = 0;
};
