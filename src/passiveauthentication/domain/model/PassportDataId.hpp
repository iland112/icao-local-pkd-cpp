#pragma once

#include "shared/exception/DomainException.hpp"
#include "shared/util/UuidUtil.hpp"
#include <string>

namespace pa::domain::model {

/**
 * Unique identifier for PassportData.
 */
class PassportDataId {
private:
    std::string id_;

    void validate() const {
        if (id_.empty()) {
            throw shared::exception::DomainException(
                "INVALID_PASSPORT_DATA_ID",
                "PassportDataId cannot be empty"
            );
        }
    }

    explicit PassportDataId(std::string id) : id_(std::move(id)) {
        validate();
    }

public:
    PassportDataId() = default;

    /**
     * Create new PassportDataId with generated UUID.
     */
    static PassportDataId newId() {
        return PassportDataId(shared::util::UuidUtil::generate());
    }

    /**
     * Alias for newId() for backward compatibility.
     */
    static PassportDataId generate() {
        return newId();
    }

    /**
     * Create PassportDataId from existing UUID string.
     */
    static PassportDataId of(const std::string& id) {
        return PassportDataId(id);
    }

    const std::string& getId() const { return id_; }

    bool operator==(const PassportDataId& other) const {
        return id_ == other.id_;
    }

    bool operator!=(const PassportDataId& other) const {
        return !(*this == other);
    }
};

} // namespace pa::domain::model
