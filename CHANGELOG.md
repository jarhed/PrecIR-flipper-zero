# Changelog

## 2.1.1 — 2026-07-13

This is a source-reconstructed patch release of the previously distributed 2.1 Momentum FAP.

### Added

- Reproducible source for the saved-profile release.
- Up to eight checksummed saved-tag profiles.
- Saved BMP paths and per-tag display/protocol settings.
- Sixteen-choice white-screen calibration with a five-second observation delay.
- **Clear With Saved** and **Send Saved Image** actions.
- 24-bit and 32-bit uncompressed BMP payload helpers.
- Exact active-configuration and frame-progress display.
- Portable profile-format and white-payload tests.

### Reliability

- Parameter-frame repeats: 15.
- Image data-frame repeats: 4.
- Refresh-frame repeats: 20.
- Wake-frame repeats: 400.
- Segment-frame repeats: 101.
- Cancellation now finishes the current frame before stopping.

### Fixed during reconstruction

- **Delete Tag** now acts on the button labelled Delete rather than Cancel.
- **Clear With Saved** uses the correct transmission kind.
- The selected BMP path remains stable through decoding and is saved only after success.
- Returning through the profile-entry flow no longer clears the newly active profile.
- Calibration exits safely when a white payload cannot be allocated.
- The displayed and manifest version now agree.
- B/W/red encoding now uses the reference `10` red-pixel code instead of `00`.

## 2.1.0

Previously distributed binary-only Momentum build with saved profiles, Test Clear, saved-image actions, and strengthened frame repeats. The exact source tree for that binary was not retained; see [`RECONSTRUCTION.md`](RECONSTRUCTION.md).

## 1.1.0

- Manual image and segment transmission.
- Pricer barcode validation and PLID conversion.
- 208 x 112 and 296 x 128 display configuration.
- PP4/PP16, page, and color-plane selection.
- Direct 1.25 MHz Flipper IR transmitter.
