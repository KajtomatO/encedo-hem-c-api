#ifndef HEM_STORAGE_H
#define HEM_STORAGE_H

#include "hem_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GET /api/storage/unlock
 * Scope: determined by `scope` parameter  (authenticated automatically)
 *
 * Unlocks a storage disk (PPA only).  The disk and access mode are
 * determined by the scope:
 *   "storage:disk0:rw"  --  disk 0, read-write
 *   "storage:disk0:ro"  --  disk 0, read-only
 *   "storage:disk1:rw"  --  disk 1, read-write
 *   "storage:disk1:ro"  --  disk 1, read-only
 *
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 */
hem_error_t hem_storage_unlock(hem_ctx_t *ctx, const char *scope);

/*
 * GET /api/storage/lock
 * Scope: determined by `scope` parameter  (authenticated automatically)
 *
 * Locks a storage disk (PPA only).  Use a read-write scope:
 *   "storage:disk0:rw"  --  lock disk 0
 *   "storage:disk1:rw"  --  lock disk 1
 *
 * Returns HEM_ERR_HTTP_STATUS (404) on EPA.
 */
hem_error_t hem_storage_lock(hem_ctx_t *ctx, const char *scope);

#ifdef __cplusplus
}
#endif

#endif /* HEM_STORAGE_H */
