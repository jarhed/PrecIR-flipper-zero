# Source reconstruction notes

## Why this document exists

The completed PrecIR 2.1 FAP survived, but its matching C source tree did not. Version 2.1.1 reconstructs that release into reviewable, buildable GPL source and applies a small set of correctness fixes. It is functionally equivalent by design; it is not expected to be byte-for-byte identical to the old executable.

## Inputs

- Original PrecIR protocol research and tools from [`m133113/PrecIR`](https://github.com/m133113/PrecIR).
- The initial Flipper port from [`jarhed/PrecIR-flipper-zero`](https://github.com/jarhed/PrecIR-flipper-zero).
- The surviving unstripped Momentum 2.1 FAP:

  ```text
  File:    precir_profiles_momentum.fap
  Size:    41,472 bytes
  SHA-256: f18fdce2f3f0a542446e76c2ac19c2836006644ddc7d286df976063f098f14d9
  Target:  Flipper Zero hardware 7
  API:     87.1
  ```

- Observed application behavior and the development history. These were used as hints and checked against the executable or tests where possible.

## Recovered exactly

- Ten-scene ordering and custom event IDs 100 through 114.
- Menu labels and saved-tag workflow.
- Sixteen Test Clear candidates and their ordering.
- Selection 1: 296 x 128, two planes, PP4, page 0.
- `PCRP` v1 profile header, 278-byte records, eight-record limit, and CRC-32.
- Missing/corrupt profile behavior and temporary-file replacement flow.
- White RLE payload; Selection 1 begins `80 00 4A 00` and pads to one 20-byte data frame.
- Wake, parameter, data, refresh, and segment repeat counts.
- Five-second calibration observation period.
- Transmission progress, cancellation boundaries, and post-refresh guidance.

## Intentional fixes and hardening

- Corrected the 2.1 Delete/Cancel branch and Clear Saved transmission-kind bugs.
- Corrected B/W/red accent encoding to match the surviving FAP and original PrecIR converter (`10` for red and `00` for yellow-accent source pixels).
- Preserved the selected BMP path instead of reloading the old profile path before decoding.
- Added strict file bounds, row bounds, integer-overflow checks, and clean allocation failure behavior while continuing to accept valid uncompressed 24-bit and 32-bit BMPs.
- Save BMP paths only after successful decoding.
- Use transactional profile changes with rollback on save failure.
- Force known segment-label transmissions to PP4.
- Keep direct-timer transmission cancellable only between complete frames.

## Deliberate parity differences

- One-plane B&W conversion uses luminance for every source color, matching the original PrecIR converter. The old executable applied accent-color rules even in one-plane mode, which could turn red source pixels white and yellow source pixels black.
- A final one-pixel RLE run is omitted because the known image dimensions imply it, matching the original PrecIR Python converter. The old executable emitted that redundant run. Both forms decode to the same pixels.
- Accent thresholds follow the original converter's normalized `> 0.5` intent. Boundary-valued antialiased pixels can differ from the executable's integer comparison.
- BMP headers and pixel ranges are checked more strictly than in the old executable. Valid 24-bit and 32-bit uncompressed files remain supported.

## Verification

The reconstructed application is compiled with the Momentum `mntm-012` SDK and passes the FAP API checker for target 7 / API 87.1. Host tests cover:

- Barcode validation and PLID conversion.
- CRC and documented protocol frames.
- 24/32-bit BMP parsing, color planes, RLE, and padding.
- Exact Selection 1 white payload.
- Exact profile file headers, records, CRC-32, save/load, duplicate rejection, deletion, and corruption reset.

The startup self-tests do not transmit IR. Physical compatibility still requires a powered, supported Pricer label and correct optical alignment.

## Not reconstructed or included

- Android barcode-scanner companion application.
- BLE RPC receiver for phone-to-PrecIR barcode delivery.
- Model-specific segment editors.
- Acknowledgement reception; the stock Flipper receiver cannot demodulate the 1.25 MHz optical carrier.
