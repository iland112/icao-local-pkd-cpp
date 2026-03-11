#pragma once

/**
 * @file personal_info_crypto.h
 * @brief AES-256-GCM encryption/decryption for personal information fields
 *
 * 개인정보보호법 제29조 (안전조치의무) 및 안전성 확보조치 기준 제7조 (암호화) 준수
 * - 요청자명, 연락처, 이메일 등 개인 식별 가능 정보를 AES-256-GCM으로 암호화하여 DB 저장
 * - 환경변수 PII_ENCRYPTION_KEY (hex-encoded 32-byte key) 기반 대칭키 암호화
 * - 암호화된 데이터 형식: "ENC:" + hex(IV[12] + ciphertext + tag[16])
 * - "ENC:" 접두어로 암호화 여부 판별 (기존 평문 데이터 하위 호환)
 */

#include <string>

namespace auth {
namespace pii {

/**
 * @brief Initialize PII encryption from environment variable
 * @return true if PII_ENCRYPTION_KEY is set and valid (64 hex chars = 32 bytes)
 */
bool initialize();

/**
 * @brief Check if PII encryption is enabled
 * @return true if a valid encryption key has been loaded
 */
bool isEnabled();

/**
 * @brief Encrypt a plaintext personal information field
 * @param plaintext The original personal information (e.g., name, phone, email)
 * @return Encrypted string in format "ENC:<hex>" or original plaintext if encryption is disabled
 */
std::string encrypt(const std::string& plaintext);

/**
 * @brief Decrypt a personal information field
 * @param ciphertext The encrypted string (format "ENC:<hex>") or plaintext (backward compatible)
 * @return Decrypted plaintext, or the original string if not encrypted or decryption is disabled
 */
std::string decrypt(const std::string& ciphertext);

/**
 * @brief Check if a value is encrypted (starts with "ENC:" prefix)
 */
bool isEncrypted(const std::string& value);

/**
 * @brief Mask a decrypted personal info field for limited-access display
 * @param value The decrypted plaintext value
 * @param type Field type: "name", "email", "phone", "org"
 * @return Masked string (e.g., "홍*동", "h***@example.com", "010-****-5678")
 */
std::string mask(const std::string& value, const std::string& type);

} // namespace pii
} // namespace auth
