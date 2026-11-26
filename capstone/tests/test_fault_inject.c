/* capstone/tests/test_fault_inject.c - SW-FI-01: the fault injection interface is test-build only.
 *
 * Built twice by tools/capstone_gates.sh (and so by tools/run_tests.sh and CI, gcc and clang):
 *   without FSW_TEST: the API must be absent and the hook must be the constant 0 (built and run);
 *   with    FSW_TEST: the API must be declared with the agreed signatures (compiled only, -c:
 *                     there is no injector to link against until Session 14).
 * The third direction, "a nominal unit that calls fault_inject() does not compile", cannot be a
 * passing test inside a compiled file; the gate script compiles such a unit and requires failure.
 *
 * Only the header exists at the baseline, so the FSW_TEST half checks declarations (types and
 * signatures), not behaviour. The injector and its behavioural tests arrive in Session 14.
 */
#include "fault_inject.h"

#include <stdio.h>

#ifdef NDEBUG
#error "capstone tests rely on assert(); do not build them with NDEBUG"
#endif
#include <assert.h>

#if defined(FSW_TEST)

#if !defined(FAULT_INJECT_ENABLED) || FAULT_INJECT_ENABLED != 1
#error "FSW_TEST build: the injection API must be enabled"
#endif

/* The signatures the scenario harness will be written against. A changed signature fails here. */
static void (*const p_inject)(fault_id_t, int) = fault_inject;
static void (*const p_clear)(fault_id_t) = fault_clear;
static void (*const p_clear_all)(void) = fault_clear_all;
static int (*const p_active)(fault_id_t, int *) = fault_active;

/* The IDs the scenarios use, distinct and in range. */
_Static_assert(FI_NONE == 0, "FI_NONE must be 0 so a zeroed table means no fault");
_Static_assert(FI_LOW_VOLTAGE > FI_NONE && FI_OVERTEMP > FI_LOW_VOLTAGE && FI_TASK_STALL > FI_OVERTEMP &&
               FI_BAD_TC > FI_TASK_STALL && FI_STORE_FULL > FI_BAD_TC && FI_COUNT > FI_STORE_FULL,
               "fault IDs must be distinct and ordered");

#define BUILD_NAME "test build (FSW_TEST)"

#else /* nominal */

#if defined(FAULT_INJECT_ENABLED)
#error "nominal build: the injection API must not be visible"
#endif
/* The hook must be an integer constant expression equal to 0, so the guarded branch is dead and
 * removed: _Static_assert accepts only constant expressions. The identifiers passed to it do not
 * exist in this build, which also proves the preprocessor discards them. */
_Static_assert(FI_ACTIVE(FI_LOW_VOLTAGE, &no_such_variable) == 0, "nominal hook must be the constant 0");
_Static_assert(FI_ACTIVE(FI_TASK_STALL, (int *) 0) == 0, "nominal hook must be the constant 0");

#define BUILD_NAME "nominal build"

#endif

static void test_fault_inject_isolation(void) {
#if defined(FSW_TEST)
    assert(p_inject != NULL && p_clear != NULL && p_clear_all != NULL && p_active != NULL);
#else
    int fi_mv = 7400;
    if (FI_ACTIVE(FI_LOW_VOLTAGE, &fi_mv)) { /* the documented product-side pattern */
        fi_mv = 0;
    }
    assert(fi_mv == 7400); /* the hook never fires in a nominal build */
#endif
}

int main(void) {
    test_fault_inject_isolation();
    printf("fault injection isolation checks passed (%s)\n", BUILD_NAME);
    return 0;
}
