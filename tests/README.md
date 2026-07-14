# PrecIR verification tests

Run the reference-vector suite from the repository root:

```powershell
python -m unittest discover -s tests -v
```

`protocol_host_test.c` exercises the actual portable C protocol implementation.
It can be built with any C11 compiler. For example, with Zig:

```powershell
python -m pip install ziglang==0.15.2
python -m ziglang cc -std=c11 -Wall -Wextra -Werror `
  tests/protocol_host_test.c precir/precir_protocol.c `
  -o protocol_host_test.exe
./protocol_host_test.exe

python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I tests/host_stubs `
  tests/image_host_test.c precir/precir_image.c `
  -o image_host_test.exe
./image_host_test.exe

python -m ziglang cc -std=c11 -Wall -Wextra -Werror `
  -I tests/profile_host_stubs -I precir `
  tests/profiles_host_test.c precir/precir_profiles.c precir/precir_protocol.c `
  -o profiles_host_test.exe
./profiles_host_test.exe
```

The image host test executes the real BMP parser, color-plane mapping, RLE, and
padding implementation through a small stdio-backed version of the Flipper
storage API. The profile host test checks collection operations, validation,
the exact PCRP v1 header and CRC, canonical record encoding, save/load, and
corruption reset through an in-memory storage stub. The FAP also executes
protocol, image-codec, and waveform self-tests during app startup. Those tests
do not energize the IR transmitter.
