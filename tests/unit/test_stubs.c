#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include "uds/uds_core.h"

/* Stub for uds_input_sdu that verifies expected data */
void __wrap_uds_input_sdu(struct uds_ctx *ctx, const uint8_t *data, uint16_t len)
{
    (void)ctx;
    if (len > 0) {
        check_expected(len);
        check_expected_ptr(data);
    }
}
