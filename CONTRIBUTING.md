# Contributing

Contributions that improve compatibility, safety, tests, or documentation are welcome. Test only on electronic shelf labels you own or are authorized to modify.

## Development setup

1. Fork the repository and create a focused branch.
2. Install uFBT and select an SDK matching the target Flipper firmware.
3. Build from `precir/` with `ufbt`.
4. Run `ufbt lint` before submitting changes.
5. Run the host tests documented in [`tests/README.md`](tests/README.md).

Keep separate `UFBT_HOME` directories for official, Momentum, and other firmware SDKs. Do not distribute a FAP without stating its hardware target and API version.

## Pull requests

Describe:

- The label model and settings affected.
- Whether behavior was tested on hardware or only with host/build checks.
- The firmware and API used for the build.
- Any protocol or image-format evidence supporting the change.

Include golden vectors for protocol changes and malformed-input cases for parser or persistence changes. Do not include retail credentials, private store data, or instructions encouraging unauthorized label modification.

The project is GPL-3.0. Contributions are submitted under the repository's existing license.
