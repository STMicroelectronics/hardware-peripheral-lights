# hardware-lights #

This module contains the STMicroelectronics Lights hardware service source code.
It is part of the STMicroelectronics delivery for Android (see the [delivery][] for more information).

[delivery]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v2.0.0

## Description ##

This module version is the updated version for STM32MP15 distribution for Android V2.0
Please see the Android delivery release notes for more details.

It is based on the AIDL Lights version 1.0.

## Documentation ##

* The [release notes][] provide information on the release.
* The [distribution package][] provides detailed information on how to use this delivery.

[release notes]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v2.0.0
[distribution package]: https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package_for_Android

## Dependencies ##

This module can not be used alone. It is part of the STMicroelectronics delivery for Android.

It provides the Light hardware service android.hardware.light@-service which has to be added in device.mk as follow:
```
PRODUCT_PACKAGES += \
    android.hardware.lights-service.stm32mp1 \
```

## Contents ##

This directory contains the sources and the associated Android makefile to generate the android.hardware.lights-service.stm32mp1 binary.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
