# Integration tests (label `integration`). Real device; skipped unless
# EHEM_TEST_URL is set (REQ-TEST-002). See tests/README.md.

ehem_add_integration_test(test_system_live SOURCES test_system_live.c)
