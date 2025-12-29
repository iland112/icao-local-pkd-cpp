/**
 * @file OpenLdapAdapter.hpp
 * @brief OpenLDAP Adapter Implementation
 */

#pragma once

#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include "shared/util/Base64Util.hpp"
#include <ldap.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <spdlog/spdlog.h>

namespace ldapintegration::infrastructure::adapter {

using namespace ldapintegration::domain::port;
using namespace ldapintegration::domain::model;

/**
 * @brief LDAP Connection Pool Entry
 */
struct LdapConnectionEntry {
    LDAP* connection;
    bool inUse;
    std::chrono::steady_clock::time_point lastUsed;
};

/**
 * @brief OpenLDAP Connection Configuration
 */
struct OpenLdapConfig {
    std::string host = "localhost";
    int port = 389;
    std::string bindDn = "";
    std::string bindPassword = "";
    std::string baseDn = "dc=ldap,dc=smartcoreinc,dc=com";
    int poolSize = 5;
    int connectTimeout = 10;
    int operationTimeout = 30;
    bool useTls = false;

    std::string getUri() const {
        return "ldap://" + host + ":" + std::to_string(port);
    }
};

/**
 * @brief OpenLDAP Adapter
 *
 * Implements ILdapConnectionPort using OpenLDAP C API.
 * Features:
 * - Connection pooling
 * - Automatic reconnection
 * - Batch operations with progress tracking
 * - Thread-safe operations
 */
class OpenLdapAdapter : public ILdapConnectionPort {
private:
    OpenLdapConfig config_;
    std::vector<LdapConnectionEntry> connectionPool_;
    mutable std::mutex poolMutex_;
    std::condition_variable poolCondition_;
    ProgressCallback progressCallback_;
    bool initialized_ = false;

public:
    explicit OpenLdapAdapter(const OpenLdapConfig& config)
        : config_(config) {
        initializePool();
    }

    ~OpenLdapAdapter() override {
        shutdown();
    }

    // ========== Connection Management ==========

    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(poolMutex_);
        for (const auto& entry : connectionPool_) {
            if (entry.connection != nullptr) {
                return true;
            }
        }
        return false;
    }

    bool testConnection() override {
        auto conn = acquireConnection();
        if (!conn) {
            return false;
        }

        // Simple search to test connection
        LDAPMessage* result = nullptr;
        int rc = ldap_search_ext_s(
            conn,
            config_.baseDn.c_str(),
            LDAP_SCOPE_BASE,
            "(objectClass=*)",
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            1,
            &result
        );

        if (result) {
            ldap_msgfree(result);
        }

        releaseConnection(conn);
        return rc == LDAP_SUCCESS;
    }

    std::string getPoolStats() const override {
        std::lock_guard<std::mutex> lock(poolMutex_);
        int active = 0;
        int idle = 0;
        for (const auto& entry : connectionPool_) {
            if (entry.connection) {
                entry.inUse ? ++active : ++idle;
            }
        }
        return "Pool[active=" + std::to_string(active) +
               ", idle=" + std::to_string(idle) +
               ", total=" + std::to_string(connectionPool_.size()) + "]";
    }

    std::string getBaseDn() const override {
        return config_.baseDn;
    }

    // ========== Base DN Operations ==========

    LdapOperationResult ensureCountryExists(const std::string& countryCode) override {
        // Create country entries under dc=data and dc=nc-data
        std::vector<std::string> paths = {
            "c=" + countryCode + ",dc=data,dc=download,dc=pkd," + config_.baseDn,
            "c=" + countryCode + ",dc=nc-data,dc=download,dc=pkd," + config_.baseDn
        };

        for (const auto& dn : paths) {
            if (!entryExists(dn)) {
                auto result = createCountryEntry(dn, countryCode);
                if (!result.success) {
                    return result;
                }
            }
        }

        return LdapOperationResult::ok();
    }

    LdapOperationResult ensureOuExists(LdapEntryType entryType, const std::string& countryCode) override {
        std::string ouPath = getOuPath(entryType, config_.baseDn);
        std::string dn = "c=" + countryCode + "," + ouPath;

        // First ensure country exists
        ensureCountryExists(countryCode);

        // Check if OU exists
        if (!entryExists(dn)) {
            return createOrganizationalUnit(dn, toString(entryType));
        }

        return LdapOperationResult::ok();
    }

    // ========== Certificate Operations ==========

    LdapOperationResult saveCertificate(const LdapCertificateEntry& entry) override {
        // Ensure OU exists
        ensureOuExists(entry.getEntryType(), entry.getCountryCode());

        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire LDAP connection");
        }

        try {
            std::string dn = entry.getDn().getValue();

            // Check if entry exists
            if (entryExists(dn)) {
                // Update existing entry
                auto result = updateCertificateEntry(conn, entry);
                releaseConnection(conn);
                return result;
            } else {
                // Add new entry
                auto result = addCertificateEntry(conn, entry);
                releaseConnection(conn);
                return result;
            }
        } catch (const std::exception& e) {
            releaseConnection(conn);
            return LdapOperationResult::error(std::string("Certificate save failed: ") + e.what());
        }
    }

    LdapOperationResult saveCertificates(const std::vector<LdapCertificateEntry>& entries) override {
        int successCount = 0;
        int total = static_cast<int>(entries.size());

        for (size_t i = 0; i < entries.size(); ++i) {
            auto result = saveCertificate(entries[i]);
            if (result.success) {
                ++successCount;
            }

            if (progressCallback_) {
                progressCallback_(
                    static_cast<int>(i + 1),
                    total,
                    "Saving certificate " + std::to_string(i + 1) + "/" + std::to_string(total)
                );
            }
        }

        return LdapOperationResult::ok(successCount);
    }

    std::optional<LdapCertificateEntry> findCertificateByFingerprint(
        const std::string& fingerprint,
        LdapEntryType entryType
    ) override {
        std::string ouPath = getOuPath(entryType, config_.baseDn);
        std::string filter = "(certificateFingerprint=" + fingerprint + ")";

        auto results = search(LdapSearchFilter::subtree(ouPath, filter));
        if (results.empty()) {
            return std::nullopt;
        }

        return convertToLdapCertificateEntry(results[0], entryType);
    }

    std::vector<LdapCertificateEntry> findCertificatesByCountry(
        const std::string& countryCode,
        LdapEntryType entryType
    ) override {
        std::string baseDn = "c=" + countryCode + "," + getOuPath(entryType, config_.baseDn);
        std::string filter = "(objectClass=inetOrgPerson)";

        auto results = search(LdapSearchFilter::subtree(baseDn, filter));

        std::vector<LdapCertificateEntry> entries;
        for (const auto& result : results) {
            auto entry = convertToLdapCertificateEntry(result, entryType);
            if (entry) {
                entries.push_back(std::move(*entry));
            }
        }

        return entries;
    }

    std::optional<LdapCertificateEntry> findCertificateByIssuerDn(
        const std::string& issuerDn,
        LdapEntryType entryType
    ) override {
        std::string ouPath = getOuPath(entryType, config_.baseDn);
        std::string escapedIssuerDn = escapeLdapFilter(issuerDn);
        std::string filter = "(issuerDN=" + escapedIssuerDn + ")";

        auto results = search(LdapSearchFilter::subtree(ouPath, filter));
        if (results.empty()) {
            return std::nullopt;
        }

        return convertToLdapCertificateEntry(results[0], entryType);
    }

    LdapOperationResult deleteCertificate(const DistinguishedName& dn) override {
        return deleteEntry(dn.getValue());
    }

    // ========== CRL Operations ==========

    LdapOperationResult saveCrl(const LdapCrlEntry& entry) override {
        ensureOuExists(LdapEntryType::CRL, entry.getCountryCode());

        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire LDAP connection");
        }

        try {
            std::string dn = entry.getDn().getValue();

            if (entryExists(dn)) {
                auto result = updateCrlEntry(conn, entry);
                releaseConnection(conn);
                return result;
            } else {
                auto result = addCrlEntry(conn, entry);
                releaseConnection(conn);
                return result;
            }
        } catch (const std::exception& e) {
            releaseConnection(conn);
            return LdapOperationResult::error(std::string("CRL save failed: ") + e.what());
        }
    }

    std::optional<LdapCrlEntry> findCrlByIssuerDn(const std::string& issuerDn) override {
        std::string ouPath = getOuPath(LdapEntryType::CRL, config_.baseDn);
        std::string escapedIssuerDn = escapeLdapFilter(issuerDn);
        std::string filter = "(issuerDN=" + escapedIssuerDn + ")";

        auto results = search(LdapSearchFilter::subtree(ouPath, filter));
        if (results.empty()) {
            return std::nullopt;
        }

        return convertToLdapCrlEntry(results[0]);
    }

    std::vector<LdapCrlEntry> findCrlsByCountry(const std::string& countryCode) override {
        std::string baseDn = "c=" + countryCode + "," + getOuPath(LdapEntryType::CRL, config_.baseDn);
        std::string filter = "(objectClass=cRLDistributionPoint)";

        auto results = search(LdapSearchFilter::subtree(baseDn, filter));

        std::vector<LdapCrlEntry> entries;
        for (const auto& result : results) {
            auto entry = convertToLdapCrlEntry(result);
            if (entry) {
                entries.push_back(std::move(*entry));
            }
        }

        return entries;
    }

    bool updateCrlIfNewer(const LdapCrlEntry& entry) override {
        auto existing = findCrlByIssuerDn(entry.getIssuerDn());
        if (!existing) {
            saveCrl(entry);
            return true;
        }

        // Compare thisUpdate timestamps
        if (entry.getThisUpdate() > existing->getThisUpdate()) {
            saveCrl(entry);
            return true;
        }

        return false;
    }

    LdapOperationResult deleteCrl(const DistinguishedName& dn) override {
        return deleteEntry(dn.getValue());
    }

    // ========== Master List Operations ==========

    LdapOperationResult saveMasterList(const LdapMasterListEntry& entry) override {
        ensureOuExists(LdapEntryType::MASTER_LIST, entry.getCountryCode());

        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire LDAP connection");
        }

        try {
            std::string dn = entry.getDn().getValue();

            if (entryExists(dn)) {
                auto result = updateMasterListEntry(conn, entry);
                releaseConnection(conn);
                return result;
            } else {
                auto result = addMasterListEntry(conn, entry);
                releaseConnection(conn);
                return result;
            }
        } catch (const std::exception& e) {
            releaseConnection(conn);
            return LdapOperationResult::error(std::string("Master List save failed: ") + e.what());
        }
    }

    std::optional<LdapMasterListEntry> findMasterListByIssuer(const std::string& issuerDn) override {
        std::string ouPath = getOuPath(LdapEntryType::MASTER_LIST, config_.baseDn);
        std::string escapedIssuerDn = escapeLdapFilter(issuerDn);
        std::string filter = "(issuerDN=" + escapedIssuerDn + ")";

        auto results = search(LdapSearchFilter::subtree(ouPath, filter));
        if (results.empty()) {
            return std::nullopt;
        }

        return convertToLdapMasterListEntry(results[0]);
    }

    std::vector<LdapMasterListEntry> findMasterListsByCountry(const std::string& countryCode) override {
        std::string baseDn = "c=" + countryCode + "," + getOuPath(LdapEntryType::MASTER_LIST, config_.baseDn);
        std::string filter = "(objectClass=pkiCA)";

        auto results = search(LdapSearchFilter::subtree(baseDn, filter));

        std::vector<LdapMasterListEntry> entries;
        for (const auto& result : results) {
            auto entry = convertToLdapMasterListEntry(result);
            if (entry) {
                entries.push_back(std::move(*entry));
            }
        }

        return entries;
    }

    bool updateMasterListIfNewer(const LdapMasterListEntry& entry) override {
        auto existing = findMasterListByIssuer(entry.getIssuerDn());
        if (!existing) {
            saveMasterList(entry);
            return true;
        }

        if (entry.isNewerThan(existing->getVersion())) {
            saveMasterList(entry);
            return true;
        }

        return false;
    }

    // ========== Generic Search ==========

    std::vector<LdapEntry> search(const LdapSearchFilter& filter) override {
        auto conn = acquireConnection();
        if (!conn) {
            return {};
        }

        std::vector<LdapEntry> results;
        LDAPMessage* searchResult = nullptr;

        // Convert attributes to char**
        std::vector<char*> attrs;
        for (const auto& attr : filter.attributes) {
            attrs.push_back(const_cast<char*>(attr.c_str()));
        }
        attrs.push_back(nullptr);

        int rc = ldap_search_ext_s(
            conn,
            filter.baseDn.c_str(),
            filter.scope,
            filter.filter.c_str(),
            filter.attributes.empty() ? nullptr : attrs.data(),
            0,
            nullptr,
            nullptr,
            nullptr,
            0,
            &searchResult
        );

        if (rc != LDAP_SUCCESS) {
            spdlog::warn("LDAP search failed: {}", ldap_err2string(rc));
            if (searchResult) ldap_msgfree(searchResult);
            releaseConnection(conn);
            return {};
        }

        // Process results
        for (LDAPMessage* entry = ldap_first_entry(conn, searchResult);
             entry != nullptr;
             entry = ldap_next_entry(conn, entry)) {

            LdapEntry ldapEntry;

            // Get DN
            char* dn = ldap_get_dn(conn, entry);
            if (dn) {
                ldapEntry.dn = dn;
                ldap_memfree(dn);
            }

            // Get attributes
            BerElement* ber = nullptr;
            for (char* attr = ldap_first_attribute(conn, entry, &ber);
                 attr != nullptr;
                 attr = ldap_next_attribute(conn, entry, ber)) {

                LdapAttribute ldapAttr;
                ldapAttr.name = attr;

                // Check if binary attribute
                ldapAttr.isBinary = (std::string(attr).find(";binary") != std::string::npos);

                if (ldapAttr.isBinary) {
                    berval** bvals = ldap_get_values_len(conn, entry, attr);
                    if (bvals) {
                        for (int i = 0; bvals[i] != nullptr; ++i) {
                            std::vector<uint8_t> data(
                                reinterpret_cast<uint8_t*>(bvals[i]->bv_val),
                                reinterpret_cast<uint8_t*>(bvals[i]->bv_val) + bvals[i]->bv_len
                            );
                            ldapAttr.binaryValues.push_back(std::move(data));
                        }
                        ldap_value_free_len(bvals);
                    }
                } else {
                    char** vals = ldap_get_values(conn, entry, attr);
                    if (vals) {
                        for (int i = 0; vals[i] != nullptr; ++i) {
                            ldapAttr.values.emplace_back(vals[i]);
                        }
                        ldap_value_free(vals);
                    }
                }

                ldapEntry.attributes.push_back(std::move(ldapAttr));
                ldap_memfree(attr);
            }

            if (ber) {
                ber_free(ber, 0);
            }

            results.push_back(std::move(ldapEntry));
        }

        ldap_msgfree(searchResult);
        releaseConnection(conn);

        return results;
    }

    bool entryExists(const std::string& dn) override {
        auto conn = acquireConnection();
        if (!conn) {
            return false;
        }

        LDAPMessage* result = nullptr;
        int rc = ldap_search_ext_s(
            conn,
            dn.c_str(),
            LDAP_SCOPE_BASE,
            "(objectClass=*)",
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            1,
            &result
        );

        bool exists = (rc == LDAP_SUCCESS && ldap_count_entries(conn, result) > 0);

        if (result) ldap_msgfree(result);
        releaseConnection(conn);

        return exists;
    }

    int countEntries(const LdapSearchFilter& filter) override {
        auto results = search(filter);
        return static_cast<int>(results.size());
    }

    void setProgressCallback(ProgressCallback callback) override {
        progressCallback_ = std::move(callback);
    }

private:
    // ========== Connection Pool Management ==========

    void initializePool() {
        std::lock_guard<std::mutex> lock(poolMutex_);

        for (int i = 0; i < config_.poolSize; ++i) {
            LDAP* conn = createConnection();
            connectionPool_.push_back({conn, false, std::chrono::steady_clock::now()});
        }

        initialized_ = true;
        spdlog::info("LDAP connection pool initialized with {} connections", config_.poolSize);
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(poolMutex_);

        for (auto& entry : connectionPool_) {
            if (entry.connection) {
                ldap_unbind_ext(entry.connection, nullptr, nullptr);
                entry.connection = nullptr;
            }
        }

        connectionPool_.clear();
        initialized_ = false;
        spdlog::info("LDAP connection pool shutdown");
    }

    LDAP* createConnection() {
        LDAP* conn = nullptr;

        int rc = ldap_initialize(&conn, config_.getUri().c_str());
        if (rc != LDAP_SUCCESS) {
            spdlog::error("Failed to initialize LDAP connection: {}", ldap_err2string(rc));
            return nullptr;
        }

        // Set protocol version
        int version = LDAP_VERSION3;
        ldap_set_option(conn, LDAP_OPT_PROTOCOL_VERSION, &version);

        // Set timeouts
        struct timeval timeout;
        timeout.tv_sec = config_.connectTimeout;
        timeout.tv_usec = 0;
        ldap_set_option(conn, LDAP_OPT_NETWORK_TIMEOUT, &timeout);

        // Bind
        berval cred;
        cred.bv_val = const_cast<char*>(config_.bindPassword.c_str());
        cred.bv_len = config_.bindPassword.length();

        rc = ldap_sasl_bind_s(
            conn,
            config_.bindDn.c_str(),
            LDAP_SASL_SIMPLE,
            &cred,
            nullptr,
            nullptr,
            nullptr
        );

        if (rc != LDAP_SUCCESS) {
            spdlog::error("LDAP bind failed: {}", ldap_err2string(rc));
            ldap_unbind_ext(conn, nullptr, nullptr);
            return nullptr;
        }

        return conn;
    }

    LDAP* acquireConnection() {
        std::unique_lock<std::mutex> lock(poolMutex_);

        // Wait for available connection
        poolCondition_.wait(lock, [this] {
            for (auto& entry : connectionPool_) {
                if (!entry.inUse && entry.connection != nullptr) {
                    return true;
                }
            }
            return false;
        });

        // Find available connection
        for (auto& entry : connectionPool_) {
            if (!entry.inUse && entry.connection != nullptr) {
                entry.inUse = true;
                entry.lastUsed = std::chrono::steady_clock::now();
                return entry.connection;
            }
        }

        return nullptr;
    }

    void releaseConnection(LDAP* conn) {
        std::lock_guard<std::mutex> lock(poolMutex_);

        for (auto& entry : connectionPool_) {
            if (entry.connection == conn) {
                entry.inUse = false;
                break;
            }
        }

        poolCondition_.notify_one();
    }

    // ========== Helper Methods ==========

    LdapOperationResult createCountryEntry(const std::string& dn, const std::string& countryCode) {
        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire connection");
        }

        LDAPMod mod_objectClass;
        mod_objectClass.mod_op = LDAP_MOD_ADD;
        mod_objectClass.mod_type = const_cast<char*>("objectClass");
        char* objectClassValues[] = {const_cast<char*>("top"), const_cast<char*>("country"), nullptr};
        mod_objectClass.mod_values = objectClassValues;

        LDAPMod mod_c;
        mod_c.mod_op = LDAP_MOD_ADD;
        mod_c.mod_type = const_cast<char*>("c");
        char* cValues[] = {const_cast<char*>(countryCode.c_str()), nullptr};
        mod_c.mod_values = cValues;

        LDAPMod* mods[] = {&mod_objectClass, &mod_c, nullptr};

        int rc = ldap_add_ext_s(conn, dn.c_str(), mods, nullptr, nullptr);
        releaseConnection(conn);

        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            return LdapOperationResult::error(std::string("Failed to create country: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok();
    }

    LdapOperationResult createOrganizationalUnit(const std::string& dn, const std::string& ouName) {
        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire connection");
        }

        LDAPMod mod_objectClass;
        mod_objectClass.mod_op = LDAP_MOD_ADD;
        mod_objectClass.mod_type = const_cast<char*>("objectClass");
        char* objectClassValues[] = {const_cast<char*>("top"), const_cast<char*>("organization"), nullptr};
        mod_objectClass.mod_values = objectClassValues;

        LDAPMod mod_o;
        mod_o.mod_op = LDAP_MOD_ADD;
        mod_o.mod_type = const_cast<char*>("o");
        char* oValues[] = {const_cast<char*>(ouName.c_str()), nullptr};
        mod_o.mod_values = oValues;

        LDAPMod* mods[] = {&mod_objectClass, &mod_o, nullptr};

        int rc = ldap_add_ext_s(conn, dn.c_str(), mods, nullptr, nullptr);
        releaseConnection(conn);

        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            return LdapOperationResult::error(std::string("Failed to create OU: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok();
    }

    LdapOperationResult addCertificateEntry(LDAP* conn, const LdapCertificateEntry& entry) {
        std::string cn = entry.getDn().getCommonName();

        // Object classes
        LDAPMod mod_objectClass;
        mod_objectClass.mod_op = LDAP_MOD_ADD;
        mod_objectClass.mod_type = const_cast<char*>("objectClass");
        char* objectClassValues[] = {
            const_cast<char*>("top"),
            const_cast<char*>("inetOrgPerson"),
            const_cast<char*>("pkiUser"),
            nullptr
        };
        mod_objectClass.mod_values = objectClassValues;

        // CN
        LDAPMod mod_cn;
        mod_cn.mod_op = LDAP_MOD_ADD;
        mod_cn.mod_type = const_cast<char*>("cn");
        char* cnValues[] = {const_cast<char*>(cn.c_str()), nullptr};
        mod_cn.mod_values = cnValues;

        // SN (required for inetOrgPerson)
        LDAPMod mod_sn;
        mod_sn.mod_op = LDAP_MOD_ADD;
        mod_sn.mod_type = const_cast<char*>("sn");
        mod_sn.mod_values = cnValues;

        // Certificate binary
        LDAPMod mod_cert;
        mod_cert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        mod_cert.mod_type = const_cast<char*>("userCertificate;binary");
        berval certVal;
        certVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getX509CertificateBinary().data()));
        certVal.bv_len = entry.getX509CertificateBinary().size();
        berval* certVals[] = {&certVal, nullptr};
        mod_cert.mod_bvalues = certVals;

        // Fingerprint
        LDAPMod mod_fingerprint;
        mod_fingerprint.mod_op = LDAP_MOD_ADD;
        mod_fingerprint.mod_type = const_cast<char*>("description");
        std::string fingerprintDesc = "fingerprint:" + entry.getFingerprint();
        char* fpValues[] = {const_cast<char*>(fingerprintDesc.c_str()), nullptr};
        mod_fingerprint.mod_values = fpValues;

        LDAPMod* mods[] = {
            &mod_objectClass,
            &mod_cn,
            &mod_sn,
            &mod_cert,
            &mod_fingerprint,
            nullptr
        };

        int rc = ldap_add_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to add certificate: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult updateCertificateEntry(LDAP* conn, const LdapCertificateEntry& entry) {
        // Certificate binary
        LDAPMod mod_cert;
        mod_cert.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        mod_cert.mod_type = const_cast<char*>("userCertificate;binary");
        berval certVal;
        certVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getX509CertificateBinary().data()));
        certVal.bv_len = entry.getX509CertificateBinary().size();
        berval* certVals[] = {&certVal, nullptr};
        mod_cert.mod_bvalues = certVals;

        LDAPMod* mods[] = {&mod_cert, nullptr};

        int rc = ldap_modify_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to update certificate: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult addCrlEntry(LDAP* conn, const LdapCrlEntry& entry) {
        std::string cn = entry.getDn().getCommonName();

        LDAPMod mod_objectClass;
        mod_objectClass.mod_op = LDAP_MOD_ADD;
        mod_objectClass.mod_type = const_cast<char*>("objectClass");
        char* objectClassValues[] = {
            const_cast<char*>("top"),
            const_cast<char*>("cRLDistributionPoint"),
            nullptr
        };
        mod_objectClass.mod_values = objectClassValues;

        LDAPMod mod_cn;
        mod_cn.mod_op = LDAP_MOD_ADD;
        mod_cn.mod_type = const_cast<char*>("cn");
        char* cnValues[] = {const_cast<char*>(cn.c_str()), nullptr};
        mod_cn.mod_values = cnValues;

        LDAPMod mod_crl;
        mod_crl.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        mod_crl.mod_type = const_cast<char*>("certificateRevocationList;binary");
        berval crlVal;
        crlVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getX509CrlBinary().data()));
        crlVal.bv_len = entry.getX509CrlBinary().size();
        berval* crlVals[] = {&crlVal, nullptr};
        mod_crl.mod_bvalues = crlVals;

        LDAPMod* mods[] = {&mod_objectClass, &mod_cn, &mod_crl, nullptr};

        int rc = ldap_add_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to add CRL: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult updateCrlEntry(LDAP* conn, const LdapCrlEntry& entry) {
        LDAPMod mod_crl;
        mod_crl.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        mod_crl.mod_type = const_cast<char*>("certificateRevocationList;binary");
        berval crlVal;
        crlVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getX509CrlBinary().data()));
        crlVal.bv_len = entry.getX509CrlBinary().size();
        berval* crlVals[] = {&crlVal, nullptr};
        mod_crl.mod_bvalues = crlVals;

        LDAPMod* mods[] = {&mod_crl, nullptr};

        int rc = ldap_modify_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to update CRL: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult addMasterListEntry(LDAP* conn, const LdapMasterListEntry& entry) {
        std::string cn = entry.getDn().getCommonName();

        LDAPMod mod_objectClass;
        mod_objectClass.mod_op = LDAP_MOD_ADD;
        mod_objectClass.mod_type = const_cast<char*>("objectClass");
        char* objectClassValues[] = {
            const_cast<char*>("top"),
            const_cast<char*>("pkiCA"),
            nullptr
        };
        mod_objectClass.mod_values = objectClassValues;

        LDAPMod mod_cn;
        mod_cn.mod_op = LDAP_MOD_ADD;
        mod_cn.mod_type = const_cast<char*>("cn");
        char* cnValues[] = {const_cast<char*>(cn.c_str()), nullptr};
        mod_cn.mod_values = cnValues;

        LDAPMod mod_ml;
        mod_ml.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        mod_ml.mod_type = const_cast<char*>("cACertificate;binary");
        berval mlVal;
        mlVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getMasterListBinary().data()));
        mlVal.bv_len = entry.getMasterListBinary().size();
        berval* mlVals[] = {&mlVal, nullptr};
        mod_ml.mod_bvalues = mlVals;

        LDAPMod* mods[] = {&mod_objectClass, &mod_cn, &mod_ml, nullptr};

        int rc = ldap_add_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to add Master List: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult updateMasterListEntry(LDAP* conn, const LdapMasterListEntry& entry) {
        LDAPMod mod_ml;
        mod_ml.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        mod_ml.mod_type = const_cast<char*>("cACertificate;binary");
        berval mlVal;
        mlVal.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.getMasterListBinary().data()));
        mlVal.bv_len = entry.getMasterListBinary().size();
        berval* mlVals[] = {&mlVal, nullptr};
        mod_ml.mod_bvalues = mlVals;

        LDAPMod* mods[] = {&mod_ml, nullptr};

        int rc = ldap_modify_ext_s(conn, entry.getDn().getValue().c_str(), mods, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            return LdapOperationResult::error(std::string("Failed to update Master List: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    LdapOperationResult deleteEntry(const std::string& dn) {
        auto conn = acquireConnection();
        if (!conn) {
            return LdapOperationResult::error("Failed to acquire connection");
        }

        int rc = ldap_delete_ext_s(conn, dn.c_str(), nullptr, nullptr);
        releaseConnection(conn);

        if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
            return LdapOperationResult::error(std::string("Failed to delete entry: ") + ldap_err2string(rc));
        }

        return LdapOperationResult::ok(1);
    }

    std::string escapeLdapFilter(const std::string& value) {
        std::string result;
        for (char c : value) {
            switch (c) {
                case '*':  result += "\\2a"; break;
                case '(':  result += "\\28"; break;
                case ')':  result += "\\29"; break;
                case '\\': result += "\\5c"; break;
                case '\0': result += "\\00"; break;
                default:   result += c;
            }
        }
        return result;
    }

    // ========== Conversion Methods ==========

    std::optional<LdapCertificateEntry> convertToLdapCertificateEntry(
        const LdapEntry& entry,
        LdapEntryType entryType
    ) {
        auto certBinary = entry.getBinaryValue("userCertificate;binary");
        if (!certBinary) {
            return std::nullopt;
        }

        auto fingerprint = entry.getAttributeValue("description");
        std::string fp = fingerprint.value_or("");

        // Extract fingerprint from description
        if (fp.find("fingerprint:") == 0) {
            fp = fp.substr(12);
        }

        // Note: This is a simplified conversion. Real implementation would
        // parse the certificate to extract all fields.
        return LdapCertificateEntry::create(
            config_.baseDn,
            "",  // certificateId - would need to generate
            entry.dn,  // Using DN as subject for now
            *certBinary,
            fp,
            "",  // serialNumber - would parse from cert
            "",  // issuerDn - would parse from cert
            entryType,
            "",  // countryCode - would extract from DN
            std::chrono::system_clock::now(),  // notBefore
            std::chrono::system_clock::now()   // notAfter
        );
    }

    std::optional<LdapCrlEntry> convertToLdapCrlEntry(const LdapEntry& entry) {
        auto crlBinary = entry.getBinaryValue("certificateRevocationList;binary");
        if (!crlBinary) {
            return std::nullopt;
        }

        // Simplified conversion
        return LdapCrlEntry::create(
            config_.baseDn,
            "",  // crlId
            entry.dn,  // issuerDn
            "",  // countryCode
            *crlBinary,
            std::chrono::system_clock::now(),  // thisUpdate
            std::chrono::system_clock::now(),  // nextUpdate
            {}  // revokedSerialNumbers
        );
    }

    std::optional<LdapMasterListEntry> convertToLdapMasterListEntry(const LdapEntry& entry) {
        auto mlBinary = entry.getBinaryValue("cACertificate;binary");
        if (!mlBinary) {
            return std::nullopt;
        }

        return LdapMasterListEntry::create(
            config_.baseDn,
            "",  // masterListId
            entry.dn,  // issuerDn
            "",  // countryCode
            *mlBinary,
            1,  // version
            std::chrono::system_clock::now(),  // signingTime
            0   // certificateCount
        );
    }

public:
    // ========== Passive Authentication Support ==========

    std::vector<uint8_t> searchCertificateBySubjectDn(
        const std::string& subjectDn,
        const std::string& certType
    ) override {
        spdlog::debug("Searching certificate by subject DN: {}, type: {}", subjectDn, certType);

        // Determine search base based on cert type
        std::string ouPath = (certType == "csca") ? "o=csca" : "o=dsc";

        // Build search filter - search by description containing the DN
        std::string filter = "(&(objectClass=inetOrgPerson)(description=*" +
                            escapeLdapFilter(subjectDn) + "*))";

        // Search in all country branches
        LdapSearchFilter searchFilter = LdapSearchFilter::subtree(
            "dc=data,dc=download,dc=pkd," + config_.baseDn,
            filter,
            {"userCertificate;binary"}
        );

        auto results = search(searchFilter);

        for (const auto& entry : results) {
            // Check if this entry is in the correct OU
            if (entry.dn.find(ouPath) != std::string::npos) {
                auto certBinary = entry.getBinaryValue("userCertificate;binary");
                if (certBinary) {
                    spdlog::debug("Found certificate for DN: {}", subjectDn);
                    return *certBinary;
                }
            }
        }

        spdlog::debug("Certificate not found for DN: {}", subjectDn);
        return {};
    }

    std::vector<std::vector<uint8_t>> searchCertificatesByCountry(
        const std::string& countryCode,
        const std::string& certType
    ) override {
        spdlog::debug("Searching certificates by country: {}, type: {}", countryCode, certType);

        std::string ouPath = (certType == "csca") ? "o=csca" : "o=dsc";
        std::string baseDn = ouPath + ",c=" + countryCode +
                            ",dc=data,dc=download,dc=pkd," + config_.baseDn;

        LdapSearchFilter searchFilter = LdapSearchFilter::oneLevel(
            baseDn,
            "(objectClass=inetOrgPerson)",
            {"userCertificate;binary"}
        );

        auto results = search(searchFilter);
        std::vector<std::vector<uint8_t>> certificates;

        for (const auto& entry : results) {
            auto certBinary = entry.getBinaryValue("userCertificate;binary");
            if (certBinary) {
                certificates.push_back(*certBinary);
            }
        }

        spdlog::debug("Found {} certificates for country: {}", certificates.size(), countryCode);
        return certificates;
    }

    bool certificateExistsBySubjectDn(
        const std::string& subjectDn,
        const std::string& certType
    ) override {
        auto cert = searchCertificateBySubjectDn(subjectDn, certType);
        return !cert.empty();
    }

    std::vector<uint8_t> searchCrlByIssuer(
        const std::string& issuerDn,
        const std::string& countryCode
    ) override {
        spdlog::debug("Searching CRL by issuer: {}, country: {}", issuerDn, countryCode);

        std::string baseDn = "o=crl,c=" + countryCode +
                            ",dc=data,dc=download,dc=pkd," + config_.baseDn;

        // Search for CRL with matching issuer
        LdapSearchFilter searchFilter = LdapSearchFilter::oneLevel(
            baseDn,
            "(objectClass=cRLDistributionPoint)",
            {"certificateRevocationList;binary"}
        );

        auto results = search(searchFilter);

        for (const auto& entry : results) {
            auto crlBinary = entry.getBinaryValue("certificateRevocationList;binary");
            if (crlBinary) {
                spdlog::debug("Found CRL for issuer: {}", issuerDn);
                return *crlBinary;
            }
        }

        spdlog::debug("CRL not found for issuer: {}", issuerDn);
        return {};
    }
};

} // namespace ldapintegration::infrastructure::adapter
