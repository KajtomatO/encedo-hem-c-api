/* hem.h
 *
 * Backend seam between the PKCS#11 layer and the Encedo HEM device.
 *
 * A dedicated Encedo HEM C SDK is planned. Until it is available every
 * function below is a TBD stub in hem.c. The PKCS#11 layer (library.c,
 * slot.c, session.c, object.c, crypto.c) is complete and calls only this
 * interface, so wiring in the SDK later means touching hem.c alone.
 *
 * Copyright (c) 2026 Krzysztof Rutecki
 * SPDX-License-Identifier: MIT
 */

#ifndef EP11_HEM_H
#define EP11_HEM_H

#include "ep11.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Device / token ----------------------------------------------------- */

/* Called from C_Initialize / C_Finalize. */
CK_RV hem_connect(void);
void  hem_disconnect(void);

/* Fill pInfo with live device data (label, serial, flags, PIN limits).
 * Returning CKR_FUNCTION_NOT_SUPPORTED makes C_GetTokenInfo fall back to
 * static placeholder data. */
CK_RV hem_get_token_info(CK_TOKEN_INFO_PTR pInfo);

/* --- Authentication ----------------------------------------------------- */

CK_RV hem_login(CK_USER_TYPE userType, CK_UTF8CHAR_PTR pPin,
                CK_ULONG ulPinLen);
CK_RV hem_logout(void);

/* --- Objects (keys, certificates) ---------------------------------------
 *
 * CK_OBJECT_HANDLE values map to HEM key DESCR entries. The handle <->
 * DESCR table is owned by hem.c; handles stay valid for the lifetime of
 * the library (C_Initialize..C_Finalize), 0 is never used
 * (CK_INVALID_HANDLE). */

CK_RV hem_create_object(CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                        CK_OBJECT_HANDLE_PTR phObject);
CK_RV hem_destroy_object(CK_OBJECT_HANDLE hObject);
CK_RV hem_get_attribute_value(CK_OBJECT_HANDLE hObject,
                              CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV hem_set_attribute_value(CK_OBJECT_HANDLE hObject,
                              CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV hem_find_objects_init(CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount);
CK_RV hem_find_objects(CK_OBJECT_HANDLE_PTR phObjects, CK_ULONG ulMaxCount,
                       CK_ULONG_PTR pulCount);
CK_RV hem_find_objects_final(void);

/* --- Key management ------------------------------------------------------ */

/* AES / generic secret (HMAC) keys. */
CK_RV hem_generate_key(CK_MECHANISM_PTR pMechanism, CK_ATTRIBUTE_PTR pTemplate,
                       CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phKey);

/* ECDSA (secp256r1/384r1/521r1/256k1), Ed25519/Ed448, X25519/X448,
 * ML-KEM 512/768/1024, ML-DSA 44/65/87. */
CK_RV hem_generate_key_pair(CK_MECHANISM_PTR pMechanism,
                            CK_ATTRIBUTE_PTR pPublicTemplate,
                            CK_ULONG ulPublicCount,
                            CK_ATTRIBUTE_PTR pPrivateTemplate,
                            CK_ULONG ulPrivateCount,
                            CK_OBJECT_HANDLE_PTR phPublicKey,
                            CK_OBJECT_HANDLE_PTR phPrivateKey);

/* ECDH (CKM_ECDH1_DERIVE) on all supported curves. */
CK_RV hem_derive_key(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hBaseKey,
                     CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                     CK_OBJECT_HANDLE_PTR phKey);

/* --- Cryptographic operations (single-part) ------------------------------ */

/* ECDSA, EdDSA, ML-DSA, HMAC (SHA-2/SHA-3). */
CK_RV hem_sign(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
               CK_BYTE_PTR pData, CK_ULONG ulDataLen,
               CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen);
CK_RV hem_verify(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                 CK_BYTE_PTR pData, CK_ULONG ulDataLen,
                 CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen);

/* AES. */
CK_RV hem_encrypt(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                  CK_BYTE_PTR pData, CK_ULONG ulDataLen,
                  CK_BYTE_PTR pEncryptedData, CK_ULONG_PTR pulEncryptedDataLen);
CK_RV hem_decrypt(CK_MECHANISM_TYPE mechanism, CK_OBJECT_HANDLE hKey,
                  CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen,
                  CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen);

/* ML-KEM; exposed once the PKCS#11 3.2 interface (C_EncapsulateKey /
 * C_DecapsulateKey) is added to the function list. */
CK_RV hem_encapsulate(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hPublicKey,
                      CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                      CK_BYTE_PTR pCiphertext, CK_ULONG_PTR pulCiphertextLen,
                      CK_OBJECT_HANDLE_PTR phKey);
CK_RV hem_decapsulate(CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hPrivateKey,
                      CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount,
                      CK_BYTE_PTR pCiphertext, CK_ULONG ulCiphertextLen,
                      CK_OBJECT_HANDLE_PTR phKey);

/* --- Random -------------------------------------------------------------- */

CK_RV hem_generate_random(CK_BYTE_PTR pRandomData, CK_ULONG ulRandomLen);

#ifdef __cplusplus
}
#endif

#endif /* EP11_HEM_H */
