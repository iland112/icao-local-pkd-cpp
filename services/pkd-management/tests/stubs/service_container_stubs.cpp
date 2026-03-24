/**
 * @file service_container_stubs.cpp
 * @brief Linker stubs for ServiceContainer and Repository methods.
 *
 * Used by test_progress_manager and test_certificate_utils.
 * These tests set g_services = nullptr so none of these functions are ever
 * actually called at runtime — but the linker needs the symbols defined.
 */

#include "../../src/infrastructure/service_container.h"
#include "../../src/repositories/certificate_repository.h"
#include "../../src/repositories/upload_repository.h"

// ---------------------------------------------------------------------------
// ServiceContainer stubs
// ---------------------------------------------------------------------------
namespace infrastructure {

repositories::UploadRepository* ServiceContainer::uploadRepository() const {
    return nullptr;
}

repositories::CertificateRepository* ServiceContainer::certificateRepository() const {
    return nullptr;
}

} // namespace infrastructure

// ---------------------------------------------------------------------------
// CertificateRepository stubs
// ---------------------------------------------------------------------------
namespace repositories {

// Constructor / destructor
CertificateRepository::CertificateRepository(common::IQueryExecutor*) {}

std::pair<std::string, bool>
CertificateRepository::saveCertificateWithDuplicateCheck(
    const std::string&, const std::string&, const std::string&,
    const std::string&, const std::string&, const std::string&,
    const std::string&, const std::string&, const std::string&,
    const std::vector<uint8_t>&, const std::string&, const std::string&,
    const x509::CertificateMetadata*, const std::string&) {
    return {"", false};
}

bool CertificateRepository::trackCertificateDuplicate(
    const std::string&, const std::string&, const std::string&,
    const std::string&, const std::string&, const std::string&) {
    return false;
}

bool CertificateRepository::incrementDuplicateCount(
    const std::string&, const std::string&) {
    return false;
}

bool CertificateRepository::updateCertificateLdapStatus(
    const std::string&, const std::string&) {
    return false;
}

// ---------------------------------------------------------------------------
// UploadRepository stubs
// ---------------------------------------------------------------------------

UploadRepository::UploadRepository(common::IQueryExecutor*) {}

bool UploadRepository::updateStatus(
    const std::string&, const std::string&, const std::string&) {
    return false;
}

bool UploadRepository::updateStatistics(
    const std::string&, int, int, int, int, int, int) {
    return false;
}

} // namespace repositories
