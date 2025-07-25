/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <keymint_support/attestation_record.h>

#include <assert.h>

#include <aidl/android/hardware/security/keymint/Tag.h>
#include <aidl/android/hardware/security/keymint/TagType.h>

#include <android-base/logging.h>

#include <openssl/asn1t.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <keymint_support/authorization_set.h>
#include <keymint_support/openssl_utils.h>

#define AT __FILE__ ":" << __LINE__

namespace aidl::android::hardware::security::keymint {

struct stack_st_ASN1_TYPE_Delete {
    void operator()(stack_st_ASN1_TYPE* p) { sk_ASN1_TYPE_free(p); }
};

struct ASN1_STRING_Delete {
    void operator()(ASN1_STRING* p) { ASN1_STRING_free(p); }
};

struct ASN1_TYPE_Delete {
    void operator()(ASN1_TYPE* p) { ASN1_TYPE_free(p); }
};

#define ASN1_INTEGER_SET STACK_OF(ASN1_INTEGER)

typedef struct km_root_of_trust {
    ASN1_OCTET_STRING* verified_boot_key;
    ASN1_BOOLEAN device_locked;
    ASN1_ENUMERATED* verified_boot_state;
    ASN1_OCTET_STRING* verified_boot_hash;
} KM_ROOT_OF_TRUST;

ASN1_SEQUENCE(KM_ROOT_OF_TRUST) = {
        ASN1_SIMPLE(KM_ROOT_OF_TRUST, verified_boot_key, ASN1_OCTET_STRING),
        ASN1_SIMPLE(KM_ROOT_OF_TRUST, device_locked, ASN1_BOOLEAN),
        ASN1_SIMPLE(KM_ROOT_OF_TRUST, verified_boot_state, ASN1_ENUMERATED),
        ASN1_SIMPLE(KM_ROOT_OF_TRUST, verified_boot_hash, ASN1_OCTET_STRING),
} ASN1_SEQUENCE_END(KM_ROOT_OF_TRUST);
IMPLEMENT_ASN1_FUNCTIONS(KM_ROOT_OF_TRUST);

// Fields ordered in tag order.
typedef struct km_auth_list {
    ASN1_INTEGER_SET* purpose;
    ASN1_INTEGER* algorithm;
    ASN1_INTEGER* key_size;
    ASN1_INTEGER_SET* digest;
    ASN1_INTEGER_SET* padding;
    ASN1_INTEGER* ec_curve;
    ASN1_INTEGER* rsa_public_exponent;
    ASN1_INTEGER_SET* mgf_digest;
    ASN1_NULL* rollback_resistance;
    ASN1_NULL* early_boot_only;
    ASN1_INTEGER* active_date_time;
    ASN1_INTEGER* origination_expire_date_time;
    ASN1_INTEGER* usage_expire_date_time;
    ASN1_INTEGER* usage_count_limit;
    ASN1_NULL* no_auth_required;
    ASN1_INTEGER* user_auth_type;
    ASN1_INTEGER* auth_timeout;
    ASN1_NULL* allow_while_on_body;
    ASN1_NULL* trusted_user_presence_required;
    ASN1_NULL* trusted_confirmation_required;
    ASN1_NULL* unlocked_device_required;
    ASN1_INTEGER* creation_date_time;
    ASN1_INTEGER* origin;
    KM_ROOT_OF_TRUST* root_of_trust;
    ASN1_INTEGER* os_version;
    ASN1_INTEGER* os_patchlevel;
    ASN1_OCTET_STRING* attestation_application_id;
    ASN1_OCTET_STRING* attestation_id_brand;
    ASN1_OCTET_STRING* attestation_id_device;
    ASN1_OCTET_STRING* attestation_id_product;
    ASN1_OCTET_STRING* attestation_id_serial;
    ASN1_OCTET_STRING* attestation_id_imei;
    ASN1_OCTET_STRING* attestation_id_meid;
    ASN1_OCTET_STRING* attestation_id_manufacturer;
    ASN1_OCTET_STRING* attestation_id_model;
    ASN1_INTEGER* vendor_patchlevel;
    ASN1_INTEGER* boot_patchlevel;
    ASN1_NULL* device_unique_attestation;
    ASN1_NULL* identity_credential;
    ASN1_OCTET_STRING* attestation_id_second_imei;
    ASN1_OCTET_STRING* module_hash;
} KM_AUTH_LIST;

ASN1_SEQUENCE(KM_AUTH_LIST) = {
        ASN1_EXP_SET_OF_OPT(KM_AUTH_LIST, purpose, ASN1_INTEGER, TAG_PURPOSE.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, algorithm, ASN1_INTEGER, TAG_ALGORITHM.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, key_size, ASN1_INTEGER, TAG_KEY_SIZE.maskedTag()),
        ASN1_EXP_SET_OF_OPT(KM_AUTH_LIST, digest, ASN1_INTEGER, TAG_DIGEST.maskedTag()),
        ASN1_EXP_SET_OF_OPT(KM_AUTH_LIST, padding, ASN1_INTEGER, TAG_PADDING.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, ec_curve, ASN1_INTEGER, TAG_EC_CURVE.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, rsa_public_exponent, ASN1_INTEGER,
                     TAG_RSA_PUBLIC_EXPONENT.maskedTag()),
        ASN1_EXP_SET_OF_OPT(KM_AUTH_LIST, mgf_digest, ASN1_INTEGER,
                            TAG_RSA_OAEP_MGF_DIGEST.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, rollback_resistance, ASN1_NULL,
                     TAG_ROLLBACK_RESISTANCE.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, early_boot_only, ASN1_NULL, TAG_EARLY_BOOT_ONLY.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, active_date_time, ASN1_INTEGER, TAG_ACTIVE_DATETIME.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, origination_expire_date_time, ASN1_INTEGER,
                     TAG_ORIGINATION_EXPIRE_DATETIME.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, usage_expire_date_time, ASN1_INTEGER,
                     TAG_USAGE_EXPIRE_DATETIME.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, usage_count_limit, ASN1_INTEGER,
                     TAG_USAGE_COUNT_LIMIT.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, no_auth_required, ASN1_NULL, TAG_NO_AUTH_REQUIRED.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, user_auth_type, ASN1_INTEGER, TAG_USER_AUTH_TYPE.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, auth_timeout, ASN1_INTEGER, TAG_AUTH_TIMEOUT.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, allow_while_on_body, ASN1_NULL,
                     TAG_ALLOW_WHILE_ON_BODY.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, trusted_user_presence_required, ASN1_NULL,
                     TAG_TRUSTED_USER_PRESENCE_REQUIRED.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, trusted_confirmation_required, ASN1_NULL,
                     TAG_TRUSTED_CONFIRMATION_REQUIRED.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, unlocked_device_required, ASN1_NULL,
                     TAG_UNLOCKED_DEVICE_REQUIRED.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, creation_date_time, ASN1_INTEGER,
                     TAG_CREATION_DATETIME.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, origin, ASN1_INTEGER, TAG_ORIGIN.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, root_of_trust, KM_ROOT_OF_TRUST, TAG_ROOT_OF_TRUST.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, os_version, ASN1_INTEGER, TAG_OS_VERSION.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, os_patchlevel, ASN1_INTEGER, TAG_OS_PATCHLEVEL.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_application_id, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_APPLICATION_ID.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_brand, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_BRAND.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_device, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_DEVICE.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_product, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_PRODUCT.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_serial, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_SERIAL.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_imei, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_IMEI.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_meid, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_MEID.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_manufacturer, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_MANUFACTURER.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_model, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_MODEL.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, vendor_patchlevel, ASN1_INTEGER,
                     TAG_VENDOR_PATCHLEVEL.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, boot_patchlevel, ASN1_INTEGER, TAG_BOOT_PATCHLEVEL.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, device_unique_attestation, ASN1_NULL,
                     TAG_DEVICE_UNIQUE_ATTESTATION.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, identity_credential, ASN1_NULL,
                     TAG_IDENTITY_CREDENTIAL_KEY.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, attestation_id_second_imei, ASN1_OCTET_STRING,
                     TAG_ATTESTATION_ID_SECOND_IMEI.maskedTag()),
        ASN1_EXP_OPT(KM_AUTH_LIST, module_hash, ASN1_OCTET_STRING, TAG_MODULE_HASH.maskedTag()),
} ASN1_SEQUENCE_END(KM_AUTH_LIST);
IMPLEMENT_ASN1_FUNCTIONS(KM_AUTH_LIST);

typedef struct km_key_description {
    ASN1_INTEGER* attestation_version;
    ASN1_ENUMERATED* attestation_security_level;
    ASN1_INTEGER* keymint_version;
    ASN1_ENUMERATED* keymint_security_level;
    ASN1_OCTET_STRING* attestation_challenge;
    ASN1_INTEGER* unique_id;
    KM_AUTH_LIST* software_enforced;
    KM_AUTH_LIST* tee_enforced;
} KM_KEY_DESCRIPTION;

ASN1_SEQUENCE(KM_KEY_DESCRIPTION) = {
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, attestation_version, ASN1_INTEGER),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, attestation_security_level, ASN1_ENUMERATED),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, keymint_version, ASN1_INTEGER),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, keymint_security_level, ASN1_ENUMERATED),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, attestation_challenge, ASN1_OCTET_STRING),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, unique_id, ASN1_OCTET_STRING),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, software_enforced, KM_AUTH_LIST),
        ASN1_SIMPLE(KM_KEY_DESCRIPTION, tee_enforced, KM_AUTH_LIST),
} ASN1_SEQUENCE_END(KM_KEY_DESCRIPTION);
IMPLEMENT_ASN1_FUNCTIONS(KM_KEY_DESCRIPTION);

template <Tag tag>
void copyAuthTag(const stack_st_ASN1_INTEGER* stack, TypedTag<TagType::ENUM_REP, tag> ttag,
                 AuthorizationSet* auth_list) {
    typedef typename TypedTag2ValueType<decltype(ttag)>::type ValueT;
    for (size_t i = 0; i < sk_ASN1_INTEGER_num(stack); ++i) {
        auth_list->push_back(
                ttag, static_cast<ValueT>(ASN1_INTEGER_get(sk_ASN1_INTEGER_value(stack, i))));
    }
}

template <Tag tag>
void copyAuthTag(const ASN1_INTEGER* asn1_int, TypedTag<TagType::ENUM, tag> ttag,
                 AuthorizationSet* auth_list) {
    typedef typename TypedTag2ValueType<decltype(ttag)>::type ValueT;
    if (!asn1_int) return;
    auth_list->push_back(ttag, static_cast<ValueT>(ASN1_INTEGER_get(asn1_int)));
}

template <Tag tag>
void copyAuthTag(const ASN1_INTEGER* asn1_int, TypedTag<TagType::UINT, tag> ttag,
                 AuthorizationSet* auth_list) {
    if (!asn1_int) return;
    auth_list->push_back(ttag, ASN1_INTEGER_get(asn1_int));
}

BIGNUM* construct_uint_max() {
    BIGNUM* value = BN_new();
    BIGNUM_Ptr one(BN_new());
    BN_one(one.get());
    BN_lshift(value, one.get(), 32);
    return value;
}

uint64_t BignumToUint64(BIGNUM* num) {
    static_assert((sizeof(BN_ULONG) == sizeof(uint32_t)) || (sizeof(BN_ULONG) == sizeof(uint64_t)),
                  "This implementation only supports 32 and 64-bit BN_ULONG");
    if (sizeof(BN_ULONG) == sizeof(uint32_t)) {
        BIGNUM_Ptr uint_max(construct_uint_max());
        BIGNUM_Ptr hi(BN_new()), lo(BN_new());
        BN_CTX_Ptr ctx(BN_CTX_new());
        BN_div(hi.get(), lo.get(), num, uint_max.get(), ctx.get());
        return static_cast<uint64_t>(BN_get_word(hi.get())) << 32 | BN_get_word(lo.get());
    } else if (sizeof(BN_ULONG) == sizeof(uint64_t)) {
        return BN_get_word(num);
    } else {
        return 0;
    }
}

template <Tag tag>
void copyAuthTag(const ASN1_INTEGER* asn1_int, TypedTag<TagType::ULONG, tag> ttag,
                 AuthorizationSet* auth_list) {
    if (!asn1_int) return;
    BIGNUM_Ptr num(ASN1_INTEGER_to_BN(asn1_int, nullptr));
    auth_list->push_back(ttag, BignumToUint64(num.get()));
}

template <Tag tag>
void copyAuthTag(const ASN1_INTEGER* asn1_int, TypedTag<TagType::DATE, tag> ttag,
                 AuthorizationSet* auth_list) {
    if (!asn1_int) return;
    BIGNUM_Ptr num(ASN1_INTEGER_to_BN(asn1_int, nullptr));
    auth_list->push_back(ttag, BignumToUint64(num.get()));
}

template <Tag tag>
void copyAuthTag(const ASN1_NULL* asn1_null, TypedTag<TagType::BOOL, tag> ttag,
                 AuthorizationSet* auth_list) {
    if (!asn1_null) return;
    auth_list->push_back(ttag);
}

template <Tag tag>
void copyAuthTag(const ASN1_OCTET_STRING* asn1_string, TypedTag<TagType::BYTES, tag> ttag,
                 AuthorizationSet* auth_list) {
    if (!asn1_string) return;
    vector<uint8_t> buf(asn1_string->data, asn1_string->data + asn1_string->length);
    auth_list->push_back(ttag, buf);
}

// Extract the values from the specified ASN.1 record and place them in auth_list.
// Does nothing with root-of-trust field.
static ErrorCode extract_auth_list(const KM_AUTH_LIST* record, AuthorizationSet* auth_list) {
    if (!record) return ErrorCode::OK;

    // Fields ordered in tag order.
    copyAuthTag(record->purpose, TAG_PURPOSE, auth_list);
    copyAuthTag(record->algorithm, TAG_ALGORITHM, auth_list);
    copyAuthTag(record->key_size, TAG_KEY_SIZE, auth_list);
    copyAuthTag(record->digest, TAG_DIGEST, auth_list);
    copyAuthTag(record->padding, TAG_PADDING, auth_list);
    copyAuthTag(record->ec_curve, TAG_EC_CURVE, auth_list);
    copyAuthTag(record->rsa_public_exponent, TAG_RSA_PUBLIC_EXPONENT, auth_list);
    copyAuthTag(record->mgf_digest, TAG_RSA_OAEP_MGF_DIGEST, auth_list);
    copyAuthTag(record->rollback_resistance, TAG_ROLLBACK_RESISTANCE, auth_list);
    copyAuthTag(record->early_boot_only, TAG_EARLY_BOOT_ONLY, auth_list);
    copyAuthTag(record->active_date_time, TAG_ACTIVE_DATETIME, auth_list);
    copyAuthTag(record->origination_expire_date_time, TAG_ORIGINATION_EXPIRE_DATETIME, auth_list);
    copyAuthTag(record->usage_expire_date_time, TAG_USAGE_EXPIRE_DATETIME, auth_list);
    copyAuthTag(record->usage_count_limit, TAG_USAGE_COUNT_LIMIT, auth_list);
    copyAuthTag(record->no_auth_required, TAG_NO_AUTH_REQUIRED, auth_list);
    copyAuthTag(record->user_auth_type, TAG_USER_AUTH_TYPE, auth_list);
    copyAuthTag(record->auth_timeout, TAG_AUTH_TIMEOUT, auth_list);
    copyAuthTag(record->allow_while_on_body, TAG_ALLOW_WHILE_ON_BODY, auth_list);
    copyAuthTag(record->trusted_user_presence_required, TAG_TRUSTED_USER_PRESENCE_REQUIRED,
                auth_list);
    copyAuthTag(record->trusted_confirmation_required, TAG_TRUSTED_CONFIRMATION_REQUIRED,
                auth_list);
    copyAuthTag(record->unlocked_device_required, TAG_UNLOCKED_DEVICE_REQUIRED, auth_list);
    copyAuthTag(record->creation_date_time, TAG_CREATION_DATETIME, auth_list);
    copyAuthTag(record->origin, TAG_ORIGIN, auth_list);
    // root_of_trust dealt with separately
    copyAuthTag(record->os_version, TAG_OS_VERSION, auth_list);
    copyAuthTag(record->os_patchlevel, TAG_OS_PATCHLEVEL, auth_list);
    copyAuthTag(record->attestation_application_id, TAG_ATTESTATION_APPLICATION_ID, auth_list);
    copyAuthTag(record->attestation_id_brand, TAG_ATTESTATION_ID_BRAND, auth_list);
    copyAuthTag(record->attestation_id_device, TAG_ATTESTATION_ID_DEVICE, auth_list);
    copyAuthTag(record->attestation_id_product, TAG_ATTESTATION_ID_PRODUCT, auth_list);
    copyAuthTag(record->attestation_id_serial, TAG_ATTESTATION_ID_SERIAL, auth_list);
    copyAuthTag(record->attestation_id_imei, TAG_ATTESTATION_ID_IMEI, auth_list);
    copyAuthTag(record->attestation_id_meid, TAG_ATTESTATION_ID_MEID, auth_list);
    copyAuthTag(record->attestation_id_manufacturer, TAG_ATTESTATION_ID_MANUFACTURER, auth_list);
    copyAuthTag(record->attestation_id_model, TAG_ATTESTATION_ID_MODEL, auth_list);
    copyAuthTag(record->vendor_patchlevel, TAG_VENDOR_PATCHLEVEL, auth_list);
    copyAuthTag(record->boot_patchlevel, TAG_BOOT_PATCHLEVEL, auth_list);
    copyAuthTag(record->device_unique_attestation, TAG_DEVICE_UNIQUE_ATTESTATION, auth_list);
    copyAuthTag(record->identity_credential, TAG_IDENTITY_CREDENTIAL_KEY, auth_list);
    copyAuthTag(record->attestation_id_second_imei, TAG_ATTESTATION_ID_SECOND_IMEI, auth_list);
    copyAuthTag(record->module_hash, TAG_MODULE_HASH, auth_list);

    return ErrorCode::OK;
}

MAKE_OPENSSL_PTR_TYPE(KM_KEY_DESCRIPTION)

// Parse the DER-encoded attestation record, placing the results in keymint_version,
// attestation_challenge, software_enforced, tee_enforced and unique_id.
ErrorCode parse_attestation_record(const uint8_t* asn1_key_desc, size_t asn1_key_desc_len,
                                   uint32_t* attestation_version,  //
                                   SecurityLevel* attestation_security_level,
                                   uint32_t* keymint_version, SecurityLevel* keymint_security_level,
                                   vector<uint8_t>* attestation_challenge,
                                   AuthorizationSet* software_enforced,
                                   AuthorizationSet* tee_enforced,  //
                                   vector<uint8_t>* unique_id) {
    const uint8_t* p = asn1_key_desc;
    KM_KEY_DESCRIPTION_Ptr record(d2i_KM_KEY_DESCRIPTION(nullptr, &p, asn1_key_desc_len));
    if (!record.get()) return ErrorCode::UNKNOWN_ERROR;

    *attestation_version = ASN1_INTEGER_get(record->attestation_version);
    *attestation_security_level =
            static_cast<SecurityLevel>(ASN1_ENUMERATED_get(record->attestation_security_level));
    *keymint_version = ASN1_INTEGER_get(record->keymint_version);
    *keymint_security_level =
            static_cast<SecurityLevel>(ASN1_ENUMERATED_get(record->keymint_security_level));

    auto& chall = record->attestation_challenge;
    attestation_challenge->resize(chall->length);
    memcpy(attestation_challenge->data(), chall->data, chall->length);
    auto& uid = record->unique_id;
    unique_id->resize(uid->length);
    memcpy(unique_id->data(), uid->data, uid->length);

    ErrorCode error = extract_auth_list(record->software_enforced, software_enforced);
    if (error != ErrorCode::OK) return error;

    return extract_auth_list(record->tee_enforced, tee_enforced);
}

ErrorCode parse_root_of_trust(const uint8_t* asn1_key_desc, size_t asn1_key_desc_len,
                              vector<uint8_t>* verified_boot_key, VerifiedBoot* verified_boot_state,
                              bool* device_locked, vector<uint8_t>* verified_boot_hash) {
    if (!verified_boot_key || !verified_boot_state || !device_locked || !verified_boot_hash) {
        LOG(ERROR) << AT << "null pointer input(s)";
        return ErrorCode::INVALID_ARGUMENT;
    }
    const uint8_t* p = asn1_key_desc;
    KM_KEY_DESCRIPTION_Ptr record(d2i_KM_KEY_DESCRIPTION(nullptr, &p, asn1_key_desc_len));
    if (!record.get()) {
        LOG(ERROR) << AT << "Failed record parsing";
        return ErrorCode::UNKNOWN_ERROR;
    }

    KM_ROOT_OF_TRUST* root_of_trust = nullptr;
    if (record->tee_enforced && record->tee_enforced->root_of_trust) {
        root_of_trust = record->tee_enforced->root_of_trust;
    } else if (record->software_enforced && record->software_enforced->root_of_trust) {
        root_of_trust = record->software_enforced->root_of_trust;
    } else {
        LOG(ERROR) << AT << " Failed root of trust parsing";
        return ErrorCode::INVALID_ARGUMENT;
    }
    if (!root_of_trust->verified_boot_key) {
        LOG(ERROR) << AT << " Failed verified boot key parsing";
        return ErrorCode::INVALID_ARGUMENT;
    }

    auto& vb_key = root_of_trust->verified_boot_key;
    verified_boot_key->resize(vb_key->length);
    memcpy(verified_boot_key->data(), vb_key->data, vb_key->length);

    *verified_boot_state =
            static_cast<VerifiedBoot>(ASN1_ENUMERATED_get(root_of_trust->verified_boot_state));
    if (!verified_boot_state) {
        LOG(ERROR) << AT << " Failed verified boot state parsing";
        return ErrorCode::INVALID_ARGUMENT;
    }

    *device_locked = root_of_trust->device_locked;
    if (!device_locked) {
        LOG(ERROR) << AT << " Failed device locked parsing";
        return ErrorCode::INVALID_ARGUMENT;
    }

    auto& vb_hash = root_of_trust->verified_boot_hash;
    if (!vb_hash) {
        LOG(ERROR) << AT << " Failed verified boot hash parsing";
        return ErrorCode::INVALID_ARGUMENT;
    }
    verified_boot_hash->resize(vb_hash->length);
    memcpy(verified_boot_hash->data(), vb_hash->data, vb_hash->length);
    return ErrorCode::OK;  // KM_ERROR_OK;
}

}  // namespace aidl::android::hardware::security::keymint
