/**
 * @file ValidateCertificateCommand.hpp
 * @brief Command for certificate validation
 */

#pragma once

#include <string>
#include <optional>

namespace certificatevalidation::application::command {

/**
 * @brief Command for validating a certificate
 */
struct ValidateCertificateCommand {
    std::string certificateId;
    std::optional<std::string> trustAnchorId;
    bool checkRevocation = true;

    static ValidateCertificateCommand of(
        const std::string& certificateId,
        bool checkRevocation = true
    ) {
        ValidateCertificateCommand cmd;
        cmd.certificateId = certificateId;
        cmd.checkRevocation = checkRevocation;
        return cmd;
    }

    static ValidateCertificateCommand withTrustAnchor(
        const std::string& certificateId,
        const std::string& trustAnchorId,
        bool checkRevocation = true
    ) {
        ValidateCertificateCommand cmd;
        cmd.certificateId = certificateId;
        cmd.trustAnchorId = trustAnchorId;
        cmd.checkRevocation = checkRevocation;
        return cmd;
    }
};

/**
 * @brief Command for checking certificate revocation
 */
struct CheckRevocationCommand {
    std::string certificateId;

    static CheckRevocationCommand of(const std::string& certificateId) {
        CheckRevocationCommand cmd;
        cmd.certificateId = certificateId;
        return cmd;
    }
};

/**
 * @brief Command for verifying trust chain
 */
struct VerifyTrustChainCommand {
    std::string dscId;
    std::optional<std::string> cscaId;

    static VerifyTrustChainCommand of(const std::string& dscId) {
        VerifyTrustChainCommand cmd;
        cmd.dscId = dscId;
        return cmd;
    }

    static VerifyTrustChainCommand withCsca(
        const std::string& dscId,
        const std::string& cscaId
    ) {
        VerifyTrustChainCommand cmd;
        cmd.dscId = dscId;
        cmd.cscaId = cscaId;
        return cmd;
    }
};

} // namespace certificatevalidation::application::command
