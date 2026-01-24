/**
 * @file stack_helpers.c
 * @brief Stack checking helpers for libmad
 */

#include <stdint.h>

void stackenter(const char *s, const char *t, int i) {
    (void)s;
    (void)t;
    (void)i;
}

int stackfree(void) {
    return 8192;
}

