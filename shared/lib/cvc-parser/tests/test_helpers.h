/**
 * @file test_helpers.h
 * @brief Test helper utilities for CVC parser unit tests
 *
 * Provides real BSI TR-03110 EAC Worked Example certificate data
 * and utility functions for testing.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cvc_test_helpers {

// =============================================================================
// Hex decode utility
// =============================================================================

inline std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned int byte = 0;
        sscanf(hex.c_str() + i, "%02x", &byte);
        result.push_back(static_cast<uint8_t>(byte));
    }
    return result;
}

// =============================================================================
// BSI TR-03110 EAC Worked Example — ECDH Certificate Chain
// Source: BSI TR-03110 Worked Example (2011), certs_ecdh/
// =============================================================================

// ECDH CVCA: CHR=DECVCAAT00001, CAR=DECVCAAT00001 (self-signed)
// Algorithm: id-TA-ECDSA-SHA-512 (0.4.0.127.0.7.2.2.2.2.5)
// Validity: 2010-09-03 → 2011-09-02
inline const std::string ECDH_CVCA_HEX =
    "7f218202f37f4e82026a5f290100420d444543564341415430303030317f4982021f"
    "060a04007f000702020202058140aadd9db8dbe9c48b3fd4e6ae33c9fc07cb308db"
    "3b3c9d20ed6639cca703308717d4d9b009bc66842aecda12ae6a380e62881ff2f2d"
    "82c68528aa6056583a48f382407830a3318b603b89e2327145ac234cc594cbdd8d3d"
    "f91610a83441caea9863bc2ded5d5aa8253aa10a2ef1c98b9ac8b57f1117a72bf2c"
    "7b9e7c1ac4d77fc94ca83403df91610a83441caea9863bc2ded5d5aa8253aa10a2e"
    "f1c98b9ac8b57f1117a72bf2c7b9e7c1ac4d77fc94cadc083e67984050b75ebae5d"
    "d2809bd638016f7238481810481aee4bdd82ed9645a21322e9c4c6a9385ed9f70b5"
    "d916c1b43b62eef4d0098eff3b1f78e2d0d48d50d1687b93b97d5f7c6d5047406a5"
    "e688b352209bcb9f8227dde385d566332ecc0eabfa9cf7822fdf209f70024a57b1a"
    "a000c55b881f8111b2dcde494a5f485e5bca4bd88a2763aed1ca2b2fa8f0540678c"
    "d1e0f3ad808928540aadd9db8dbe9c48b3fd4e6ae33c9fc07cb308db3b3c9d20ed6"
    "639cca70330870553e5c414ca92619418661197fac10471db1d381085ddaddb58796"
    "829ca900698681810464f09c617c0d5a4e2e88b2598af06860440c07c5ed353a18a"
    "14e938a6cbce30594d94079594ccfaefe28d9aac9ac1bd37c89b6ccbd10b14fc3aa"
    "19deb1fd03ea151a42b892547a339618c4c9f26fa983855d893f81413cd320ea423"
    "0d3415ebccdb5908d91dc23c6566f47b8a0e0a19c175bdc775d8824676aacfaedea"
    "0c160e8701015f200d444543564341415430303030317f4c0e060904007f00070301"
    "02015301c35f25060100000903005f24060101000902055f378180a5e72244d0ac9c"
    "d3e8fe9b878ea32a24f4aa060fd17c2a8291d9797e8a60f28a2978e3f4eae840e95"
    "b6a64fda44d8d39a699521cd5382203964945150a6cc4a50c49ee234ab36df675e4"
    "4c11499080676dd5b3ac1859d98b302ca9605cd94a51d7da5738beaa49848bca777"
    "7b7dc9accdaa1d0b64432b2c13889a4854c9ff346";

// ECDH DV: CHR=DETESTDVDE019, CAR=DECVCAAT00001
// Algorithm: id-TA-ECDSA-SHA-512 (0.4.0.127.0.7.2.2.2.2.5)
inline const std::string ECDH_DV_HEX =
    "7f218201667f4e81de5f290100420d444543564341415430303030317f498190060a"
    "04007f00070202020205868181040a74972e84b7d2c428fbe46d40c92d6cb56ae6a4"
    "b0af5b8bc0927e5ef6f73220776d31693e36d685df6cf3763e41728d967dc1963f7c"
    "a70f0ea7adb5ea856d8a133b867b8ef4132e7ffa3e8f32cd0321fc22199b9323376e"
    "59d84a062491948117178bf4db6c0ee7a235bfc3d4459183d408232ac781c78cf5c5"
    "4e2ff35ba5165f200d444554455354445644453031397f4c12060904007f00070301"
    "02025305801fffff105f25060100000903005f24060100010003005f37818098c637"
    "befa63058921902896a605206d5be3bff2f7e258eb0bb06ea7e84dfbc98e35276110"
    "c684bb23d277350e3862e6a30520808f417b8911985880a556e1758c7fe6545b3da0"
    "3383bd7b5b51dec007139cbc44ff320f84f2b6ad44c7b28fcaeb2b7b98e3a2bae83"
    "22b115a8f10d48349281b4f4645c9aae307cfae9ad6b194";

// ECDH IS (AT): CHR=DETESTATDE019, CAR=DETESTDVDE019
// Algorithm: id-TA-ECDSA-SHA-512 (0.4.0.127.0.7.2.2.2.2.5)
inline const std::string ECDH_IS_HEX =
    "7f218201667f4e81de5f290100420d444554455354445644453031397f498190060a"
    "04007f00070202020205868181043385b484e8e994c93f55adf4a9c92da3c063ac7d"
    "744e85d38ee070bf6ff8d7aba2de688724b27d6bae2b1c8ae074c09bb8808cc83036"
    "b0692898711d3f6fc3213846e14b3154845084dfbc0eb70ae2bdea8eebd6796acfa4"
    "c56f5703ce5a6efe0a266e43c3f71a1b7eec60e5c6f0850dc4d3455f53282c9fbca"
    "85dc947d13bee5f200d444554455354415444453031397f4c12060904007f00070301"
    "0202530500000001105f25060100000903005f24060100010003005f37818034bb8c2"
    "80a7a593cf7c7e9f375ffffbfd92c4e3f6188a44824215e11ef47d03428cfc91936a"
    "2609c742ef92c5968bce65ba42aed7aad70b7b2a31201dc152cec930d7d7954bfbe0"
    "021ad1fd2acaa6c349a6c2fb86514b9f03dfab99871fbd990dd8416d3eda2883ebd0"
    "b94401a7dbfe16e5a1db643ee7c2fc0d5d77bae3666af";

// =============================================================================
// BSI TR-03110 EAC Worked Example — DH Certificate Chain
// Source: BSI TR-03110 Worked Example (2011), certs_dh/
// =============================================================================

// DH CVCA: CHR=DETESTCVCA00003, CAR=DETESTCVCA00003 (self-signed)
// Algorithm: id-TA-RSA-v1-5-SHA-1 (0.4.0.127.0.7.2.2.2.1.1)
inline const std::string DH_CVCA_HEX =
    "7f2182016a7f4e81e25f290100420f4445544553544356434130303030337f498194"
    "060a04007f00070202020101818180b5ada440f264803dbeec57ab1ffbd12b426874"
    "23ec299dd52a6bb0345193d1e2a5bbc11955908a08760f400fa03d56097b5f216564"
    "8d85071a80a1b66ffa9321526dda41e146d532991ca0c1dcfc8d0bbe298c1f0edb57"
    "400faed67e135d542471c82e351b06d43c946604fcbd5045056c4b8a7b98ec3872e4"
    "1bb3d7a591ccad82030100015f200f4445544553544356434130303030337f4c0e06"
    "0904007f0007030102015301c35f25060100000302045f24060101000301095f3781"
    "80a27ed830e99082e5813068b9b21662444ed1c8713aba71985b5288f376423608b8"
    "eec825173862f8df9f259bcd611c96a2dba86ac6c48a3310cfd6fade9b4bd02bb520"
    "367ad90b379f6d35ad01d406f8cb1d94d025730a98d7dfbe3b0f74183531de6e699d"
    "23652ce39067635a8d176c66064b79b7271a0ee600bac94ad51e4f";

// DH IS (AT): CHR=DETESTATDE019, CAR=DETESTDVDE019
// Algorithm: id-TA-RSA-v1-5-SHA-1 (0.4.0.127.0.7.2.2.2.1.1)
inline const std::string DH_IS_HEX =
    "7f2182016a7f4e81e25f290100420d444554455354445644453031397f498194060a"
    "04007f000702020201018181809f7ef68e153db4fd1084ddedbeae842c556d419fcb"
    "5ef621aa3751f0fc0cfd714fc0e768866b3f44e2725af0351a97edb1ba88dfdd9b4d"
    "81d408fe0763346a772cf64616465c8fd971b775d2e13426c5bc11894795c5ad2c3e"
    "426837f3a1019fe95124ea5d433e906d7993496321efcbdbc32d93c0680b45f3b8f6"
    "4a5dafcfb982030100015f200d444554455354415444453031397f4c12060904007f"
    "000703010202530500000001105f25060100000302045f24060100000402045f3781"
    "808cb16126a1fdbb8248c88bdb1fb1199c3f253856fe10835f7bff62a30bd281b8a1"
    "f0fe0381a5b0a42651f77df7215221f0ede488e689ea45cee20b19c7b1d1edb6ac21"
    "f34088819f6fd5dc333109e15a15dff685a2b69d17d5e23dafe363a8e76331cc25b9"
    "13fb6ed830eb457ad0a67396a190cae39cc6c2e4671e6052d3c22d";

// =============================================================================
// Invalid / malformed test data
// =============================================================================

// Valid TLV structure but not a CVC (wrong outer tag)
inline const std::string WRONG_TAG_HEX = "7f220101ff";

// Truncated CVC (cut off in the middle)
inline const std::string TRUNCATED_CVC_HEX = "7f218202f37f4e82";

// Empty data
inline const std::string EMPTY_HEX = "";

} // namespace cvc_test_helpers
