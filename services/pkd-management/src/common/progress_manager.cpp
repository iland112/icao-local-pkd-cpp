#include "progress_manager.h"
#include "certificate_utils.h"
#include "x509_metadata_extractor.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace common {

// =============================================================================
// Processing Stage Helper Functions
// =============================================================================

std::string stageToString(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return "UPLOAD_COMPLETED";
        case ProcessingStage::PARSING_STARTED: return "PARSING_STARTED";
        case ProcessingStage::PARSING_IN_PROGRESS: return "PARSING_IN_PROGRESS";
        case ProcessingStage::PARSING_COMPLETED: return "PARSING_COMPLETED";
        case ProcessingStage::VALIDATION_STARTED: return "VALIDATION_STARTED";
        case ProcessingStage::VALIDATION_EXTRACTING_METADATA: return "VALIDATION_EXTRACTING_METADATA";
        case ProcessingStage::VALIDATION_VERIFYING_SIGNATURE: return "VALIDATION_VERIFYING_SIGNATURE";
        case ProcessingStage::VALIDATION_CHECKING_TRUST_CHAIN: return "VALIDATION_CHECKING_TRUST_CHAIN";
        case ProcessingStage::VALIDATION_CHECKING_CRL: return "VALIDATION_CHECKING_CRL";
        case ProcessingStage::VALIDATION_CHECKING_ICAO_COMPLIANCE: return "VALIDATION_CHECKING_ICAO_COMPLIANCE";
        case ProcessingStage::VALIDATION_IN_PROGRESS: return "VALIDATION_IN_PROGRESS";
        case ProcessingStage::VALIDATION_COMPLETED: return "VALIDATION_COMPLETED";
        case ProcessingStage::DB_SAVING_STARTED: return "DB_SAVING_STARTED";
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return "DB_SAVING_IN_PROGRESS";
        case ProcessingStage::DB_SAVING_COMPLETED: return "DB_SAVING_COMPLETED";
        case ProcessingStage::LDAP_SAVING_STARTED: return "LDAP_SAVING_STARTED";
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return "LDAP_SAVING_IN_PROGRESS";
        case ProcessingStage::LDAP_SAVING_COMPLETED: return "LDAP_SAVING_COMPLETED";
        case ProcessingStage::COMPLETED: return "COMPLETED";
        case ProcessingStage::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

std::string stageToKorean(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return "파일 업로드 완료";
        case ProcessingStage::PARSING_STARTED: return "파일 파싱 시작";
        case ProcessingStage::PARSING_IN_PROGRESS: return "파일 파싱 중";
        case ProcessingStage::PARSING_COMPLETED: return "파일 파싱 완료";
        case ProcessingStage::VALIDATION_STARTED: return "인증서 검증 시작";
        case ProcessingStage::VALIDATION_EXTRACTING_METADATA: return "인증서 메타데이터 추출 중";
        case ProcessingStage::VALIDATION_VERIFYING_SIGNATURE: return "인증서 서명 검증 중";
        case ProcessingStage::VALIDATION_CHECKING_TRUST_CHAIN: return "신뢰 체인 검증 중";
        case ProcessingStage::VALIDATION_CHECKING_CRL: return "인증서 폐기 목록 확인 중";
        case ProcessingStage::VALIDATION_CHECKING_ICAO_COMPLIANCE: return "ICAO 9303 준수 확인 중";
        case ProcessingStage::VALIDATION_IN_PROGRESS: return "인증서 검증 중";
        case ProcessingStage::VALIDATION_COMPLETED: return "인증서 검증 완료";
        case ProcessingStage::DB_SAVING_STARTED: return "DB 저장 시작";
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return "DB 저장 중";
        case ProcessingStage::DB_SAVING_COMPLETED: return "DB 저장 완료";
        case ProcessingStage::LDAP_SAVING_STARTED: return "LDAP 저장 시작";
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return "LDAP 저장 중";
        case ProcessingStage::LDAP_SAVING_COMPLETED: return "LDAP 저장 완료";
        case ProcessingStage::COMPLETED: return "처리 완료";
        case ProcessingStage::FAILED: return "처리 실패";
        default: return "알 수 없음";
    }
}

int stageToBasePercentage(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return 5;
        case ProcessingStage::PARSING_STARTED: return 10;
        case ProcessingStage::PARSING_IN_PROGRESS: return 30;
        case ProcessingStage::PARSING_COMPLETED: return 50;
        case ProcessingStage::VALIDATION_STARTED: return 52;
        case ProcessingStage::VALIDATION_EXTRACTING_METADATA: return 54;
        case ProcessingStage::VALIDATION_VERIFYING_SIGNATURE: return 58;
        case ProcessingStage::VALIDATION_CHECKING_TRUST_CHAIN: return 62;
        case ProcessingStage::VALIDATION_CHECKING_CRL: return 66;
        case ProcessingStage::VALIDATION_CHECKING_ICAO_COMPLIANCE: return 68;
        case ProcessingStage::VALIDATION_IN_PROGRESS: return 60;
        case ProcessingStage::VALIDATION_COMPLETED: return 70;
        case ProcessingStage::DB_SAVING_STARTED: return 72;
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return 78;
        case ProcessingStage::DB_SAVING_COMPLETED: return 85;
        case ProcessingStage::LDAP_SAVING_STARTED: return 87;
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return 93;
        case ProcessingStage::LDAP_SAVING_COMPLETED: return 100;
        case ProcessingStage::COMPLETED: return 100;
        case ProcessingStage::FAILED: return 0;
        default: return 0;
    }
}

// =============================================================================
// CertificateMetadata Implementation
// =============================================================================

Json::Value CertificateMetadata::toJson() const {
    Json::Value json;

    // Identity
    json["subjectDn"] = subjectDn;
    json["issuerDn"] = issuerDn;
    json["serialNumber"] = serialNumber;
    json["countryCode"] = countryCode;

    // Certificate type
    json["certificateType"] = certificateType;
    json["isSelfSigned"] = isSelfSigned;
    json["isLinkCertificate"] = isLinkCertificate;

    // Cryptographic details
    json["signatureAlgorithm"] = signatureAlgorithm;
    json["publicKeyAlgorithm"] = publicKeyAlgorithm;
    json["keySize"] = keySize;

    // X.509 Extensions
    json["isCa"] = isCa;
    if (pathLengthConstraint.has_value()) {
        json["pathLengthConstraint"] = pathLengthConstraint.value();
    }

    Json::Value keyUsageArray(Json::arrayValue);
    for (const auto& ku : keyUsage) {
        keyUsageArray.append(ku);
    }
    json["keyUsage"] = keyUsageArray;

    Json::Value ekuArray(Json::arrayValue);
    for (const auto& eku : extendedKeyUsage) {
        ekuArray.append(eku);
    }
    json["extendedKeyUsage"] = ekuArray;

    // Validity period
    json["notBefore"] = notBefore;
    json["notAfter"] = notAfter;
    json["isExpired"] = isExpired;

    // Fingerprints
    json["fingerprintSha256"] = fingerprintSha256;
    json["fingerprintSha1"] = fingerprintSha1;

    // ASN.1 Structure (optional)
    if (asn1Text.has_value()) {
        json["asn1Text"] = asn1Text.value();
    }

    return json;
}

// =============================================================================
// IcaoComplianceStatus Implementation
// =============================================================================

Json::Value IcaoComplianceStatus::toJson() const {
    Json::Value json;

    json["isCompliant"] = isCompliant;
    json["complianceLevel"] = complianceLevel;

    Json::Value violationsArray(Json::arrayValue);
    for (const auto& v : violations) {
        violationsArray.append(v);
    }
    json["violations"] = violationsArray;

    if (pkdConformanceCode.has_value()) {
        json["pkdConformanceCode"] = pkdConformanceCode.value();
    }
    if (pkdConformanceText.has_value()) {
        json["pkdConformanceText"] = pkdConformanceText.value();
    }
    if (pkdVersion.has_value()) {
        json["pkdVersion"] = pkdVersion.value();
    }

    // Specific compliance checks
    json["keyUsageCompliant"] = keyUsageCompliant;
    json["algorithmCompliant"] = algorithmCompliant;
    json["keySizeCompliant"] = keySizeCompliant;
    json["validityPeriodCompliant"] = validityPeriodCompliant;
    json["dnFormatCompliant"] = dnFormatCompliant;
    json["extensionsCompliant"] = extensionsCompliant;

    return json;
}

// =============================================================================
// ProcessingError Implementation
// =============================================================================

Json::Value ProcessingError::toJson() const {
    Json::Value json;
    json["timestamp"] = timestamp;
    json["errorType"] = errorType;
    json["entryDn"] = entryDn;
    json["certificateDn"] = certificateDn;
    json["countryCode"] = countryCode;
    json["certificateType"] = certificateType;
    json["message"] = message;
    return json;
}

void addProcessingError(
    ValidationStatistics& stats,
    const std::string& errorType,
    const std::string& entryDn,
    const std::string& certificateDn,
    const std::string& countryCode,
    const std::string& certificateType,
    const std::string& message
) {
    // Increment counters
    stats.totalErrorCount++;

    if (errorType == "BASE64_DECODE_FAILED" || errorType == "CERT_PARSE_FAILED" ||
        errorType == "CRL_PARSE_FAILED" || errorType == "ML_PARSE_FAILED" ||
        errorType == "ML_CERT_PARSE_FAILED") {
        stats.parseErrorCount++;
    } else if (errorType == "DB_SAVE_FAILED" || errorType == "ML_CERT_SAVE_FAILED") {
        stats.dbSaveErrorCount++;
    } else if (errorType == "LDAP_SAVE_FAILED" || errorType == "ML_LDAP_SAVE_FAILED") {
        stats.ldapSaveErrorCount++;
    } else {
        // ENTRY_PROCESSING_EXCEPTION and others count toward parse errors
        stats.parseErrorCount++;
    }

    // Create error record with ISO 8601 timestamp
    ProcessingError error;
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
    error.timestamp = ss.str();
    error.errorType = errorType;
    error.entryDn = entryDn;
    error.certificateDn = certificateDn;
    error.countryCode = countryCode;
    error.certificateType = certificateType;
    error.message = message;

    // Append to bounded list
    if (static_cast<int>(stats.recentErrors.size()) >= ValidationStatistics::MAX_RECENT_ERRORS) {
        stats.recentErrors.erase(stats.recentErrors.begin());
    }
    stats.recentErrors.push_back(std::move(error));
}

// =============================================================================
// ValidationStatistics Implementation
// =============================================================================

Json::Value ValidationStatistics::toJson() const {
    Json::Value json;

    // Overall counts
    json["totalCertificates"] = totalCertificates;
    json["processedCount"] = processedCount;
    json["validCount"] = validCount;
    json["invalidCount"] = invalidCount;
    json["pendingCount"] = pendingCount;

    // Trust chain results
    json["trustChainValidCount"] = trustChainValidCount;
    json["trustChainInvalidCount"] = trustChainInvalidCount;
    json["cscaNotFoundCount"] = cscaNotFoundCount;

    // Expiration status
    json["expiredCount"] = expiredCount;
    json["notYetValidCount"] = notYetValidCount;
    json["validPeriodCount"] = validPeriodCount;

    // CRL status
    json["revokedCount"] = revokedCount;
    json["notRevokedCount"] = notRevokedCount;
    json["crlNotCheckedCount"] = crlNotCheckedCount;

    // Signature algorithm distribution
    Json::Value sigAlgsJson(Json::objectValue);
    for (const auto& [alg, count] : signatureAlgorithms) {
        sigAlgsJson[alg] = count;
    }
    json["signatureAlgorithms"] = sigAlgsJson;

    // Key size distribution
    Json::Value keySizesJson(Json::objectValue);
    for (const auto& [size, count] : keySizes) {
        keySizesJson[std::to_string(size)] = count;
    }
    json["keySizes"] = keySizesJson;

    // Certificate type distribution
    Json::Value certTypesJson(Json::objectValue);
    for (const auto& [type, count] : certificateTypes) {
        certTypesJson[type] = count;
    }
    json["certificateTypes"] = certTypesJson;

    // ICAO compliance summary
    json["icaoCompliantCount"] = icaoCompliantCount;
    json["icaoNonCompliantCount"] = icaoNonCompliantCount;
    json["icaoWarningCount"] = icaoWarningCount;

    Json::Value complianceViolationsJson(Json::objectValue);
    for (const auto& [violation, count] : complianceViolations) {
        complianceViolationsJson[violation] = count;
    }
    json["complianceViolations"] = complianceViolationsJson;

    // Error tracking
    json["totalErrorCount"] = totalErrorCount;
    json["parseErrorCount"] = parseErrorCount;
    json["dbSaveErrorCount"] = dbSaveErrorCount;
    json["ldapSaveErrorCount"] = ldapSaveErrorCount;

    Json::Value errorsArray(Json::arrayValue);
    for (const auto& err : recentErrors) {
        errorsArray.append(err.toJson());
    }
    json["recentErrors"] = errorsArray;

    return json;
}

// =============================================================================
// ProcessingProgress Implementation
// =============================================================================

std::string ProcessingProgress::toJson() const {
    Json::Value json;

    // Basic progress
    json["uploadId"] = uploadId;
    json["stage"] = stageToString(stage);
    json["stageName"] = stageToKorean(stage);
    json["percentage"] = percentage;
    json["processedCount"] = processedCount;
    json["totalCount"] = totalCount;
    json["message"] = message;
    json["errorMessage"] = errorMessage;
    json["details"] = details;

    // Timestamp
    auto time = std::chrono::system_clock::to_time_t(updatedAt);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
    json["updatedAt"] = ss.str();

    // Enhanced fields (Phase 4.4)
    if (currentCertificate.has_value()) {
        json["currentCertificate"] = currentCertificate->toJson();
    }

    if (currentCompliance.has_value()) {
        json["icaoCompliance"] = currentCompliance->toJson();
    }

    if (statistics.has_value()) {
        json["statistics"] = statistics->toJson();
    }

    // Single-line JSON for SSE compatibility
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, json);
}

ProcessingProgress ProcessingProgress::create(
    const std::string& uploadId,
    ProcessingStage stage,
    int processedCount,
    int totalCount,
    const std::string& message,
    const std::string& errorMessage,
    const std::string& details
) {
    ProcessingProgress p;
    p.uploadId = uploadId;
    p.stage = stage;
    p.processedCount = processedCount;
    p.totalCount = totalCount;
    p.message = message;
    p.errorMessage = errorMessage;
    p.details = details;
    p.updatedAt = std::chrono::system_clock::now();

    // Calculate percentage based on stage and progress
    int basePercent = stageToBasePercentage(stage);
    if (totalCount > 0 && processedCount > 0) {
        // Scale within stage range
        int nextPercent = 100;
        if (stage == ProcessingStage::PARSING_IN_PROGRESS) nextPercent = 50;
        else if (stage == ProcessingStage::VALIDATION_IN_PROGRESS) nextPercent = 70;
        else if (stage == ProcessingStage::DB_SAVING_IN_PROGRESS) nextPercent = 85;
        else if (stage == ProcessingStage::LDAP_SAVING_IN_PROGRESS) nextPercent = 100;

        int range = nextPercent - basePercent;
        p.percentage = basePercent + (range * processedCount / totalCount);
    } else {
        p.percentage = basePercent;
    }

    return p;
}

ProcessingProgress ProcessingProgress::createWithMetadata(
    const std::string& uploadId,
    ProcessingStage stage,
    int processedCount,
    int totalCount,
    const std::string& message,
    const CertificateMetadata& certMetadata,
    const std::optional<IcaoComplianceStatus>& compliance,
    const std::optional<ValidationStatistics>& stats
) {
    // Create basic progress
    ProcessingProgress p = create(uploadId, stage, processedCount, totalCount, message);

    // Add metadata
    p.currentCertificate = certMetadata;
    p.currentCompliance = compliance;
    p.statistics = stats;

    return p;
}

// =============================================================================
// ProgressManager Implementation
// =============================================================================

ProgressManager& ProgressManager::getInstance() {
    static ProgressManager instance;
    return instance;
}

void ProgressManager::sendProgress(const ProcessingProgress& progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    progressCache_[progress.uploadId] = progress;

    // Send to SSE callback if registered
    auto it = sseCallbacks_.find(progress.uploadId);
    if (it != sseCallbacks_.end()) {
        try {
            std::string sseData = "event: progress\ndata: " + progress.toJson() + "\n\n";
            it->second(sseData);
            spdlog::info("[SSE] Sent event: {} - {} ({}%) processed={}/{}",
                progress.uploadId.substr(0, 8), stageToString(progress.stage),
                progress.percentage, progress.processedCount, progress.totalCount);
        } catch (const std::exception& e) {
            spdlog::warn("[SSE] Callback failed for {}: {}", progress.uploadId.substr(0, 8), e.what());
            sseCallbacks_.erase(it);
        } catch (...) {
            spdlog::warn("[SSE] Callback failed for {} (unknown error)", progress.uploadId.substr(0, 8));
            sseCallbacks_.erase(it);
        }
    } else {
        spdlog::debug("[SSE] No callback registered for {} - {} ({}%)",
            progress.uploadId.substr(0, 8), stageToString(progress.stage), progress.percentage);
    }

    spdlog::debug("Progress: {} - {} ({}%)", progress.uploadId, stageToString(progress.stage), progress.percentage);
}

void ProgressManager::registerSseCallback(const std::string& uploadId, std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    sseCallbacks_[uploadId] = callback;
    spdlog::info("[SSE] Callback registered for upload: {}", uploadId.substr(0, 8));

    // Send cached progress if available
    auto it = progressCache_.find(uploadId);
    if (it != progressCache_.end()) {
        std::string sseData = "event: progress\ndata: " + it->second.toJson() + "\n\n";
        callback(sseData);
        spdlog::info("[SSE] Sent cached progress: {} - {} ({}%)",
            uploadId.substr(0, 8), stageToString(it->second.stage), it->second.percentage);
    } else {
        spdlog::info("[SSE] No cached progress for {}", uploadId.substr(0, 8));
    }
}

void ProgressManager::unregisterSseCallback(const std::string& uploadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    sseCallbacks_.erase(uploadId);
}

std::optional<ProcessingProgress> ProgressManager::getProgress(const std::string& uploadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = progressCache_.find(uploadId);
    if (it != progressCache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ProgressManager::clearProgress(const std::string& uploadId) {
    std::lock_guard<std::mutex> lock(mutex_);
    progressCache_.erase(uploadId);
    sseCallbacks_.erase(uploadId);
}

// =============================================================================
// ICAO 9303 Compliance Checker Implementation
// =============================================================================

IcaoComplianceStatus checkIcaoCompliance(X509* cert, const std::string& certType) {
    IcaoComplianceStatus status;

    // Initialize all fields
    status.isCompliant = true;  // Will be set to false if any check fails
    status.complianceLevel = "CONFORMANT";
    status.keyUsageCompliant = true;
    status.algorithmCompliant = true;
    status.keySizeCompliant = true;
    status.validityPeriodCompliant = true;
    status.dnFormatCompliant = true;
    status.extensionsCompliant = true;

    if (!cert) {
        status.isCompliant = false;
        status.complianceLevel = "ERROR";
        status.violations.push_back("NULL certificate pointer");
        return status;
    }

    // Extract metadata using existing extractor
    x509::CertificateMetadata metadata = x509::extractMetadata(cert);

    // =============================================================================
    // 1. Key Usage Validation
    // =============================================================================

    std::vector<std::string> requiredKeyUsage;
    std::vector<std::string> forbiddenKeyUsage;

    if (certType == "CSCA") {
        requiredKeyUsage = {"keyCertSign", "cRLSign"};
        // CSCA must be CA
        if (!metadata.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("CSCA must have CA=TRUE");
        }
    } else if (certType == "DSC" || certType == "DSC_NC") {
        requiredKeyUsage = {"digitalSignature"};
        // DSC must NOT be CA
        if (metadata.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("DSC must have CA=FALSE");
        }
    } else if (certType == "MLSC") {
        requiredKeyUsage = {"keyCertSign"};
        // MLSC must be CA and self-signed
        if (!metadata.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("MLSC must have CA=TRUE");
        }
        if (!metadata.isSelfSigned) {
            status.keyUsageCompliant = false;
            status.violations.push_back("MLSC must be self-signed");
        }
    }

    // Check if all required key usages are present
    for (const auto& required : requiredKeyUsage) {
        if (std::find(metadata.keyUsage.begin(), metadata.keyUsage.end(), required) == metadata.keyUsage.end()) {
            status.keyUsageCompliant = false;
            status.violations.push_back("Missing required Key Usage: " + required);
        }
    }

    // =============================================================================
    // 2. Signature Algorithm Validation
    // =============================================================================

    std::string sigAlg = metadata.signatureAlgorithm;
    std::string hashAlg = metadata.signatureHashAlgorithm;

    // Convert to lowercase for comparison
    std::transform(sigAlg.begin(), sigAlg.end(), sigAlg.begin(), ::tolower);
    std::transform(hashAlg.begin(), hashAlg.end(), hashAlg.begin(), ::tolower);

    // ICAO approved hash algorithms: SHA-256, SHA-384, SHA-512
    bool approvedHash = (hashAlg.find("sha-256") != std::string::npos ||
                         hashAlg.find("sha-384") != std::string::npos ||
                         hashAlg.find("sha-512") != std::string::npos ||
                         hashAlg.find("sha256") != std::string::npos ||
                         hashAlg.find("sha384") != std::string::npos ||
                         hashAlg.find("sha512") != std::string::npos);

    // ICAO approved public key algorithms: RSA, ECDSA
    std::string pubKeyAlg = metadata.publicKeyAlgorithm;
    bool approvedPubKey = (pubKeyAlg == "RSA" || pubKeyAlg == "ECDSA");

    if (!approvedHash) {
        status.algorithmCompliant = false;
        status.violations.push_back("Signature hash algorithm not ICAO-approved (must be SHA-256/384/512): " + hashAlg);
    }

    if (!approvedPubKey) {
        status.algorithmCompliant = false;
        status.violations.push_back("Public key algorithm not ICAO-approved (must be RSA or ECDSA): " + pubKeyAlg);
    }

    // =============================================================================
    // 3. Key Size Validation
    // =============================================================================

    int keySize = metadata.publicKeySize;

    if (pubKeyAlg == "RSA") {
        // ICAO recommends RSA 2048-4096 bits
        if (keySize < 2048) {
            status.keySizeCompliant = false;
            status.violations.push_back("RSA key size below minimum (2048 bits): " + std::to_string(keySize) + " bits");
        } else if (keySize > 4096) {
            // Warning only, not a hard failure
            status.complianceLevel = "WARNING";
            status.violations.push_back("RSA key size exceeds recommended maximum (4096 bits): " + std::to_string(keySize) + " bits");
        }
    } else if (pubKeyAlg == "ECDSA") {
        // ICAO approved curves: P-256 (224 bits), P-384 (384 bits), P-521 (521 bits)
        if (metadata.publicKeyCurve.has_value()) {
            std::string curve = metadata.publicKeyCurve.value();
            bool approvedCurve = (curve == "prime256v1" || curve == "secp256r1" ||  // P-256
                                  curve == "secp384r1" ||                            // P-384
                                  curve == "secp521r1");                             // P-521

            if (!approvedCurve) {
                status.keySizeCompliant = false;
                status.violations.push_back("ECDSA curve not ICAO-approved (must be P-256/384/521): " + curve);
            }
        } else {
            if (keySize < 224) {
                status.keySizeCompliant = false;
                status.violations.push_back("ECDSA key size below minimum (224 bits): " + std::to_string(keySize) + " bits");
            }
        }
    }

    // =============================================================================
    // 4. Validity Period Validation
    // =============================================================================

    // Extract notBefore and notAfter times
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);

    if (notBefore && notAfter) {
        // Calculate validity period in days
        int days = 0;
        int secs = 0;
        if (ASN1_TIME_diff(&days, &secs, notBefore, notAfter)) {
            int validityYears = days / 365;

            // ICAO recommendations:
            // - CSCA: maximum 15 years
            // - DSC: maximum 3 years (often less for security)
            if (certType == "CSCA" && validityYears > 15) {
                status.validityPeriodCompliant = false;
                status.violations.push_back("CSCA validity period exceeds ICAO recommendation (15 years): " + std::to_string(validityYears) + " years");
            } else if ((certType == "DSC" || certType == "DSC_NC") && validityYears > 3) {
                // This is a warning, not a hard failure (some countries use longer periods)
                status.complianceLevel = "WARNING";
                status.violations.push_back("DSC validity period exceeds ICAO recommendation (3 years): " + std::to_string(validityYears) + " years");
            }
        }
    }

    // =============================================================================
    // 5. DN Format Validation
    // =============================================================================

    // ICAO requires C (Country) attribute in Subject DN
    X509_NAME* subject = X509_get_subject_name(cert);
    if (subject) {
        int cnid = X509_NAME_get_index_by_NID(subject, NID_countryName, -1);
        if (cnid < 0) {
            status.dnFormatCompliant = false;
            status.violations.push_back("Subject DN missing required Country (C) attribute");
        }
    } else {
        status.dnFormatCompliant = false;
        status.violations.push_back("Certificate has no Subject DN");
    }

    // =============================================================================
    // 6. Required Extensions Validation
    // =============================================================================

    // Basic Constraints extension is CRITICAL for CA certificates
    if (certType == "CSCA" || certType == "MLSC") {
        BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(
            cert, NID_basic_constraints, nullptr, nullptr);

        if (!bc) {
            status.extensionsCompliant = false;
            status.violations.push_back(certType + " missing required Basic Constraints extension");
        } else {
            BASIC_CONSTRAINTS_free(bc);
        }
    }

    // Key Usage extension should be present
    if (metadata.keyUsage.empty()) {
        status.extensionsCompliant = false;
        status.violations.push_back("Missing Key Usage extension");
    }

    // =============================================================================
    // Final Compliance Assessment
    // =============================================================================

    if (!status.keyUsageCompliant || !status.algorithmCompliant ||
        !status.keySizeCompliant || !status.dnFormatCompliant ||
        !status.extensionsCompliant) {
        status.isCompliant = false;
        status.complianceLevel = "NON_CONFORMANT";
    } else if (!status.validityPeriodCompliant) {
        // Validity period issues are warnings only
        status.isCompliant = true;
        if (status.complianceLevel != "WARNING") {
            status.complianceLevel = "WARNING";
        }
    }

    // Set PKD conformance code if non-compliant
    if (!status.isCompliant) {
        if (!status.keyUsageCompliant) {
            status.pkdConformanceCode = "ERR:" + certType + ".KEY_USAGE";
            status.pkdConformanceText = "Key Usage does not meet ICAO 9303 requirements for " + certType;
        } else if (!status.algorithmCompliant) {
            status.pkdConformanceCode = "ERR:" + certType + ".ALGORITHM";
            status.pkdConformanceText = "Signature algorithm not approved by ICAO 9303";
        } else if (!status.keySizeCompliant) {
            status.pkdConformanceCode = "ERR:" + certType + ".KEY_SIZE";
            status.pkdConformanceText = "Key size does not meet ICAO 9303 minimum requirements";
        } else if (!status.dnFormatCompliant) {
            status.pkdConformanceCode = "ERR:" + certType + ".DN_FORMAT";
            status.pkdConformanceText = "Distinguished Name format does not comply with ICAO 9303";
        } else if (!status.extensionsCompliant) {
            status.pkdConformanceCode = "ERR:" + certType + ".EXTENSIONS";
            status.pkdConformanceText = "Missing required X.509 extensions per ICAO 9303";
        }
    }

    return status;
}

// =============================================================================
// Certificate Metadata Extraction for Progress Tracking
// =============================================================================

CertificateMetadata extractCertificateMetadataForProgress(X509* cert, bool includeAsn1Text)
{
    CertificateMetadata metadata;

    if (!cert) {
        spdlog::warn("[ProgressManager] extractCertificateMetadataForProgress: NULL certificate pointer");
        return metadata;
    }

    try {
        // === Identity ===
        X509_NAME* subject = X509_get_subject_name(cert);
        X509_NAME* issuer = X509_get_issuer_name(cert);

        metadata.subjectDn = ::certificate_utils::x509NameToString(subject);
        metadata.issuerDn = ::certificate_utils::x509NameToString(issuer);
        metadata.serialNumber = ::certificate_utils::asn1IntegerToHex(X509_get_serialNumber(cert));
        metadata.countryCode = ::certificate_utils::extractCountryCode(metadata.subjectDn);

        // === Extract detailed X.509 metadata using x509_metadata_extractor ===
        ::x509::CertificateMetadata x509Meta = ::x509::extractMetadata(cert);

        // === Certificate Type Determination ===
        // Determine certificate type based on Key Usage and Basic Constraints
        metadata.isSelfSigned = x509Meta.isSelfSigned;
        metadata.isLinkCertificate = ::certificate_utils::isLinkCertificate(cert);
        metadata.isCa = x509Meta.isCA;
        metadata.pathLengthConstraint = x509Meta.pathLenConstraint;

        // Heuristic certificate type detection
        if (x509Meta.isSelfSigned && x509Meta.isCA) {
            // Self-signed CA certificate
            bool hasKeyCertSign = std::find(x509Meta.keyUsage.begin(), x509Meta.keyUsage.end(), "keyCertSign") != x509Meta.keyUsage.end();
            bool hasCrlSign = std::find(x509Meta.keyUsage.begin(), x509Meta.keyUsage.end(), "cRLSign") != x509Meta.keyUsage.end();

            if (hasKeyCertSign && hasCrlSign) {
                metadata.certificateType = "CSCA";  // Country Signing CA
            } else if (hasKeyCertSign) {
                metadata.certificateType = "MLSC";  // Master List Signer Certificate
            } else {
                metadata.certificateType = "CSCA";  // Default to CSCA for self-signed CAs
            }
        } else if (metadata.isLinkCertificate) {
            metadata.certificateType = "CSCA";  // Link certificates are treated as CSCA
        } else if (!x509Meta.isCA) {
            // End-entity certificate (not a CA)
            bool hasDigitalSignature = std::find(x509Meta.keyUsage.begin(), x509Meta.keyUsage.end(), "digitalSignature") != x509Meta.keyUsage.end();

            if (hasDigitalSignature) {
                metadata.certificateType = "DSC";  // Document Signer Certificate
            } else {
                metadata.certificateType = "UNKNOWN";
            }
        } else {
            // CA certificate but not self-signed (intermediate CA / link cert)
            metadata.certificateType = "CSCA";
        }

        // === Cryptographic Details ===
        metadata.signatureAlgorithm = x509Meta.signatureAlgorithm;
        metadata.publicKeyAlgorithm = x509Meta.publicKeyAlgorithm;
        metadata.keySize = x509Meta.publicKeySize;

        // === X.509 Extensions ===
        metadata.keyUsage = x509Meta.keyUsage;
        metadata.extendedKeyUsage = x509Meta.extendedKeyUsage;

        // === Validity Period ===
        const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
        const ASN1_TIME* notAfter = X509_get0_notAfter(cert);

        metadata.notBefore = certificate_utils::asn1TimeToIso8601(notBefore);
        metadata.notAfter = certificate_utils::asn1TimeToIso8601(notAfter);
        metadata.isExpired = certificate_utils::isExpired(cert);

        // === Fingerprints ===
        metadata.fingerprintSha256 = certificate_utils::computeSha256Fingerprint(cert);
        metadata.fingerprintSha1 = certificate_utils::computeSha1Fingerprint(cert);

        // === Optional ASN.1 Structure (for detailed view) ===
        if (includeAsn1Text) {
            try {
                metadata.asn1Text = certificate_utils::extractAsn1Text(cert);
            } catch (const std::exception& e) {
                spdlog::warn("[ProgressManager] Failed to extract ASN.1 text: {}", e.what());
                metadata.asn1Text = std::nullopt;
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("[ProgressManager] extractCertificateMetadataForProgress failed: {}", e.what());
    }

    return metadata;
}

} // namespace common
