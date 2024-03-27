# hardware-lights #

This module contains the STMicroelectronics android.hardware.lights binary source code.

It is part of the STMicroelectronics delivery for Android.

## Description ##

This module implements android.hardware.lights AIDL version 2.
Please see the Android delivery release notes for more details.

## Documentation ##

* The [release notes][] provide information on the release.
[release notes]: https://wiki.st.com/stm32mpu/wiki/STM32_MPU_OpenSTDroid_release_note_-_v5.1.0

## Dependencies ##

This module can not be used alone. It is part of the STMicroelectronics delivery for Android.

```
PRODUCT_PACKAGES += \
    android.hardware.lights-service.stm32mpu \
```

## Containing ##

This directory contains the sources and associated Android makefile to generate the lights binary.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
