/*
 * test_version.c — first unit test: ehem_version().
 *
 * A passing run through CTest also demonstrates that the CMake build,
 * the static/shared targets and the `unit` label all work end to end.
 *
 * verifies: REQ-BUILD-001, REQ-API-006
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>

#include "ehem/ehem.h"

#ifndef EHEM_VERSION_STRING
#define EHEM_VERSION_STRING "0.0.0-dev"
#endif

/* ehem_version() returns exactly the project version string. */
static void test_version_matches_project(void **state)
{
    (void)state;
    const char *v = ehem_version();
    assert_non_null(v);
    assert_string_equal(v, EHEM_VERSION_STRING);
}

/* ehem_version() is a semantic version "MAJOR.MINOR.PATCH...". */
static void test_version_is_semver(void **state)
{
    (void)state;
    unsigned major, minor, patch;
    assert_int_equal(3, sscanf(ehem_version(), "%u.%u.%u", &major, &minor, &patch));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_version_matches_project),
        cmocka_unit_test(test_version_is_semver),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
