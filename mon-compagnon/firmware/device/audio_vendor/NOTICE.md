# Audio source attribution

Mon compagnon audio integration is GPL-3.0-only, like its linked FolloUp-derived board adapter. Full FolloUp license: `../vendor/FolloUp-LICENSE.txt`.

Pin mapping, 16 kHz full-duplex I2S0 configuration and mono left Philips slot were inspected read-only from **rorom62157-cloud/folloup-sticky**, commit **1682b7e7d7ff9c6b23ab7e903c44b241f095b402**:

- `components/board/include/waveshare_board_config.h`, blob `e29eeee1d5215ce64a7b7c75c945465e22c0538a`
- `components/audio_hal/board_es8311_codec.cc`, blob `8c1de8b32acac0466b7fa63472c21899595978be`
- `components/board/waveshare_board.cpp`, blob `6797435cfe8305ea2ba28f1d12241555b745573b`

The FolloUp firmware was neither built nor changed. Its esp_codec_dev dependency is absent from the installed Arduino SDK. Instead this directory retains Espressif's small ES8311 register driver under **Apache-2.0**, full text in `LICENSE`. Original copyright/SPDX notices are retained:

Repository: https://github.com/espressif/esp-bsp/tree/master/components/es8311

Fetched 2026-11-04; immutable source blob identities:

| Local file | Original path | Original Git blob |
| --- | --- | --- |
| es8311.inc | components/es8311/es8311.c | 40a44e576eb27c17bd074ca99823f22998baa3a6 |
| es8311.h | components/es8311/include/es8311.h | d746299a632f713efba445d7a895a23993ab89b8 |
| es8311_reg.h | components/es8311/priv_include/es8311_reg.h | aeae4c0cd80a3a85a300f7d918de196aea58480f |
| LICENSE | components/es8311/LICENSE | d645695673349e3947e8e5ae42332d0ac3164cd7 |

Changes: migrate legacy I2C calls to IDF master-device handles shared with RTC/PMIC, 100 ms transaction timeout, check allocation/device registration and previously unchecked ADC write, unsigned coefficient-loop index for strict compilation. `es8311.inc` is included exactly once by audio.cpp to work with Arduino's flat sketch source discovery. It must not be compiled separately. Initialization, coefficient table, register definitions and analog microphone setup otherwise follow the original driver. This is a real register/PCM adapter, not a synthetic input source.

`wav_metadata.hpp` is original Mon compagnon validation code, GPL-3.0-only.
