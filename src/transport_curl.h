/*
 * transport_curl.h — libcurl-typed internals of the default transport.
 *
 * INTERNAL, and the ONLY internal header that pulls <curl/curl.h>. It exists so
 * the pure CURLcode → ehem_rc mapping can be unit-tested directly (no network),
 * which is how STEP-M1-060 verifies error translation offline. Nothing in
 * include/ehem/ includes this, keeping the public headers libcurl-free
 * (REQ-NET-002 rationale).
 */
#ifndef EHEM_TRANSPORT_CURL_H
#define EHEM_TRANSPORT_CURL_H

#include <curl/curl.h>

#include "ehem/ehem.h"

/*
 * Translate a libcurl result into a transport-level ehem_rc (REQ-API-003,
 * REQ-NET-004):
 *   - could-not-connect / DNS failure, and connect-phase timeout → UNREACHABLE
 *   - post-connect timeout and mid-transfer failures (incl. TLS)   → NETWORK
 *   - out-of-memory → NOMEM; malformed URL / unsupported scheme → ARG
 *   - CURLE_OK → EHEM_OK
 * `connect_time` is CURLINFO_CONNECT_TIME (seconds; 0.0 if a connection was
 * never established) and is what disambiguates a connect timeout from a total
 * timeout, since libcurl reports both as CURLE_OPERATION_TIMEDOUT.
 */
ehem_rc ehem_curl_map_error(CURLcode code, double connect_time);

#endif /* EHEM_TRANSPORT_CURL_H */
