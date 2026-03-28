/**
 * @file upload_module_stubs.cpp
 * @brief Linker stubs for upload module repositories and services
 *
 * test_upload_services links upload_services.cpp (the UploadServiceContainer)
 * which constructs all 6 repositories and 3 services via make_shared<>.
 * Each of those types has a real .cpp file with heavy dependencies (JsonCpp,
 * OpenSSL, LDAP, Drogon, ...).  Rather than pulling in the full dependency
 * chain, we provide minimal stub constructors that satisfy the linker without
 * executing any real I/O.
 *
 * IMPORTANT: These stubs are ONLY for use with unit-test executables.
 *            They must never be linked into the production binary.
 *
 * All stub constructors just store the pointer (or do nothing).  Because
 * UploadServiceContainer tests only verify that accessors return non-null
 * after initialize() and null after shutdown(), no actual repository/service
 * methods are ever called at runtime.
 */

#include <memory>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>
#include <cstdint>

// Forward declarations of shared types used in constructor signatures
#include "i_query_executor.h"
#include <ldap_connection_pool.h>

// Validation providers (needed by ValidationService ctor)
#include <icao/validation/providers.h>

// --- Repository headers ---
#include "upload/repositories/upload_repository.h"
#include "upload/repositories/certificate_repository.h"
#include "upload/repositories/crl_repository.h"
#include "upload/repositories/validation_repository.h"
#include "upload/repositories/deviation_list_repository.h"
#include "upload/repositories/ldif_structure_repository.h"

// --- Service headers ---
#include "upload/services/upload_service.h"
#include "upload/services/validation_service.h"
#include "upload/services/ldif_structure_service.h"

// ============================================================================
// Repository stub constructors
// ============================================================================

namespace repositories {

UploadRepository::UploadRepository(common::IQueryExecutor* qe)
    : queryExecutor_(qe)
{
    if (!queryExecutor_)
        throw std::invalid_argument("UploadRepository: queryExecutor cannot be nullptr");
}

CertificateRepository::CertificateRepository(common::IQueryExecutor* /*qe*/) {}

CrlRepository::CrlRepository(common::IQueryExecutor* /*qe*/) {}

ValidationRepository::ValidationRepository(
    common::IQueryExecutor* /*qe*/,
    std::shared_ptr<common::LdapConnectionPool> /*ldapPool*/,
    const std::string& /*ldapBaseDn*/)
{}

DeviationListRepository::DeviationListRepository(common::IQueryExecutor* /*qe*/) {}

LdifStructureRepository::LdifStructureRepository(UploadRepository* /*repo*/) {}
LdifStructureData LdifStructureRepository::getLdifStructure(const std::string& /*uploadId*/, int /*maxEntries*/) { return {}; }

} // namespace repositories

// ============================================================================
// Service stub constructors
// ============================================================================

namespace services {

UploadService::UploadService(
    repositories::UploadRepository* /*uploadRepo*/,
    repositories::CertificateRepository* /*certRepo*/,
    common::LdapConnectionPool* /*ldapPool*/,
    repositories::DeviationListRepository* /*dlRepo*/)
{}

ValidationService::ValidationService(
    repositories::ValidationRepository* /*validationRepo*/,
    repositories::CertificateRepository* /*certRepo*/,
    repositories::CrlRepository* /*crlRepo*/,
    icao::validation::ICscaProvider* /*ldapCscaProvider*/,
    icao::validation::ICrlProvider* /*ldapCrlProvider*/)
{}

LdifStructureService::LdifStructureService(
    repositories::LdifStructureRepository* /*repo*/)
{}

} // namespace services
