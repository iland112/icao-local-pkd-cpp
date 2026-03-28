#pragma once
/**
 * @file openssl_raii.h
 * @brief RAII wrappers for OpenSSL resource types
 *
 * Provides unique_ptr-based smart pointers for BIO, FILE, X509,
 * CMS_ContentInfo, EVP_PKEY, X509_STORE, and X509_CRL.
 * Zero runtime overhead (empty-base-optimized functor deleters).
 */

#include <memory>
#include <cstdio>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#include <openssl/cms.h>
#include <openssl/evp.h>

namespace openssl {

struct BioDeleter       { void operator()(BIO* p)             const { if (p) BIO_free(p); } };
struct FileDeleter      { void operator()(FILE* p)            const { if (p) fclose(p); } };
struct X509Deleter      { void operator()(X509* p)            const { if (p) X509_free(p); } };
struct X509CrlDeleter   { void operator()(X509_CRL* p)        const { if (p) X509_CRL_free(p); } };
struct CmsDeleter       { void operator()(CMS_ContentInfo* p)  const { if (p) CMS_ContentInfo_free(p); } };
struct EvpPkeyDeleter   { void operator()(EVP_PKEY* p)         const { if (p) EVP_PKEY_free(p); } };
struct X509StoreDeleter { void operator()(X509_STORE* p)       const { if (p) X509_STORE_free(p); } };

using BioPtr       = std::unique_ptr<BIO,             BioDeleter>;
using FilePtr      = std::unique_ptr<FILE,            FileDeleter>;
using X509Ptr      = std::unique_ptr<X509,            X509Deleter>;
using X509CrlPtr   = std::unique_ptr<X509_CRL,        X509CrlDeleter>;
using CmsPtr       = std::unique_ptr<CMS_ContentInfo,  CmsDeleter>;
using EvpPkeyPtr   = std::unique_ptr<EVP_PKEY,         EvpPkeyDeleter>;
using X509StorePtr = std::unique_ptr<X509_STORE,       X509StoreDeleter>;

} // namespace openssl
