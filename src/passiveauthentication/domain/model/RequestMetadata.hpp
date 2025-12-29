#pragma once

#include "shared/domain/ValueObject.hpp"
#include <string>
#include <optional>

namespace pa::domain::model {

/**
 * Request metadata for audit purposes.
 *
 * Contains information about the request source for audit logging.
 */
class RequestMetadata : public shared::domain::ValueObject {
private:
    std::optional<std::string> ipAddress_;
    std::optional<std::string> userAgent_;
    std::optional<std::string> requestedBy_;

    RequestMetadata(
        std::optional<std::string> ipAddress,
        std::optional<std::string> userAgent,
        std::optional<std::string> requestedBy
    ) : ipAddress_(std::move(ipAddress)),
        userAgent_(std::move(userAgent)),
        requestedBy_(std::move(requestedBy)) {}

public:
    RequestMetadata() = default;

    /**
     * Create RequestMetadata with all fields.
     */
    static RequestMetadata of(
        const std::string& ipAddress,
        const std::string& userAgent,
        const std::string& requestedBy
    ) {
        return RequestMetadata(
            ipAddress.empty() ? std::nullopt : std::optional<std::string>(ipAddress),
            userAgent.empty() ? std::nullopt : std::optional<std::string>(userAgent),
            requestedBy.empty() ? std::nullopt : std::optional<std::string>(requestedBy)
        );
    }

    /**
     * Create RequestMetadata with only IP address.
     */
    static RequestMetadata withIpAddress(const std::string& ipAddress) {
        return RequestMetadata(
            ipAddress.empty() ? std::nullopt : std::optional<std::string>(ipAddress),
            std::nullopt,
            std::nullopt
        );
    }

    /**
     * Create empty RequestMetadata.
     */
    static RequestMetadata empty() {
        return RequestMetadata(std::nullopt, std::nullopt, std::nullopt);
    }

    const std::optional<std::string>& getIpAddress() const { return ipAddress_; }
    const std::optional<std::string>& getUserAgent() const { return userAgent_; }
    const std::optional<std::string>& getRequestedBy() const { return requestedBy_; }

    bool hasIpAddress() const { return ipAddress_.has_value(); }
    bool hasUserAgent() const { return userAgent_.has_value(); }
    bool hasRequestedBy() const { return requestedBy_.has_value(); }
};

} // namespace pa::domain::model
