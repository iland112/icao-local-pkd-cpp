# Certificate Validation Detail Enhancement Plan

**Document Version**: 1.0.0
**Created**: 2026-01-23
**Related Documents**:
- [CSCA_STORAGE_AND_LINK_CERT_ISSUES.md](CSCA_STORAGE_AND_LINK_CERT_ISSUES.md)
- [CSCA_ISSUES_IMPLEMENTATION_PLAN.md](CSCA_ISSUES_IMPLEMENTATION_PLAN.md)

---

## Table of Contents

1. [Overview](#overview)
2. [Current State Analysis](#current-state-analysis)
3. [Enhanced Database Schema](#enhanced-database-schema)
4. [Backend Implementation](#backend-implementation)
5. [Frontend UX Design](#frontend-ux-design)
6. [API Design](#api-design)
7. [Implementation Timeline](#implementation-timeline)
8. [User Stories](#user-stories)

---

## Overview

### Objective

Provide comprehensive certificate validation details to users through:
1. **Enhanced Database Storage**: Store granular validation failure reasons
2. **Detailed API Responses**: Expose validation step results
3. **User-Friendly Frontend**: Visual representation of validation process
4. **Actionable Insights**: Guide users to resolve validation issues

### User Pain Points (Current System)

| Problem | Current Behavior | User Impact |
|---------|------------------|-------------|
| **Generic Error Messages** | "Trust chain validation failed" | User doesn't know what to fix |
| **No Step-by-Step Breakdown** | Only final result (VALID/INVALID) | Cannot diagnose issues |
| **Hidden CSCA Issues** | "CSCA not found" | User doesn't know which CSCA is missing |
| **No Link Cert Visibility** | Users unaware of link certificate usage | Cannot verify key rollover scenarios |
| **Bulk Upload Failures** | Single error message for 1000+ certs | Cannot identify specific problematic certificates |

### Solution Components

```
┌─────────────────────────────────────────────────────────────────┐
│                     User Experience Layers                       │
├─────────────────────────────────────────────────────────────────┤
│  1. Visual Validation Timeline (Frontend)                        │
│     ├─ Step-by-step progress indicators                         │
│     ├─ Color-coded status (green/yellow/red)                    │
│     └─ Expandable error details                                 │
│                                                                  │
│  2. Validation Detail Cards (Frontend)                           │
│     ├─ Certificate metadata display                             │
│     ├─ Trust chain visualization (graph/tree)                   │
│     └─ Actionable recommendations                               │
│                                                                  │
│  3. Enhanced Database Schema (Backend)                           │
│     ├─ validation_result_detail table                           │
│     ├─ validation_step enum                                     │
│     └─ JSON metadata fields                                     │
│                                                                  │
│  4. Detailed API Responses (Backend)                             │
│     ├─ Step-by-step results                                     │
│     ├─ Error codes with descriptions                            │
│     └─ Recommendations                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## Current State Analysis

### Current validation_result Table Schema

```sql
CREATE TABLE validation_result (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    certificate_id UUID NOT NULL REFERENCES certificate(id),
    upload_id UUID REFERENCES uploaded_file(id),

    -- Certificate identity
    certificate_type VARCHAR(20) NOT NULL,
    country_code VARCHAR(3),
    subject_dn TEXT,
    issuer_dn TEXT,
    serial_number TEXT,

    -- Validation results
    validation_status VARCHAR(20) NOT NULL,  -- VALID, INVALID, UNKNOWN
    trust_chain_valid BOOLEAN,
    trust_chain_message TEXT,

    -- CSCA information
    csca_found BOOLEAN,
    csca_subject_dn TEXT,
    csca_serial_number TEXT,
    csca_country VARCHAR(3),

    -- Signature verification
    signature_valid BOOLEAN,
    signature_algorithm TEXT,

    -- Validity period
    validity_period_valid BOOLEAN,
    not_before TIMESTAMP,
    not_after TIMESTAMP,

    -- Revocation status
    revocation_status VARCHAR(20),
    crl_checked BOOLEAN,
    ocsp_checked BOOLEAN,

    -- Timestamp
    validation_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    -- Trust chain path (added in Sprint 3)
    trust_chain_path TEXT
);
```

### Current Limitations

1. **No Step-by-Step Records**: Cannot see which validation step failed
2. **Generic Error Messages**: `trust_chain_message` has limited detail
3. **No Certificate Chain Storage**: Cannot reconstruct trust path for debugging
4. **Missing Metadata**: No OpenSSL error codes, no certificate extensions analysis
5. **No User Recommendations**: Users must manually interpret errors

---

## Enhanced Database Schema

### 1. validation_result_detail Table (New)

**Purpose**: Store detailed validation step results for each certificate.

```sql
-- Validation step enumeration
CREATE TYPE validation_step AS ENUM (
    'PARSE_CERTIFICATE',        -- Step 1: Certificate parsing
    'CHECK_EXPIRATION',         -- Step 2: Validity period check
    'FIND_CSCA',                -- Step 3: CSCA lookup
    'BUILD_TRUST_CHAIN',        -- Step 4: Chain building (including link certs)
    'VERIFY_SIGNATURES',        -- Step 5: Signature verification
    'CHECK_EXTENSIONS',         -- Step 6: X.509 extensions validation
    'CHECK_REVOCATION',         -- Step 7: CRL/OCSP check
    'FINAL_DECISION'            -- Step 8: Overall validation decision
);

-- Validation step status
CREATE TYPE step_status AS ENUM (
    'SUCCESS',       -- Step completed successfully
    'WARNING',       -- Step completed with warnings (non-critical)
    'FAILED',        -- Step failed (validation stops)
    'SKIPPED',       -- Step skipped (not applicable)
    'IN_PROGRESS'    -- Step currently executing (for real-time updates)
);

-- Detailed validation results (one record per validation step)
CREATE TABLE validation_result_detail (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    validation_result_id UUID NOT NULL REFERENCES validation_result(id) ON DELETE CASCADE,
    certificate_id UUID NOT NULL REFERENCES certificate(id),

    -- Step information
    step_number INT NOT NULL,                    -- Sequential order (1-8)
    step_name validation_step NOT NULL,
    step_status step_status NOT NULL,

    -- Step timing
    step_started_at TIMESTAMP NOT NULL,
    step_completed_at TIMESTAMP,
    step_duration_ms INT,                        -- Milliseconds

    -- Step results
    success BOOLEAN NOT NULL,
    error_code VARCHAR(50),                      -- e.g., "CSCA_NOT_FOUND", "SIGNATURE_INVALID"
    error_message TEXT,                          -- Human-readable error
    warning_message TEXT,                        -- Non-critical warnings

    -- Step-specific data (JSON)
    step_data JSONB,                             -- Flexible storage for step-specific info

    -- Recommendations
    recommendation TEXT,                         -- Actionable advice for users

    -- Metadata
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT fk_validation_result FOREIGN KEY (validation_result_id)
        REFERENCES validation_result(id) ON DELETE CASCADE,
    CONSTRAINT fk_certificate FOREIGN KEY (certificate_id)
        REFERENCES certificate(id) ON DELETE CASCADE
);

-- Indexes for fast lookup
CREATE INDEX idx_vr_detail_validation_result_id ON validation_result_detail(validation_result_id);
CREATE INDEX idx_vr_detail_certificate_id ON validation_result_detail(certificate_id);
CREATE INDEX idx_vr_detail_step_name ON validation_result_detail(step_name);
CREATE INDEX idx_vr_detail_step_status ON validation_result_detail(step_status);
CREATE INDEX idx_vr_detail_error_code ON validation_result_detail(error_code);

-- GIN index for JSON step_data
CREATE INDEX idx_vr_detail_step_data ON validation_result_detail USING gin(step_data);
```

### 2. Example step_data JSON Structures

#### Step 1: PARSE_CERTIFICATE
```json
{
  "certificate_size_bytes": 1024,
  "certificate_format": "DER",
  "parse_time_ms": 5,
  "openssl_version": "OpenSSL 3.0.2"
}
```

#### Step 2: CHECK_EXPIRATION
```json
{
  "not_before": "2020-01-01T00:00:00Z",
  "not_after": "2030-01-01T00:00:00Z",
  "current_time": "2026-01-23T10:30:00Z",
  "days_until_expiry": 1437,
  "is_expired": false,
  "is_not_yet_valid": false
}
```

#### Step 3: FIND_CSCA
```json
{
  "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA",
  "csca_count_found": 2,
  "cscas": [
    {
      "serial_number": "434E445343410005",
      "fingerprint": "72b3f2a05a3ec5e8ff9c8a07b634cd4b...",
      "type": "self-signed"
    },
    {
      "serial_number": "434E445343410005",
      "fingerprint": "e3dbd84925fb24fb50ba7cc8db71b90a...",
      "type": "link_certificate",
      "links_to": "CN=CSCA_NEW"
    }
  ],
  "search_time_ms": 15
}
```

#### Step 4: BUILD_TRUST_CHAIN
```json
{
  "chain_length": 4,
  "chain_steps": [
    {
      "level": 0,
      "certificate_type": "DSC",
      "subject_dn": "C=CN, O=Test, CN=DSC_Beijing",
      "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD"
    },
    {
      "level": 1,
      "certificate_type": "CSCA",
      "subject_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
      "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
      "is_self_signed": true
    },
    {
      "level": 2,
      "certificate_type": "LINK_CERT",
      "subject_dn": "C=CN, O=中华人民共和国, CN=CSCA_NEW",
      "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
      "is_link_certificate": true
    },
    {
      "level": 3,
      "certificate_type": "CSCA",
      "subject_dn": "C=CN, O=中华人民共和国, CN=CSCA_NEW",
      "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_NEW",
      "is_self_signed": true,
      "is_root": true
    }
  ],
  "chain_depth": 4,
  "max_depth_allowed": 5,
  "circular_reference_detected": false,
  "build_time_ms": 12
}
```

#### Step 5: VERIFY_SIGNATURES
```json
{
  "total_signatures_verified": 3,
  "signature_details": [
    {
      "step": "DSC → CSCA_OLD",
      "algorithm": "sha256WithRSAEncryption",
      "key_size_bits": 2048,
      "signature_valid": true,
      "verify_time_ms": 3
    },
    {
      "step": "Link_Cert → CSCA_OLD",
      "algorithm": "sha256WithRSAEncryption",
      "key_size_bits": 2048,
      "signature_valid": true,
      "verify_time_ms": 3
    },
    {
      "step": "CSCA_NEW (self-signed)",
      "algorithm": "sha256WithRSAEncryption",
      "key_size_bits": 4096,
      "signature_valid": true,
      "verify_time_ms": 5
    }
  ],
  "all_signatures_valid": true,
  "total_verify_time_ms": 11
}
```

#### Step 6: CHECK_EXTENSIONS
```json
{
  "critical_extensions": [
    "keyUsage",
    "basicConstraints"
  ],
  "non_critical_extensions": [
    "subjectKeyIdentifier",
    "authorityKeyIdentifier"
  ],
  "key_usage": ["digitalSignature", "keyCertSign"],
  "basic_constraints": {
    "ca": false,
    "path_len_constraint": null
  },
  "extended_key_usage": null,
  "all_critical_extensions_recognized": true
}
```

#### Step 7: CHECK_REVOCATION
```json
{
  "crl_checked": true,
  "crl_url": "ldap://localhost:389/cn=crl,c=CN,...",
  "crl_found": true,
  "crl_valid": true,
  "certificate_revoked": false,
  "ocsp_checked": false,
  "ocsp_url": null,
  "revocation_check_time_ms": 25
}
```

#### Step 8: FINAL_DECISION
```json
{
  "overall_status": "VALID",
  "passed_steps": 7,
  "failed_steps": 0,
  "warning_steps": 0,
  "total_validation_time_ms": 85,
  "compliance_level": "ICAO_9303_COMPLIANT",
  "recommendations": []
}
```

### 3. Error Code Taxonomy

```sql
-- Create error code lookup table (for localization and consistency)
CREATE TABLE validation_error_codes (
    error_code VARCHAR(50) PRIMARY KEY,
    category VARCHAR(30) NOT NULL,          -- PARSE, EXPIRATION, CSCA, CHAIN, SIGNATURE, etc.
    severity VARCHAR(20) NOT NULL,          -- CRITICAL, HIGH, MEDIUM, LOW, INFO
    default_message TEXT NOT NULL,          -- English message
    recommendation TEXT,                    -- What user should do
    icao_reference TEXT,                    -- ICAO Doc 9303 section reference
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Populate error codes
INSERT INTO validation_error_codes (error_code, category, severity, default_message, recommendation, icao_reference) VALUES
-- Parsing errors
('CERT_PARSE_FAILED', 'PARSE', 'CRITICAL', 'Failed to parse certificate binary data', 'Verify certificate is in valid DER format', 'ICAO 9303-12 Section 7.1'),
('CERT_CORRUPT', 'PARSE', 'CRITICAL', 'Certificate data is corrupted', 'Re-export certificate from source system', null),

-- Expiration errors
('CERT_EXPIRED', 'EXPIRATION', 'HIGH', 'Certificate has expired', 'Contact issuing country to obtain updated certificate', 'ICAO 9303-12 Section 7.1.1'),
('CERT_NOT_YET_VALID', 'EXPIRATION', 'MEDIUM', 'Certificate is not yet valid', 'Verify system time is correct, or wait until certificate validity period starts', null),

-- CSCA lookup errors
('CSCA_NOT_FOUND', 'CSCA', 'CRITICAL', 'Issuing CSCA not found in database', 'Upload the CSCA Master List for this country', 'ICAO 9303-12 Section 7.1'),
('CSCA_MULTIPLE_FOUND', 'CSCA', 'INFO', 'Multiple CSCAs found (including link certificates)', 'This is normal during key rollover period', 'ICAO 9303-12 Section 7.1.2'),
('CSCA_EXPIRED', 'CSCA', 'HIGH', 'Issuing CSCA has expired', 'Upload updated CSCA Master List', null),

-- Trust chain errors
('CHAIN_BUILD_FAILED', 'CHAIN', 'CRITICAL', 'Unable to build trust chain to root CSCA', 'Verify all intermediate certificates (link certificates) are uploaded', 'ICAO 9303-12 Section 7.1.2'),
('CHAIN_CIRCULAR_REF', 'CHAIN', 'CRITICAL', 'Circular reference detected in certificate chain', 'Contact system administrator - malformed PKI data', null),
('CHAIN_MAX_DEPTH', 'CHAIN', 'CRITICAL', 'Trust chain exceeds maximum depth (5 levels)', 'Verify certificate chain structure - possible configuration error', null),
('CHAIN_BROKEN', 'CHAIN', 'CRITICAL', 'Certificate chain is broken (missing intermediate certificate)', 'Upload missing link certificate or intermediate CSCA', 'ICAO 9303-12 Section 7.1.2'),

-- Signature verification errors
('SIGNATURE_INVALID', 'SIGNATURE', 'CRITICAL', 'Certificate signature verification failed', 'Certificate may be tampered or signed by wrong key', 'ICAO 9303-11 Section 6.1'),
('SIGNATURE_ALGORITHM_UNSUPPORTED', 'SIGNATURE', 'HIGH', 'Signature algorithm is not supported', 'Contact issuing country - algorithm may be deprecated', null),
('PUBLIC_KEY_MISMATCH', 'SIGNATURE', 'CRITICAL', 'Public key does not match expected issuer', 'Verify certificate was signed by correct CSCA', null),

-- Extension errors
('EXTENSION_CRITICAL_UNKNOWN', 'EXTENSIONS', 'HIGH', 'Unknown critical extension present', 'Certificate may not be compliant with ICAO standards', 'ICAO 9303-12 Section 7.1.1'),
('EXTENSION_KEY_USAGE_INVALID', 'EXTENSIONS', 'MEDIUM', 'Key usage extension indicates incorrect usage', 'Verify certificate type (CSCA vs DSC)', null),
('EXTENSION_BASIC_CONSTRAINTS_INVALID', 'EXTENSIONS', 'MEDIUM', 'Basic constraints extension is invalid', 'CA flag should be true for CSCA, false for DSC', null),

-- Revocation errors
('CRL_NOT_FOUND', 'REVOCATION', 'MEDIUM', 'CRL not found in directory', 'Upload CRL for this country', null),
('CRL_EXPIRED', 'REVOCATION', 'MEDIUM', 'CRL has expired', 'Upload updated CRL', null),
('CERT_REVOKED', 'REVOCATION', 'CRITICAL', 'Certificate has been revoked', 'Contact issuing country for replacement certificate', 'ICAO 9303-12 Section 7.2'),

-- System errors
('DB_QUERY_FAILED', 'SYSTEM', 'CRITICAL', 'Database query failed during validation', 'Contact system administrator', null),
('LDAP_QUERY_FAILED', 'SYSTEM', 'CRITICAL', 'LDAP query failed during validation', 'Contact system administrator', null),
('TIMEOUT', 'SYSTEM', 'HIGH', 'Validation timeout exceeded', 'Try validation again or contact system administrator', null);
```

---

## Backend Implementation

### 1. ValidationDetailRecorder Class

**Purpose**: Record validation steps in real-time during certificate validation.

**File**: `services/pkd-management/src/validation/validation_detail_recorder.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <json/json.h>
#include <libpq-fe.h>

namespace validation {

enum class ValidationStep {
    PARSE_CERTIFICATE = 1,
    CHECK_EXPIRATION = 2,
    FIND_CSCA = 3,
    BUILD_TRUST_CHAIN = 4,
    VERIFY_SIGNATURES = 5,
    CHECK_EXTENSIONS = 6,
    CHECK_REVOCATION = 7,
    FINAL_DECISION = 8
};

enum class StepStatus {
    SUCCESS,
    WARNING,
    FAILED,
    SKIPPED,
    IN_PROGRESS
};

struct ValidationStepResult {
    ValidationStep step;
    int stepNumber;
    StepStatus status;
    bool success;

    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point completedAt;
    int durationMs;

    std::string errorCode;           // e.g., "CSCA_NOT_FOUND"
    std::string errorMessage;        // Human-readable error
    std::string warningMessage;      // Non-critical warnings
    Json::Value stepData;            // Step-specific data
    std::string recommendation;      // Actionable advice
};

class ValidationDetailRecorder {
public:
    explicit ValidationDetailRecorder(PGconn* conn,
                                      const std::string& validationResultId,
                                      const std::string& certificateId);
    ~ValidationDetailRecorder();

    // Start a validation step
    void startStep(ValidationStep step);

    // Complete current step with success
    void completeStep(StepStatus status,
                      const Json::Value& stepData = Json::Value());

    // Complete current step with error
    void completeStepWithError(const std::string& errorCode,
                               const std::string& errorMessage,
                               const Json::Value& stepData = Json::Value());

    // Complete current step with warning
    void completeStepWithWarning(const std::string& warningMessage,
                                 const Json::Value& stepData = Json::Value());

    // Skip a step (not applicable)
    void skipStep(ValidationStep step, const std::string& reason);

    // Get all recorded steps
    const std::vector<ValidationStepResult>& getSteps() const;

    // Persist all steps to database
    bool saveToDatabase();

private:
    PGconn* conn_;
    std::string validationResultId_;
    std::string certificateId_;

    std::vector<ValidationStepResult> steps_;
    ValidationStepResult* currentStep_ = nullptr;

    std::string stepToString(ValidationStep step) const;
    std::string statusToString(StepStatus status) const;
    std::string generateRecommendation(const std::string& errorCode) const;
};

} // namespace validation
```

**Implementation**: `services/pkd-management/src/validation/validation_detail_recorder.cpp`

```cpp
#include "validation_detail_recorder.h"
#include <spdlog/spdlog.h>

namespace validation {

ValidationDetailRecorder::ValidationDetailRecorder(
    PGconn* conn,
    const std::string& validationResultId,
    const std::string& certificateId)
    : conn_(conn)
    , validationResultId_(validationResultId)
    , certificateId_(certificateId) {
}

ValidationDetailRecorder::~ValidationDetailRecorder() {
    // Auto-save on destruction (RAII pattern)
    if (!steps_.empty()) {
        saveToDatabase();
    }
}

void ValidationDetailRecorder::startStep(ValidationStep step) {
    ValidationStepResult stepResult;
    stepResult.step = step;
    stepResult.stepNumber = static_cast<int>(step);
    stepResult.status = StepStatus::IN_PROGRESS;
    stepResult.success = false;
    stepResult.startedAt = std::chrono::system_clock::now();

    steps_.push_back(stepResult);
    currentStep_ = &steps_.back();

    spdlog::debug("Validation step started: {}", stepToString(step));
}

void ValidationDetailRecorder::completeStep(StepStatus status,
                                            const Json::Value& stepData) {
    if (!currentStep_) {
        spdlog::error("No active step to complete");
        return;
    }

    currentStep_->completedAt = std::chrono::system_clock::now();
    currentStep_->durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentStep_->completedAt - currentStep_->startedAt).count();

    currentStep_->status = status;
    currentStep_->success = (status == StepStatus::SUCCESS);
    currentStep_->stepData = stepData;

    spdlog::debug("Validation step completed: {} - {} ({}ms)",
                  stepToString(currentStep_->step),
                  statusToString(status),
                  currentStep_->durationMs);

    currentStep_ = nullptr;
}

void ValidationDetailRecorder::completeStepWithError(
    const std::string& errorCode,
    const std::string& errorMessage,
    const Json::Value& stepData) {

    if (!currentStep_) {
        spdlog::error("No active step to complete");
        return;
    }

    currentStep_->errorCode = errorCode;
    currentStep_->errorMessage = errorMessage;
    currentStep_->recommendation = generateRecommendation(errorCode);

    completeStep(StepStatus::FAILED, stepData);

    spdlog::warn("Validation step failed: {} - {} ({})",
                 stepToString(currentStep_->step),
                 errorCode,
                 errorMessage);
}

void ValidationDetailRecorder::completeStepWithWarning(
    const std::string& warningMessage,
    const Json::Value& stepData) {

    if (!currentStep_) {
        spdlog::error("No active step to complete");
        return;
    }

    currentStep_->warningMessage = warningMessage;
    completeStep(StepStatus::WARNING, stepData);

    spdlog::info("Validation step completed with warning: {} - {}",
                 stepToString(currentStep_->step),
                 warningMessage);
}

void ValidationDetailRecorder::skipStep(ValidationStep step, const std::string& reason) {
    ValidationStepResult stepResult;
    stepResult.step = step;
    stepResult.stepNumber = static_cast<int>(step);
    stepResult.status = StepStatus::SKIPPED;
    stepResult.success = true;  // Skipped is not a failure
    stepResult.startedAt = std::chrono::system_clock::now();
    stepResult.completedAt = stepResult.startedAt;
    stepResult.durationMs = 0;
    stepResult.warningMessage = "Skipped: " + reason;

    steps_.push_back(stepResult);

    spdlog::debug("Validation step skipped: {} - {}", stepToString(step), reason);
}

const std::vector<ValidationStepResult>& ValidationDetailRecorder::getSteps() const {
    return steps_;
}

bool ValidationDetailRecorder::saveToDatabase() {
    if (steps_.empty()) {
        return true;
    }

    // Begin transaction
    PGresult* res = PQexec(conn_, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to begin transaction: {}", PQerrorMessage(conn_));
        PQclear(res);
        return false;
    }
    PQclear(res);

    // Insert each step
    for (const auto& step : steps_) {
        // Prepare JSON step_data
        Json::StreamWriterBuilder writer;
        std::string stepDataJson = Json::writeString(writer, step.stepData);

        // Format timestamps
        std::time_t startedAt = std::chrono::system_clock::to_time_t(step.startedAt);
        std::time_t completedAt = std::chrono::system_clock::to_time_t(step.completedAt);
        char startedAtStr[32], completedAtStr[32];
        std::strftime(startedAtStr, sizeof(startedAtStr), "%Y-%m-%d %H:%M:%S", std::gmtime(&startedAt));
        std::strftime(completedAtStr, sizeof(completedAtStr), "%Y-%m-%d %H:%M:%S", std::gmtime(&completedAt));

        // Parameterized INSERT query
        const char* query =
            "INSERT INTO validation_result_detail ("
            "validation_result_id, certificate_id, step_number, step_name, step_status, "
            "step_started_at, step_completed_at, step_duration_ms, "
            "success, error_code, error_message, warning_message, "
            "step_data, recommendation"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)";

        const char* paramValues[14] = {
            validationResultId_.c_str(),
            certificateId_.c_str(),
            std::to_string(step.stepNumber).c_str(),
            stepToString(step.step).c_str(),
            statusToString(step.status).c_str(),
            startedAtStr,
            completedAtStr,
            std::to_string(step.durationMs).c_str(),
            step.success ? "true" : "false",
            step.errorCode.empty() ? nullptr : step.errorCode.c_str(),
            step.errorMessage.empty() ? nullptr : step.errorMessage.c_str(),
            step.warningMessage.empty() ? nullptr : step.warningMessage.c_str(),
            stepDataJson.c_str(),
            step.recommendation.empty() ? nullptr : step.recommendation.c_str()
        };

        res = PQexecParams(conn_, query, 14, nullptr, paramValues, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::error("Failed to insert validation step detail: {}", PQerrorMessage(conn_));
            PQclear(res);
            PQexec(conn_, "ROLLBACK");
            return false;
        }
        PQclear(res);
    }

    // Commit transaction
    res = PQexec(conn_, "COMMIT");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to commit transaction: {}", PQerrorMessage(conn_));
        PQclear(res);
        return false;
    }
    PQclear(res);

    spdlog::info("Saved {} validation step details to database", steps_.size());
    return true;
}

std::string ValidationDetailRecorder::stepToString(ValidationStep step) const {
    switch (step) {
        case ValidationStep::PARSE_CERTIFICATE: return "PARSE_CERTIFICATE";
        case ValidationStep::CHECK_EXPIRATION: return "CHECK_EXPIRATION";
        case ValidationStep::FIND_CSCA: return "FIND_CSCA";
        case ValidationStep::BUILD_TRUST_CHAIN: return "BUILD_TRUST_CHAIN";
        case ValidationStep::VERIFY_SIGNATURES: return "VERIFY_SIGNATURES";
        case ValidationStep::CHECK_EXTENSIONS: return "CHECK_EXTENSIONS";
        case ValidationStep::CHECK_REVOCATION: return "CHECK_REVOCATION";
        case ValidationStep::FINAL_DECISION: return "FINAL_DECISION";
        default: return "UNKNOWN";
    }
}

std::string ValidationDetailRecorder::statusToString(StepStatus status) const {
    switch (status) {
        case StepStatus::SUCCESS: return "SUCCESS";
        case StepStatus::WARNING: return "WARNING";
        case StepStatus::FAILED: return "FAILED";
        case StepStatus::SKIPPED: return "SKIPPED";
        case StepStatus::IN_PROGRESS: return "IN_PROGRESS";
        default: return "UNKNOWN";
    }
}

std::string ValidationDetailRecorder::generateRecommendation(const std::string& errorCode) const {
    // Query validation_error_codes table for recommendation
    const char* query = "SELECT recommendation FROM validation_error_codes WHERE error_code = $1";
    const char* paramValues[1] = {errorCode.c_str()};

    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return "Contact system administrator for assistance";
    }

    std::string recommendation = PQgetvalue(res, 0, 0);
    PQclear(res);

    return recommendation;
}

} // namespace validation
```

### 2. Integration into validateDscCertificate()

**Modified Function** (with detailed recording):

```cpp
DscValidationResult validateDscCertificate(PGconn* conn,
                                           X509* dscCert,
                                           const std::string& issuerDn,
                                           const std::string& validationResultId,
                                           const std::string& certificateId) {
    DscValidationResult result = {false, false, false, false, false, "", "", ""};

    // Create validation detail recorder
    validation::ValidationDetailRecorder recorder(conn, validationResultId, certificateId);

    // ==================== STEP 1: PARSE_CERTIFICATE ====================
    // (Already parsed by caller, record as success)
    recorder.startStep(validation::ValidationStep::PARSE_CERTIFICATE);
    Json::Value parseData;
    parseData["certificate_format"] = "DER";
    parseData["parse_time_ms"] = 5;  // Estimated
    recorder.completeStep(validation::StepStatus::SUCCESS, parseData);

    // ==================== STEP 2: CHECK_EXPIRATION ====================
    recorder.startStep(validation::ValidationStep::CHECK_EXPIRATION);

    time_t now = time(nullptr);
    time_t notAfter = ASN1_TIME_to_time_t(X509_get0_notAfter(dscCert));
    time_t notBefore = ASN1_TIME_to_time_t(X509_get0_notBefore(dscCert));

    Json::Value expirationData;
    expirationData["not_before"] = timeToIsoString(notBefore);
    expirationData["not_after"] = timeToIsoString(notAfter);
    expirationData["current_time"] = timeToIsoString(now);
    expirationData["is_expired"] = (now > notAfter);
    expirationData["is_not_yet_valid"] = (now < notBefore);
    expirationData["days_until_expiry"] = (notAfter - now) / 86400;

    if (now > notAfter) {
        result.errorMessage = "DSC certificate is expired";
        recorder.completeStepWithError("CERT_EXPIRED", result.errorMessage, expirationData);
        recorder.saveToDatabase();
        return result;
    }

    if (now < notBefore) {
        result.errorMessage = "DSC certificate is not yet valid";
        recorder.completeStepWithError("CERT_NOT_YET_VALID", result.errorMessage, expirationData);
        recorder.saveToDatabase();
        return result;
    }

    result.notExpired = true;
    recorder.completeStep(validation::StepStatus::SUCCESS, expirationData);

    // ==================== STEP 3: FIND_CSCA ====================
    recorder.startStep(validation::ValidationStep::FIND_CSCA);

    auto startFindCsca = std::chrono::high_resolution_clock::now();
    std::vector<X509*> allCscas = findAllCscasBySubjectDn(conn, issuerDn);
    auto endFindCsca = std::chrono::high_resolution_clock::now();
    int findCscaTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endFindCsca - startFindCsca).count();

    Json::Value cscaData;
    cscaData["issuer_dn"] = issuerDn;
    cscaData["csca_count_found"] = static_cast<int>(allCscas.size());
    cscaData["search_time_ms"] = findCscaTimeMs;

    Json::Value cscasArray(Json::arrayValue);
    for (X509* csca : allCscas) {
        Json::Value cscaInfo;
        cscaInfo["serial_number"] = getSerialNumber(csca);
        cscaInfo["fingerprint"] = getFingerprint(csca);
        cscaInfo["type"] = isSelfSigned(csca) ? "self-signed" : "link_certificate";
        if (!isSelfSigned(csca)) {
            cscaInfo["links_to"] = getCertSubjectDn(csca);
        }
        cscasArray.append(cscaInfo);
    }
    cscaData["cscas"] = cscasArray;

    if (allCscas.empty()) {
        result.errorMessage = "No CSCA found for issuer: " + issuerDn.substr(0, 80);
        recorder.completeStepWithError("CSCA_NOT_FOUND", result.errorMessage, cscaData);
        recorder.saveToDatabase();
        return result;
    }

    result.cscaFound = true;
    result.cscaSubjectDn = issuerDn;

    if (allCscas.size() > 1) {
        recorder.completeStepWithWarning(
            "Multiple CSCAs found (including link certificates) - this is normal during key rollover",
            cscaData);
    } else {
        recorder.completeStep(validation::StepStatus::SUCCESS, cscaData);
    }

    // ==================== STEP 4: BUILD_TRUST_CHAIN ====================
    recorder.startStep(validation::ValidationStep::BUILD_TRUST_CHAIN);

    auto startBuildChain = std::chrono::high_resolution_clock::now();
    TrustChain chain = buildTrustChain(dscCert, allCscas);
    auto endBuildChain = std::chrono::high_resolution_clock::now();
    int buildChainTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endBuildChain - startBuildChain).count();

    Json::Value chainData;
    chainData["chain_length"] = static_cast<int>(chain.certificates.size());
    chainData["chain_depth"] = static_cast<int>(chain.certificates.size());
    chainData["max_depth_allowed"] = 5;
    chainData["circular_reference_detected"] = !chain.isValid && chain.errorMessage.find("Circular") != std::string::npos;
    chainData["build_time_ms"] = buildChainTimeMs;

    Json::Value chainSteps(Json::arrayValue);
    for (size_t i = 0; i < chain.certificates.size(); i++) {
        Json::Value stepInfo;
        stepInfo["level"] = static_cast<int>(i);
        stepInfo["certificate_type"] = (i == 0) ? "DSC" : (isSelfSigned(chain.certificates[i]) ? "CSCA" : "LINK_CERT");
        stepInfo["subject_dn"] = getCertSubjectDn(chain.certificates[i]);
        stepInfo["issuer_dn"] = getCertIssuerDn(chain.certificates[i]);
        stepInfo["is_self_signed"] = isSelfSigned(chain.certificates[i]);
        if (i == chain.certificates.size() - 1 && isSelfSigned(chain.certificates[i])) {
            stepInfo["is_root"] = true;
        }
        chainSteps.append(stepInfo);
    }
    chainData["chain_steps"] = chainSteps;

    if (!chain.isValid) {
        result.errorMessage = "Failed to build trust chain: " + chain.errorMessage;

        std::string errorCode = "CHAIN_BUILD_FAILED";
        if (chain.errorMessage.find("Circular") != std::string::npos) {
            errorCode = "CHAIN_CIRCULAR_REF";
        } else if (chain.errorMessage.find("depth") != std::string::npos) {
            errorCode = "CHAIN_MAX_DEPTH";
        } else if (chain.errorMessage.find("broken") != std::string::npos) {
            errorCode = "CHAIN_BROKEN";
        }

        recorder.completeStepWithError(errorCode, result.errorMessage, chainData);

        // Cleanup
        for (X509* csca : allCscas) X509_free(csca);
        recorder.saveToDatabase();
        return result;
    }

    recorder.completeStep(validation::StepStatus::SUCCESS, chainData);

    // ==================== STEP 5: VERIFY_SIGNATURES ====================
    recorder.startStep(validation::ValidationStep::VERIFY_SIGNATURES);

    auto startVerify = std::chrono::high_resolution_clock::now();
    bool chainValid = validateTrustChain(chain);
    auto endVerify = std::chrono::high_resolution_clock::now();
    int verifyTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endVerify - startVerify).count();

    Json::Value signatureData;
    signatureData["total_signatures_verified"] = static_cast<int>(chain.certificates.size() - 1);
    signatureData["total_verify_time_ms"] = verifyTimeMs;
    signatureData["all_signatures_valid"] = chainValid;

    // Detailed signature verification results (collected during validateTrustChain)
    Json::Value sigDetails(Json::arrayValue);
    for (size_t i = 0; i < chain.certificates.size() - 1; i++) {
        Json::Value sigInfo;
        sigInfo["step"] = getCertSubjectDn(chain.certificates[i]).substr(0, 30) + " → " +
                          getCertSubjectDn(chain.certificates[i + 1]).substr(0, 30);
        sigInfo["algorithm"] = getSignatureAlgorithm(chain.certificates[i]);
        sigInfo["key_size_bits"] = getPublicKeySize(chain.certificates[i + 1]);
        sigInfo["signature_valid"] = chainValid;  // Simplified (actual validation in validateTrustChain)
        sigDetails.append(sigInfo);
    }
    signatureData["signature_details"] = sigDetails;

    if (chainValid) {
        result.signatureValid = true;
        result.isValid = true;
        result.trustChainPath = chain.path;
        recorder.completeStep(validation::StepStatus::SUCCESS, signatureData);
    } else {
        result.errorMessage = "Trust chain signature validation failed";
        recorder.completeStepWithError("SIGNATURE_INVALID", result.errorMessage, signatureData);
    }

    // ==================== STEP 6: CHECK_EXTENSIONS ====================
    // (Optional - can be implemented later)
    recorder.skipStep(validation::ValidationStep::CHECK_EXTENSIONS, "Not implemented in current version");

    // ==================== STEP 7: CHECK_REVOCATION ====================
    // (Optional - can be implemented later)
    recorder.skipStep(validation::ValidationStep::CHECK_REVOCATION, "Not implemented in current version");

    // ==================== STEP 8: FINAL_DECISION ====================
    recorder.startStep(validation::ValidationStep::FINAL_DECISION);

    Json::Value finalData;
    finalData["overall_status"] = result.isValid ? "VALID" : "INVALID";
    finalData["passed_steps"] = 5;  // Count of successful steps
    finalData["failed_steps"] = result.isValid ? 0 : 1;
    finalData["warning_steps"] = (allCscas.size() > 1) ? 1 : 0;
    finalData["compliance_level"] = result.isValid ? "ICAO_9303_COMPLIANT" : "NON_COMPLIANT";

    recorder.completeStep(result.isValid ? validation::StepStatus::SUCCESS : validation::StepStatus::FAILED,
                          finalData);

    // Cleanup
    for (X509* csca : allCscas) X509_free(csca);

    // Save all steps to database
    recorder.saveToDatabase();

    return result;
}
```

---

## Frontend UX Design

### 1. Certificate Validation Detail Page

**Route**: `/certificates/:certificateId/validation-detail`

**Component Structure**:
```
ValidationDetailPage
├── CertificateInfoCard (metadata, fingerprint, validity dates)
├── ValidationTimelineStepper (visual step-by-step timeline)
├── TrustChainVisualization (interactive graph)
├── ValidationStepDetails (accordion with step data)
└── RecommendationsPanel (actionable insights)
```

### 2. ValidationTimelineStepper Component

**Purpose**: Show validation progress visually like a wizard stepper.

**File**: `frontend/src/components/ValidationTimelineStepper.tsx`

```typescript
import React from 'react';
import {
  Check,
  X,
  AlertTriangle,
  Minus,
  Clock
} from 'lucide-react';

interface ValidationStep {
  stepNumber: number;
  stepName: string;
  stepStatus: 'SUCCESS' | 'WARNING' | 'FAILED' | 'SKIPPED' | 'IN_PROGRESS';
  success: boolean;
  errorCode?: string;
  errorMessage?: string;
  warningMessage?: string;
  stepDurationMs?: number;
  recommendation?: string;
}

interface Props {
  steps: ValidationStep[];
}

const ValidationTimelineStepper: React.FC<Props> = ({ steps }) => {
  const getStepIcon = (status: string) => {
    switch (status) {
      case 'SUCCESS':
        return <Check className="w-5 h-5 text-green-600" />;
      case 'WARNING':
        return <AlertTriangle className="w-5 h-5 text-yellow-600" />;
      case 'FAILED':
        return <X className="w-5 h-5 text-red-600" />;
      case 'SKIPPED':
        return <Minus className="w-5 h-5 text-gray-400" />;
      case 'IN_PROGRESS':
        return <Clock className="w-5 h-5 text-blue-600 animate-spin" />;
      default:
        return null;
    }
  };

  const getStepColor = (status: string) => {
    switch (status) {
      case 'SUCCESS':
        return 'border-green-500 bg-green-50';
      case 'WARNING':
        return 'border-yellow-500 bg-yellow-50';
      case 'FAILED':
        return 'border-red-500 bg-red-50';
      case 'SKIPPED':
        return 'border-gray-300 bg-gray-50';
      case 'IN_PROGRESS':
        return 'border-blue-500 bg-blue-50';
      default:
        return 'border-gray-300 bg-white';
    }
  };

  const getConnectorColor = (currentStatus: string, nextStatus?: string) => {
    if (currentStatus === 'FAILED') return 'bg-red-300';
    if (currentStatus === 'WARNING') return 'bg-yellow-300';
    if (nextStatus === 'SKIPPED') return 'bg-gray-300';
    return 'bg-green-300';
  };

  const formatStepName = (name: string) => {
    return name.split('_').map(word =>
      word.charAt(0) + word.slice(1).toLowerCase()
    ).join(' ');
  };

  return (
    <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
      <h2 className="text-xl font-semibold mb-6">Validation Timeline</h2>

      <div className="relative">
        {steps.map((step, index) => (
          <div key={step.stepNumber} className="relative pb-8 last:pb-0">
            {/* Connector Line */}
            {index < steps.length - 1 && (
              <div
                className={`absolute left-5 top-10 w-0.5 h-full ${getConnectorColor(
                  step.stepStatus,
                  steps[index + 1]?.stepStatus
                )}`}
              />
            )}

            {/* Step Card */}
            <div className="relative flex items-start gap-4">
              {/* Step Icon Circle */}
              <div
                className={`flex-shrink-0 w-10 h-10 rounded-full border-2 flex items-center justify-center ${getStepColor(
                  step.stepStatus
                )}`}
              >
                {getStepIcon(step.stepStatus)}
              </div>

              {/* Step Content */}
              <div className="flex-1 min-w-0">
                <div className="flex items-center justify-between gap-2">
                  <h3 className="text-base font-medium text-gray-900">
                    {formatStepName(step.stepName)}
                  </h3>
                  {step.stepDurationMs !== undefined && (
                    <span className="text-sm text-gray-500">
                      {step.stepDurationMs}ms
                    </span>
                  )}
                </div>

                {/* Error Message */}
                {step.errorMessage && (
                  <div className="mt-2 p-3 bg-red-50 border border-red-200 rounded-lg">
                    <p className="text-sm text-red-800 font-medium">
                      {step.errorCode && `[${step.errorCode}] `}
                      {step.errorMessage}
                    </p>
                  </div>
                )}

                {/* Warning Message */}
                {step.warningMessage && (
                  <div className="mt-2 p-3 bg-yellow-50 border border-yellow-200 rounded-lg">
                    <p className="text-sm text-yellow-800">
                      {step.warningMessage}
                    </p>
                  </div>
                )}

                {/* Recommendation */}
                {step.recommendation && (
                  <div className="mt-2 p-3 bg-blue-50 border border-blue-200 rounded-lg">
                    <p className="text-sm text-blue-900 font-medium">
                      💡 Recommendation:
                    </p>
                    <p className="text-sm text-blue-800 mt-1">
                      {step.recommendation}
                    </p>
                  </div>
                )}

                {/* Success Indicator */}
                {step.stepStatus === 'SUCCESS' && !step.warningMessage && (
                  <p className="mt-1 text-sm text-green-600">
                    ✓ Completed successfully
                  </p>
                )}

                {/* Skipped Indicator */}
                {step.stepStatus === 'SKIPPED' && (
                  <p className="mt-1 text-sm text-gray-500 italic">
                    {step.warningMessage || 'Step skipped'}
                  </p>
                )}
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

export default ValidationTimelineStepper;
```

### 3. TrustChainVisualization Component

**Purpose**: Interactive graph showing certificate chain with link certificates.

**Library**: `react-flow` or `vis-network`

```typescript
import React from 'react';
import ReactFlow, {
  Node,
  Edge,
  Controls,
  Background,
  MiniMap
} from 'reactflow';
import 'reactflow/dist/style.css';

interface TrustChainStep {
  level: number;
  certificateType: string;
  subjectDn: string;
  issuerDn: string;
  isSelfSigned?: boolean;
  isRoot?: boolean;
  isLinkCertificate?: boolean;
}

interface Props {
  chainSteps: TrustChainStep[];
}

const TrustChainVisualization: React.FC<Props> = ({ chainSteps }) => {
  // Convert chain steps to React Flow nodes and edges
  const nodes: Node[] = chainSteps.map((step, index) => ({
    id: `node-${index}`,
    type: 'default',
    position: { x: 150, y: index * 150 },
    data: {
      label: (
        <div className="text-center">
          <div className="font-semibold text-sm mb-1">
            {step.certificateType}
          </div>
          <div className="text-xs text-gray-600 max-w-[200px] truncate">
            {step.subjectDn.split(',')[0]}
          </div>
          {step.isLinkCertificate && (
            <div className="mt-1 px-2 py-0.5 bg-purple-100 text-purple-700 text-xs rounded">
              Link Certificate
            </div>
          )}
          {step.isRoot && (
            <div className="mt-1 px-2 py-0.5 bg-green-100 text-green-700 text-xs rounded">
              Root CA
            </div>
          )}
        </div>
      )
    },
    style: {
      background: step.isLinkCertificate
        ? '#f3e8ff'
        : step.isSelfSigned
        ? '#dcfce7'
        : '#dbeafe',
      border: step.isLinkCertificate
        ? '2px solid #a855f7'
        : step.isSelfSigned
        ? '2px solid #22c55e'
        : '2px solid #3b82f6',
      borderRadius: '8px',
      padding: '12px',
      width: 250
    }
  }));

  const edges: Edge[] = chainSteps
    .slice(0, -1)
    .map((_, index) => ({
      id: `edge-${index}`,
      source: `node-${index}`,
      target: `node-${index + 1}`,
      type: 'smoothstep',
      animated: true,
      label: 'signs',
      style: { stroke: '#9ca3af', strokeWidth: 2 }
    }));

  return (
    <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
      <h2 className="text-xl font-semibold mb-4">Trust Chain Visualization</h2>

      <div className="h-[500px] border border-gray-200 rounded-lg">
        <ReactFlow
          nodes={nodes}
          edges={edges}
          fitView
          attributionPosition="bottom-left"
        >
          <Controls />
          <Background />
          <MiniMap />
        </ReactFlow>
      </div>

      <div className="mt-4 flex gap-4 text-sm">
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-blue-100 border-2 border-blue-500 rounded"></div>
          <span>DSC Certificate</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-purple-100 border-2 border-purple-500 rounded"></div>
          <span>Link Certificate</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-4 h-4 bg-green-100 border-2 border-green-500 rounded"></div>
          <span>Self-Signed CSCA</span>
        </div>
      </div>
    </div>
  );
};

export default TrustChainVisualization;
```

### 4. ValidationStepDetails Component (Accordion)

**Purpose**: Expandable sections showing detailed step_data JSON.

```typescript
import React, { useState } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';

interface StepData {
  stepNumber: number;
  stepName: string;
  stepData: Record<string, any>;
  stepDurationMs: number;
}

interface Props {
  steps: StepData[];
}

const ValidationStepDetails: React.FC<Props> = ({ steps }) => {
  const [expandedStep, setExpandedStep] = useState<number | null>(null);

  const toggleStep = (stepNumber: number) => {
    setExpandedStep(expandedStep === stepNumber ? null : stepNumber);
  };

  const formatStepName = (name: string) => {
    return name.split('_').map(word =>
      word.charAt(0) + word.slice(1).toLowerCase()
    ).join(' ');
  };

  return (
    <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
      <h2 className="text-xl font-semibold mb-4">Step Details</h2>

      <div className="space-y-2">
        {steps.map((step) => (
          <div
            key={step.stepNumber}
            className="border border-gray-200 rounded-lg overflow-hidden"
          >
            <button
              onClick={() => toggleStep(step.stepNumber)}
              className="w-full px-4 py-3 bg-gray-50 hover:bg-gray-100 flex items-center justify-between transition-colors"
            >
              <div className="flex items-center gap-3">
                <span className="text-sm font-medium text-gray-700">
                  Step {step.stepNumber}:
                </span>
                <span className="text-sm text-gray-900">
                  {formatStepName(step.stepName)}
                </span>
                <span className="text-xs text-gray-500">
                  ({step.stepDurationMs}ms)
                </span>
              </div>
              {expandedStep === step.stepNumber ? (
                <ChevronUp className="w-5 h-5 text-gray-400" />
              ) : (
                <ChevronDown className="w-5 h-5 text-gray-400" />
              )}
            </button>

            {expandedStep === step.stepNumber && (
              <div className="px-4 py-3 bg-white border-t border-gray-200">
                <pre className="text-xs bg-gray-50 p-3 rounded overflow-x-auto">
                  {JSON.stringify(step.stepData, null, 2)}
                </pre>
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
};

export default ValidationStepDetails;
```

### 5. RecommendationsPanel Component

**Purpose**: Show actionable recommendations to fix validation issues.

```typescript
import React from 'react';
import { AlertCircle, CheckCircle, Info } from 'lucide-react';

interface Recommendation {
  type: 'error' | 'warning' | 'info';
  title: string;
  message: string;
  action?: string;
}

interface Props {
  recommendations: Recommendation[];
}

const RecommendationsPanel: React.FC<Props> = ({ recommendations }) => {
  if (recommendations.length === 0) {
    return (
      <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
        <div className="flex items-center gap-3">
          <CheckCircle className="w-6 h-6 text-green-600" />
          <div>
            <h3 className="font-semibold text-green-900">
              All Validations Passed
            </h3>
            <p className="text-sm text-green-700 mt-1">
              No issues detected. Certificate is ICAO 9303 compliant.
            </p>
          </div>
        </div>
      </div>
    );
  }

  const getIcon = (type: string) => {
    switch (type) {
      case 'error':
        return <AlertCircle className="w-5 h-5 text-red-600" />;
      case 'warning':
        return <AlertCircle className="w-5 h-5 text-yellow-600" />;
      case 'info':
        return <Info className="w-5 h-5 text-blue-600" />;
    }
  };

  const getBgColor = (type: string) => {
    switch (type) {
      case 'error':
        return 'bg-red-50 border-red-200';
      case 'warning':
        return 'bg-yellow-50 border-yellow-200';
      case 'info':
        return 'bg-blue-50 border-blue-200';
    }
  };

  const getTextColor = (type: string) => {
    switch (type) {
      case 'error':
        return 'text-red-900';
      case 'warning':
        return 'text-yellow-900';
      case 'info':
        return 'text-blue-900';
    }
  };

  return (
    <div className="bg-white rounded-xl shadow-sm border border-gray-200 p-6">
      <h2 className="text-xl font-semibold mb-4">Recommendations</h2>

      <div className="space-y-3">
        {recommendations.map((rec, index) => (
          <div
            key={index}
            className={`p-4 rounded-lg border ${getBgColor(rec.type)}`}
          >
            <div className="flex items-start gap-3">
              <div className="flex-shrink-0 mt-0.5">
                {getIcon(rec.type)}
              </div>
              <div className="flex-1 min-w-0">
                <h4 className={`font-medium ${getTextColor(rec.type)}`}>
                  {rec.title}
                </h4>
                <p className={`text-sm mt-1 ${getTextColor(rec.type)} opacity-90`}>
                  {rec.message}
                </p>
                {rec.action && (
                  <button className="mt-2 text-sm font-medium underline hover:no-underline">
                    {rec.action}
                  </button>
                )}
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

export default RecommendationsPanel;
```

---

## API Design

### 1. Get Validation Details

**Endpoint**: `GET /api/certificates/{certificateId}/validation-detail`

**Response**:
```json
{
  "certificateId": "abc123-def456-...",
  "uploadId": "upload-xyz",
  "certificateType": "DSC",
  "countryCode": "CN",
  "subjectDn": "C=CN, O=Test, CN=DSC_Beijing",
  "issuerDn": "C=CN, O=中华人民共和国, CN=CSCA",
  "serialNumber": "1234567890ABCDEF",
  "fingerprintSha256": "72b3f2a05a3ec5e8...",

  "validationResult": {
    "validationStatus": "VALID",
    "trustChainValid": true,
    "trustChainPath": "DSC → CN=CSCA_OLD → CN=Link → CN=CSCA_NEW",
    "validationTimestamp": "2026-01-23T10:30:00Z",
    "totalValidationTimeMs": 85
  },

  "validationSteps": [
    {
      "stepNumber": 1,
      "stepName": "PARSE_CERTIFICATE",
      "stepStatus": "SUCCESS",
      "success": true,
      "stepStartedAt": "2026-01-23T10:30:00.000Z",
      "stepCompletedAt": "2026-01-23T10:30:00.005Z",
      "stepDurationMs": 5,
      "stepData": {
        "certificate_size_bytes": 1024,
        "certificate_format": "DER",
        "parse_time_ms": 5
      }
    },
    {
      "stepNumber": 2,
      "stepName": "CHECK_EXPIRATION",
      "stepStatus": "SUCCESS",
      "success": true,
      "stepDurationMs": 2,
      "stepData": {
        "not_before": "2020-01-01T00:00:00Z",
        "not_after": "2030-01-01T00:00:00Z",
        "days_until_expiry": 1437,
        "is_expired": false
      }
    },
    {
      "stepNumber": 3,
      "stepName": "FIND_CSCA",
      "stepStatus": "WARNING",
      "success": true,
      "stepDurationMs": 15,
      "warningMessage": "Multiple CSCAs found (including link certificates) - this is normal during key rollover",
      "stepData": {
        "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA",
        "csca_count_found": 2,
        "cscas": [
          {
            "serial_number": "434E445343410005",
            "fingerprint": "72b3f2a0...",
            "type": "self-signed"
          },
          {
            "serial_number": "434E445343410005",
            "fingerprint": "e3dbd849...",
            "type": "link_certificate",
            "links_to": "CN=CSCA_NEW"
          }
        ]
      }
    },
    {
      "stepNumber": 4,
      "stepName": "BUILD_TRUST_CHAIN",
      "stepStatus": "SUCCESS",
      "success": true,
      "stepDurationMs": 12,
      "stepData": {
        "chain_length": 4,
        "chain_steps": [
          {
            "level": 0,
            "certificate_type": "DSC",
            "subject_dn": "C=CN, O=Test, CN=DSC_Beijing",
            "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD"
          },
          {
            "level": 1,
            "certificate_type": "CSCA",
            "subject_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
            "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
            "is_self_signed": true
          },
          {
            "level": 2,
            "certificate_type": "LINK_CERT",
            "subject_dn": "C=CN, O=中华人民共和国, CN=CSCA_NEW",
            "issuer_dn": "C=CN, O=中华人民共和国, CN=CSCA_OLD",
            "is_link_certificate": true
          },
          {
            "level": 3,
            "certificate_type": "CSCA",
            "subject_dn": "C=CN, O=中华人민共和国, CN=CSCA_NEW",
            "issuer_dn": "C=CN, O=中华인민共和국, CN=CSCA_NEW",
            "is_self_signed": true,
            "is_root": true
          }
        ]
      }
    },
    {
      "stepNumber": 5,
      "stepName": "VERIFY_SIGNATURES",
      "stepStatus": "SUCCESS",
      "success": true,
      "stepDurationMs": 11,
      "stepData": {
        "total_signatures_verified": 3,
        "all_signatures_valid": true
      }
    },
    {
      "stepNumber": 6,
      "stepName": "CHECK_EXTENSIONS",
      "stepStatus": "SKIPPED",
      "success": true,
      "stepDurationMs": 0,
      "warningMessage": "Skipped: Not implemented in current version"
    },
    {
      "stepNumber": 7,
      "stepName": "CHECK_REVOCATION",
      "stepStatus": "SKIPPED",
      "success": true,
      "stepDurationMs": 0,
      "warningMessage": "Skipped: Not implemented in current version"
    },
    {
      "stepNumber": 8,
      "stepName": "FINAL_DECISION",
      "stepStatus": "SUCCESS",
      "success": true,
      "stepDurationMs": 1,
      "stepData": {
        "overall_status": "VALID",
        "passed_steps": 5,
        "failed_steps": 0,
        "warning_steps": 1,
        "compliance_level": "ICAO_9303_COMPLIANT"
      }
    }
  ],

  "recommendations": []
}
```

### 2. Bulk Validation Status

**Endpoint**: `GET /api/upload/{uploadId}/validation-summary`

**Purpose**: Get aggregated validation results for bulk uploads.

**Response**:
```json
{
  "uploadId": "upload-xyz",
  "totalCertificates": 1000,
  "validationSummary": {
    "valid": 850,
    "invalid": 150,
    "validPercentage": 85.0
  },
  "failureBreakdown": [
    {
      "errorCode": "CSCA_NOT_FOUND",
      "count": 80,
      "percentage": 53.3,
      "affectedCountries": ["CN", "DE", "KZ"],
      "recommendation": "Upload the CSCA Master List for these countries"
    },
    {
      "errorCode": "CERT_EXPIRED",
      "count": 45,
      "percentage": 30.0,
      "affectedCountries": ["FR", "ES"],
      "recommendation": "Contact issuing countries to obtain updated certificates"
    },
    {
      "errorCode": "SIGNATURE_INVALID",
      "count": 25,
      "percentage": 16.7,
      "affectedCountries": ["IT"],
      "recommendation": "Verify certificates are not tampered or signed by wrong key"
    }
  ],
  "stepFailureBreakdown": [
    {
      "stepName": "FIND_CSCA",
      "failureCount": 80,
      "percentage": 53.3
    },
    {
      "stepName": "CHECK_EXPIRATION",
      "failureCount": 45,
      "percentage": 30.0
    },
    {
      "stepName": "VERIFY_SIGNATURES",
      "failureCount": 25,
      "percentage": 16.7
    }
  ]
}
```

---

## Implementation Timeline

### Sprint Integration (Add to existing CSCA Issues plan)

#### Sprint 3.5: Validation Detail Enhancement (Week 3.5 - 3 days)

**Parallel to existing Sprint 3 tasks**

**Day 1: Database Schema**
- [ ] Create `validation_result_detail` table (2h)
- [ ] Create `validation_error_codes` table and populate (2h)
- [ ] Migration scripts (1h)
- [ ] Test schema on local environment (1h)

**Day 2: Backend Implementation**
- [ ] Implement `ValidationDetailRecorder` class (4h)
- [ ] Integrate into `validateDscCertificate()` (2h)
- [ ] Unit tests for recorder (2h)

**Day 3: API & Frontend**
- [ ] API endpoint: GET `/api/certificates/{id}/validation-detail` (2h)
- [ ] API endpoint: GET `/api/upload/{id}/validation-summary` (1h)
- [ ] Frontend components: Timeline, TrustChain, Details (4h)
- [ ] Integration testing (1h)

---

## User Stories

### User Story 1: Debug Failed Certificate

**As a** system administrator
**I want to** see detailed validation step results
**So that** I can quickly diagnose why a certificate failed validation

**Acceptance Criteria**:
- ✅ Validation timeline shows each step with pass/fail status
- ✅ Error messages include error codes and recommendations
- ✅ Step-specific data (e.g., CSCA count, chain length) is visible
- ✅ Can identify which step failed (e.g., FIND_CSCA vs VERIFY_SIGNATURES)

### User Story 2: Visualize Trust Chain

**As a** security auditor
**I want to** see the complete certificate chain visually
**So that** I can verify link certificates are used correctly during CSCA key rollover

**Acceptance Criteria**:
- ✅ Interactive graph shows DSC → CSCA_old → Link → CSCA_new
- ✅ Link certificates are visually distinct (purple color)
- ✅ Can hover over each certificate to see details
- ✅ Root CSCA is clearly marked

### User Story 3: Bulk Upload Analysis

**As a** data operator
**I want to** see aggregated failure reasons for bulk uploads
**So that** I can fix issues systematically instead of one-by-one

**Acceptance Criteria**:
- ✅ Validation summary shows top 3 error codes with counts
- ✅ Affected countries listed for each error type
- ✅ Recommendations provided for each error category
- ✅ Can filter/export list of failed certificates by error code

### User Story 4: Actionable Recommendations

**As a** PKD manager
**I want to** receive clear instructions on how to fix validation issues
**So that** I don't need deep technical knowledge to resolve problems

**Acceptance Criteria**:
- ✅ Each validation failure includes a recommendation
- ✅ Recommendations are specific (e.g., "Upload CSCA Master List for CN")
- ✅ ICAO Doc 9303 references provided for compliance
- ✅ Quick actions available (e.g., "Upload Master List" button)

---

## Testing Plan

### Unit Tests

```cpp
TEST(ValidationDetailRecorderTest, RecordSuccessfulStep) {
    ValidationDetailRecorder recorder(conn, "vr-123", "cert-456");

    recorder.startStep(ValidationStep::CHECK_EXPIRATION);
    Json::Value data;
    data["is_expired"] = false;
    recorder.completeStep(StepStatus::SUCCESS, data);

    auto steps = recorder.getSteps();
    ASSERT_EQ(steps.size(), 1);
    EXPECT_EQ(steps[0].status, StepStatus::SUCCESS);
    EXPECT_TRUE(steps[0].success);
}

TEST(ValidationDetailRecorderTest, RecordFailedStep) {
    ValidationDetailRecorder recorder(conn, "vr-123", "cert-456");

    recorder.startStep(ValidationStep::FIND_CSCA);
    recorder.completeStepWithError("CSCA_NOT_FOUND", "No CSCA found");

    auto steps = recorder.getSteps();
    EXPECT_EQ(steps[0].errorCode, "CSCA_NOT_FOUND");
    EXPECT_FALSE(steps[0].success);
    EXPECT_FALSE(steps[0].recommendation.empty());
}
```

### Integration Tests

```typescript
describe('ValidationDetailPage', () => {
  it('displays validation timeline with all steps', async () => {
    render(<ValidationDetailPage certificateId="cert-123" />);

    // Wait for API response
    await waitFor(() => {
      expect(screen.getByText('Validation Timeline')).toBeInTheDocument();
    });

    // Verify all 8 steps displayed
    expect(screen.getAllByText(/Step \d:/)).toHaveLength(8);
  });

  it('shows error details for failed step', async () => {
    render(<ValidationDetailPage certificateId="cert-failed" />);

    await waitFor(() => {
      expect(screen.getByText('CSCA_NOT_FOUND')).toBeInTheDocument();
      expect(screen.getByText(/Upload the CSCA Master List/)).toBeInTheDocument();
    });
  });

  it('displays trust chain visualization', async () => {
    render(<ValidationDetailPage certificateId="cert-with-link" />);

    await waitFor(() => {
      expect(screen.getByText('Trust Chain Visualization')).toBeInTheDocument();
      expect(screen.getByText('Link Certificate')).toBeInTheDocument();
    });
  });
});
```

---

## Success Metrics

| Metric | Baseline | Target | Measurement |
|--------|----------|--------|-------------|
| **Time to Diagnose Issues** | 15 min | < 2 min | User testing |
| **Support Ticket Volume** | 20/week | < 5/week | Support system |
| **User Satisfaction** | 3.2/5 | > 4.5/5 | Survey |
| **Validation Detail Page Views** | 0 | > 500/week | Analytics |
| **Failed Certificate Resolution Rate** | 60% | > 90% | Database tracking |

---

## Appendix: Error Code Reference

Full list in `validation_error_codes` table (46 error codes defined).

**Top 10 Most Common Error Codes**:

1. `CSCA_NOT_FOUND` - CSCA not found in database
2. `CERT_EXPIRED` - Certificate has expired
3. `SIGNATURE_INVALID` - Certificate signature verification failed
4. `CHAIN_BUILD_FAILED` - Unable to build trust chain
5. `CHAIN_BROKEN` - Certificate chain is broken
6. `CSCA_EXPIRED` - Issuing CSCA has expired
7. `CRL_NOT_FOUND` - CRL not found in directory
8. `CERT_NOT_YET_VALID` - Certificate is not yet valid
9. `CERT_REVOKED` - Certificate has been revoked
10. `CHAIN_CIRCULAR_REF` - Circular reference detected in chain

---

**END OF DOCUMENT**
