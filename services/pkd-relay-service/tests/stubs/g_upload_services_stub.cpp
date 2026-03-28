/**
 * @file g_upload_services_stub.cpp
 * @brief Defines g_uploadServices = nullptr for test executables that do NOT
 *        exercise the full UploadServiceContainer but still link translation
 *        units that reference the global pointer.
 *
 * Tests that DO exercise UploadServiceContainer directly (test_upload_services)
 * define their own g_uploadServices instance and must NOT link this stub.
 */

#include "upload/upload_services.h"

infrastructure::UploadServiceContainer* g_uploadServices = nullptr;
