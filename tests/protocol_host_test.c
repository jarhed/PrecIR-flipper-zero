#include "../precir/precir_protocol.h"

#include <stdio.h>
#include <string.h>

static int expect_validation(const char* barcode, PrecIRBarcodeValidation expected) {
    PrecIRBarcodeValidation actual = precir_barcode_validate(barcode);
    if(actual != expected) {
        fprintf(stderr, "validation mismatch for %s: got %d expected %d\n", barcode, actual, expected);
        return 1;
    }
    return 0;
}

int main(void) {
    if(!precir_protocol_self_test()) {
        fputs("protocol self-test failed\n", stderr);
        return 1;
    }

    int failures = 0;
    failures += expect_validation("G4591371776312423", PrecIRBarcodeValid);
    failures += expect_validation("G459137177631242", PrecIRBarcodeInvalidLength);
    failures += expect_validation("g4591371776312427", PrecIRBarcodeInvalidFormat);
    failures += expect_validation("G5591371776312424", PrecIRBarcodeInvalidFamily);
    failures += expect_validation("G459137X776312423", PrecIRBarcodeInvalidFormat);
    failures += expect_validation("G4999991776312420", PrecIRBarcodeAddressOutOfRange);
    failures += expect_validation("G4591379999912427", PrecIRBarcodeAddressOutOfRange);
    failures += expect_validation("G4591371776312424", PrecIRBarcodeInvalidChecksum);

    uint8_t unchanged[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t output[sizeof(unchanged)];
    memcpy(output, unchanged, sizeof(output));
    if(precir_plid_from_barcode("invalid", output) || memcmp(output, unchanged, sizeof(output)) != 0) {
        fputs("invalid barcode changed PLID output\n", stderr);
        failures++;
    }

    if(failures == 0) puts("protocol host tests passed");
    return failures == 0 ? 0 : 1;
}
