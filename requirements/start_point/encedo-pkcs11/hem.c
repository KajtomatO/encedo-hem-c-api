/* hem.c
 *
 * Encedo HEM backend — TBD.
 *
 * Every function here will be implemented on top of the dedicated
 * Encedo HEM C SDK once it is available. Until then:
 *   - lifecycle stubs (connect/disconnect) succeed so the module loads,
 *   - object enumeration reports an empty token,
 *   - everything else returns CKR_FUNCTION_NOT_SUPPORTED.
 *
 * Copyright (c) 2026 Krzysztof Rutecki
 * SPDX-License-Identifier: MIT
 */

#include "hem.h"

CK_RV hem_connect(void)
{
    /* TBD: HEM SDK — open device / establish authenticated channel. */
    return CKR_OK;
}

void hem_disconnect(void)
{
    /* TBD: HEM SDK — close device connection. */
}

CK_RV hem_get_token_info(CK_TOKEN_INFO_PTR pInfo)
{
    /* TBD: HEM SDK — read device label, serial number, auth state.
     * C_GetTokenInfo falls back to placeholders while unsupported. */
    (void)pInfo;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_login(CK_USER_TYPE userType, CK_UTF8CHAR_PTR pPin,
                CK_ULONG ulPinLen)
{
    /* TBD: HEM SDK — authenticate (map PIN to HEM auth credential). */
    (void)userType; (void)pPin; (void)ulPinLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_logout(void)
{
    /* TBD: HEM SDK — drop authenticated state. */
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_create_object(CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                        CK_OBJECT_HANDLE_PTR phObject)
{
    /* TBD: HEM SDK — import key/certificate object. */
    (void)pTemplate; (void)ulCount; (void)phObject;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_destroy_object(CK_OBJECT_HANDLE hObject)
{
    /* TBD: HEM SDK — delete object. */
    (void)hObject;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_get_attribute_value(CK_OBJECT_HANDLE hObject,
                              CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    /* TBD: HEM SDK — read object attributes (class, key type, id, label,
     * public key material, ...). */
    (void)hObject; (void)pTemplate; (void)ulCount;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_set_attribute_value(CK_OBJECT_HANDLE hObject,
                              CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    /* TBD: HEM SDK — update mutable attributes (e.g. CKA_LABEL). */
    (void)hObject; (void)pTemplate; (void)ulCount;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_find_objects_init(CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
    /* TBD: HEM SDK — start object enumeration matching template. */
    (void)pTemplate; (void)ulCount;
    return CKR_OK;
}

CK_RV hem_find_objects(CK_OBJECT_HANDLE_PTR phObjects, CK_ULONG ulMaxCount,
                       CK_ULONG_PTR pulCount)
{
    /* TBD: HEM SDK — return next matching handles. Empty token for now. */
    (void)phObjects; (void)ulMaxCount;
    *pulCount = 0;
    return CKR_OK;
}

CK_RV hem_find_objects_final(void)
{
    /* TBD: HEM SDK — end object enumeration. */
    return CKR_OK;
}

CK_RV hem_generate_key(CK_MECHANISM_PTR pMechanism, CK_ATTRIBUTE_PTR pTemplate,
                       CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phKey)
{
    /* TBD: HEM SDK — generate AES / generic secret (HMAC) key. */
    (void)pMechanism; (void)pTemplate; (void)ulCount; (void)phKey;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_generate_key_pair(CK_MECHANISM_PTR pMechanism,
                            CK_ATTRIBUTE_PTR pPublicTemplate,
                            CK_ULONG ulPublicCount,
                            CK_ATTRIBUTE_PTR pPrivateTemplate,
                            CK_ULONG ulPrivateCount,
                            CK_OBJECT_HANDLE_PTR phPublicKey,
                            CK_OBJECT_HANDLE_PTR phPrivateKey)
{
    /* TBD: HEM SDK — generate EC/EdDSA/X25519/ML-KEM/ML-DSA key pair. */
    (void)pMechanism;
    (void)pPublicTemplate; (void)ulPublicCount;
    (void)pPrivateTemplate; (void)ulPrivateCount;
    (void)phPublicKey; (void)phPrivateKey;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_derive_key(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hBaseKey,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phKey)
{
    /* TBD: HEM SDK — ECDH derive. */
    (void)pMechanism; (void)hBaseKey; (void)pTemplate; (void)ulCount;
    (void)phKey;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_sign(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
               CK_BYTE_PTR pData, CK_ULONG ulDataLen,
               CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
    /* TBD: HEM SDK — sign (ECDSA/EdDSA/ML-DSA/HMAC). Must support the
     * length-query convention: pSignature == NULL -> set *pulSignatureLen. */
    (void)mechanism; (void)hKey; (void)pData; (void)ulDataLen;
    (void)pSignature; (void)pulSignatureLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_verify(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                 CK_BYTE_PTR pData, CK_ULONG ulDataLen,
                 CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
    /* TBD: HEM SDK — verify signature. */
    (void)mechanism; (void)hKey; (void)pData; (void)ulDataLen;
    (void)pSignature; (void)ulSignatureLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_encrypt(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                  CK_BYTE_PTR pData, CK_ULONG ulDataLen,
                  CK_BYTE_PTR pEncryptedData, CK_ULONG_PTR pulEncryptedDataLen)
{
    /* TBD: HEM SDK — AES encrypt. */
    (void)mechanism; (void)hKey; (void)pData; (void)ulDataLen;
    (void)pEncryptedData; (void)pulEncryptedDataLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_decrypt(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                  CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen,
                  CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
    /* TBD: HEM SDK — AES decrypt. */
    (void)mechanism; (void)hKey; (void)pEncryptedData; (void)ulEncryptedDataLen;
    (void)pData; (void)pulDataLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_encapsulate(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hPublicKey,
                      CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                      CK_BYTE_PTR pCiphertext, CK_ULONG_PTR pulCiphertextLen,
                      CK_OBJECT_HANDLE_PTR phKey)
{
    /* TBD: HEM SDK — ML-KEM encapsulate. */
    (void)pMechanism; (void)hPublicKey; (void)pTemplate; (void)ulCount;
    (void)pCiphertext; (void)pulCiphertextLen; (void)phKey;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_decapsulate(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hPrivateKey,
                      CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                      CK_BYTE_PTR pCiphertext, CK_ULONG ulCiphertextLen,
                      CK_OBJECT_HANDLE_PTR phKey)
{
    /* TBD: HEM SDK — ML-KEM decapsulate. */
    (void)pMechanism; (void)hPrivateKey; (void)pTemplate; (void)ulCount;
    (void)pCiphertext; (void)ulCiphertextLen; (void)phKey;
    return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_RV hem_generate_random(CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen)
{
    /* TBD: HEM SDK — hardware RNG. */
    (void)pRandomData; (void)ulRandomLen;
    return CKR_FUNCTION_NOT_SUPPORTED;
}
