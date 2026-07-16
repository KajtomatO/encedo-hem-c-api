# Disruptive tests (label `disruptive`). Real device; MUTATES + REBOOTS it, so
# gated on EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1 (STEP-M2-060). Excluded from
# `-L integration`; run with `./dev test it -d` or
# `EHEM_ALLOW_DISRUPTIVE=1 ctest -L disruptive`.

# REQ-TOOL-003: live cert-install (harvest → skip-if-current → install →
# reboot → verify) driven through the real hem-tool cert-install code path.
ehem_add_disruptive_test(test_cert_install_live SOURCES test_cert_install_live.c)
