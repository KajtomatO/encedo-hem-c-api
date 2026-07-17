/*
 * <ehem/storage.h> — USB-MSC partition visibility (storage unlock/lock).
 *
 * PPA devices embed a microSD with a plaintext partition (disk 0) and an
 * encrypted partition (disk 1). "Unlock" exposes a partition to whatever
 * host the device's USB port is plugged into; "lock" withdraws it. Neither
 * is a cryptographic operation — the encrypted partition's key material is
 * already live once the device is initialised; these calls only flip USB-MSC
 * visibility flags (decryption happens transparently in the hardware XTS
 * engine).
 *
 * UNUSUAL SCOPE MODEL: the firmware reads the disk index and access mode
 * from the JWT SCOPE STRING, not from the URL — scope "storage:disk0" grants
 * read-only disk 0, "storage:disk1:rw" read-write disk 1. The SDK composes
 * the scope from the arguments; the scope-keyed token cache makes each
 * (disk, mode) pair its own cached token.
 *
 * PPA-only routes: an EPA build answers 404 → EHEM_ERR_NOT_FOUND.
 *
 * implements: REQ-SYS-010
 */
#ifndef EHEM_STORAGE_H
#define EHEM_STORAGE_H

#include "ehem/ehem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Expose disk `disk` (0 = plaintext, 1 = encrypted; 2 exists only on DEBUG
 * firmware builds) over USB-MSC: GET /api/storage/unlock with scope
 * "storage:disk<N>" (writable == 0, read-only) or "storage:disk<N>:rw"
 * (writable != 0). The SDK uses the plain URL path — the mode rides in the
 * scope, the form the HEM test suite proves. Returns EHEM_OK on the empty
 * 200. EHEM_ERR_ARG (no I/O) when disk is outside 0..2; 406 (disk not
 * supported by this build / mode not grantable) → EHEM_ERR_DEVICE; 403 →
 * EHEM_ERR_SCOPE_DENIED; 409 (fls_state, uninitialised, formatting) →
 * EHEM_ERR_DEVICE; EPA 404 → EHEM_ERR_NOT_FOUND.
 */
EHEM_API ehem_rc ehem_storage_unlock(ehem_ctx *ctx, int disk, int writable);

/*
 * Withdraw disk `disk` from USB-MSC: GET /api/storage/lock with scope
 * "storage:disk<N>". Same argument, return, and error contract as unlock
 * (no mode — locking is unconditional). Locking does not re-encrypt or zero
 * anything; it only hides the partition from the host.
 */
EHEM_API ehem_rc ehem_storage_lock(ehem_ctx *ctx, int disk);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_STORAGE_H */
