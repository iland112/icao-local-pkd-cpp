/**
 * @file g_services_stub.cpp
 * @brief Defines g_services=nullptr plus linker stubs for ServiceContainer and
 *        Repository methods referenced by certificate_utils.cpp.
 *
 * Used by test_lc_validator which includes certificate_utils.cpp but does NOT
 * define g_services itself.  All DB-dependent paths in certificate_utils.cpp
 * are guarded by `if (!g_services)` so none of these stubs are ever called at
 * runtime.
 *
 * NOT used by test_certificate_utils or test_progress_manager — those tests
 * define g_services = nullptr directly in their own .cpp file and rely on
 * service_container_stubs.cpp for the method stubs.
 */

#include "../../src/infrastructure/service_container.h"
#include "../../src/repositories/certificate_repository.h"
#include "../../src/repositories/upload_repository.h"

// ---------------------------------------------------------------------------
// Global service container pointer — nullptr disables all DB-dependent paths
// ---------------------------------------------------------------------------
infrastructure::ServiceContainer* g_services = nullptr;

// ---------------------------------------------------------------------------
// ServiceContainer method stubs
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
