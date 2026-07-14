# PrecIR for Flipper Zero

PrecIR is a Flipper Zero application for transmitting image and segment updates to compatible infrared Pricer electronic shelf labels (ESLs). It implements the PP4 and PP16 signaling and frame formats documented by [furrtek's PrecIR research](https://www.furrtek.org/?a=esl).

Use it only with ESLs you own or have permission to test. Updating an ESL does not update a store's price database.

## Hardware and firmware

A stock Flipper Zero is enough. The application drives the built-in approximately 940 nm IR emitters with a 1.25 MHz carrier, so no ESP32 or other transmitter board is required. Aim the top edge of the Flipper directly at the label's optical receiver and keep it still until the transfer finishes.

This release is built and checked for:

- Flipper Zero hardware target 7
- Momentum `mntm-012`
- Firmware API `87.1`

The source can be rebuilt against another compatible Flipper firmware SDK. A prebuilt FAP with the wrong API version will be rejected by the firmware. An external high-speed IR module can improve range, but a module intended only for 38 kHz remotes may not switch correctly at 1.25 MHz.

## Features

- Save up to eight tag profiles.
- Store a 17-character Pricer barcode, BMP path, display size, color/plane mode, PP4/PP16 mode, and page for each tag.
- Choose or replace one saved BMP per tag.
- Send a saved image or a white frame with the saved settings.
- Run **Test Clear** through 16 size/plane/protocol/page combinations and save the one that works.
- Decode bounded, uncompressed 24-bit and 32-bit BMP pixel data from the SD card.
- Scale images to 208 x 112 or 296 x 128 and encode B&W, B/W/red, or experimental four-color bitplanes.
- Use raw image data or PrecIR RLE, whichever is smaller.
- Send 23-byte segment patterns entered as 46 hexadecimal characters.
- Show the exact active configuration and frame progress during transmission.
- Cancel safely after the current complete frame.

Saved profiles use a checksummed `PCRP` v1 file at `/data/precir/profiles.dat`. Missing files start with an empty profile list; invalid or corrupted files are rejected and reset in memory. BMP paths are saved only after the selected image decodes successfully.

Phone barcode scanning over Bluetooth is not included in this release. It still requires a companion Android application and a matching Flipper RPC receiver.

## Install

Copy [`precir/dist/precir.fap`](precir/dist/precir.fap) to:

```text
/ext/apps/Infrared/precir.fap
```

Then open **Apps > Infrared > PrecIR**. Remove older duplicate PrecIR FAPs if more than one copy appears in the menu.

## Use

### Add and send to a tag

1. Select **+ New Tag**.
2. Enter the complete 17-character barcode printed on the tag.
3. Select **Test Clear** if the correct settings are unknown.
4. Keep the Flipper aimed while a white frame is sent, then wait five seconds.
5. If the label is fully white, press **Save**. Otherwise press **Next**.
6. Select **Choose BMP**, pick an image under `/ext`, and keep the Flipper aimed for the full transfer.

The confirmed working first calibration choice is:

```text
1/16  296x128  2 planes
PP4   Page 0
```

That result is specific to the tested SmartTAG HD Large Red label; other Pricer models can require a different choice.

### Saved-tag actions

- **Send Saved Image** decodes and sends the stored BMP with the saved configuration.
- **Choose/Change BMP** validates a new image before storing its path.
- **Clear With Saved** sends one white image with the saved configuration.
- **Test Clear** starts calibration again.
- **Edit Settings** changes size, planes, protocol, and page directly.
- **Set Segments** sends a 23-byte segment bitmap using PP4.
- **Delete Tag** removes the profile, settings, and saved path; it does not delete the BMP itself.

E-paper can continue refreshing after the IR transfer has ended. Keep the label still for several more seconds. “Transfer sent” confirms that the Flipper emitted every frame; the label does not send an acknowledgement that the stock Flipper can receive.

### BMP requirements

- Windows BMP pixel data
- Uncompressed `BI_RGB`
- 24-bit BGR or 32-bit BGRA
- Stored somewhere under `/ext`

Exact 208 x 112 or 296 x 128 assets give the most predictable composition. B/W/red images should use strong black, white, and red regions. Four-color output remains experimental.

## Build and verify

Install [uFBT](https://pypi.org/project/ufbt/) and use an SDK matching the firmware on the Flipper. Keep the Momentum SDK isolated from other firmware SDKs:

```powershell
python -m pip install --upgrade ufbt
$env:UFBT_HOME="$HOME\.ufbt-momentum-mntm-012"
cd precir
ufbt
ufbt lint
```

The build writes `precir/dist/precir.fap`. The checked build reports target 7 and API 87.1.

Run the portable verification suite from the repository root:

```powershell
python -m unittest discover -s tests -v
```

The additional C11 host tests exercise the actual protocol, image, and profile implementations. See [`tests/README.md`](tests/README.md) for their compile commands.

The FAP also runs protocol, image-codec, and waveform self-tests at startup. These checks do not energize the IR transmitter. They do not replace oscilloscope measurements or testing with each physical label model.

## Source layout

- `precir/precir_profiles.*` — stable profile format, validation, CRC-32, and transactional saves
- `precir/precir_protocol.*` — barcode/PLID validation, CRC-16, and protocol frames
- `precir/precir_image.*` — bounded BMP reading, scaling, color planes, RLE, and white payloads
- `precir/precir_ir.*` — direct TIM1 carrier generation, PP4/PP16 timing, repeats, and cancellation
- `precir/precir_scene_*.c` — menus, profile workflow, calibration, and progress UI
- `tests/` — portable golden-vector and storage-format tests

See [`RECONSTRUCTION.md`](RECONSTRUCTION.md) for provenance and the relationship between this source release and the previously distributed v2.1 FAP. See [`CHANGELOG.md`](CHANGELOG.md) for release details.

## Limitations

- The application is transmit-only on stock Flipper hardware. Its normal IR receiver is intended for approximately 38 kHz signals, not the 1.25 MHz PrecIR carrier.
- Compatibility is limited to Pricer labels using the supported optical protocol; unrelated RF or BLE ESLs are not supported.
- Optical range depends on alignment, ambient light, label battery condition, and receiver sensitivity.
- Segment bit mappings vary by label model.
- Four-color encoding has not been verified across label models.

## Credits and license

- Protocol research and original PrecIR tools: [furrtek / m133113](https://github.com/m133113/PrecIR)
- Initial Flipper application port: [jarhed](https://github.com/jarhed/PrecIR-flipper-zero)
- Reconstructed saved-profile release and verification work: PrecIR contributors

This repository is distributed under the [GNU General Public License v3](LICENSE).
