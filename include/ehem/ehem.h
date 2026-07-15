/*
 * ehem.h — Encedo HEM C SDK, top-level public header.
 *
 * Part of encedo-hem-c-api. MIT licensed, written from scratch
 * (ARCHITECTURE.md §1). Public symbols all carry the `ehem_` prefix.
 */
#ifndef EHEM_H
#define EHEM_H

/*
 * EHEM_API — symbol-visibility / export-control macro.
 * implements: REQ-API-006
 *
 * Default visibility is hidden (the library is compiled with
 * -fvisibility=hidden / C_VISIBILITY_PRESET hidden); only symbols tagged
 * EHEM_API are exported from the shared library. On Windows the same macro
 * carries __declspec(dllexport/dllimport).
 *
 *   - Building the shared library : the build defines EHEM_BUILDING_SHARED.
 *   - Consuming the shared library on Windows: define EHEM_USING_SHARED.
 *   - Static library / internal TUs: EHEM_API expands to nothing.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(EHEM_BUILDING_SHARED)
#    define EHEM_API __declspec(dllexport)
#  elif defined(EHEM_USING_SHARED)
#    define EHEM_API __declspec(dllimport)
#  else
#    define EHEM_API
#  endif
#else
#  if defined(EHEM_BUILDING_SHARED) && (defined(__GNUC__) || defined(__clang__))
#    define EHEM_API __attribute__((visibility("default")))
#  else
#    define EHEM_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime library version, e.g. "0.1.0" (semantic versioning; 0.x until
 * full-spec conformance — ARCHITECTURE.md §4). The returned pointer is a
 * static string owned by the library; the caller must not free it.
 */
EHEM_API const char *ehem_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EHEM_H */
