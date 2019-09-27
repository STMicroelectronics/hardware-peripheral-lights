# hardware-lights #

This module contains the STMicroelectronics Lights HAL source code.
It is part of the STMicroelectronics delivery for Android (see the [delivery][] for more information).

[delivery]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v1.1.0

## Description ##

This is the first version of the module for stm32mp1.
It is based on Lights API version 1.0.

The configuration file lights.stm.xml shall be available in /vendor/etc or /system/etc on the device.
It defines the configuration for lights, refer to lights.example.xml for more information.
The xml is parsed by the "liblightsconfig" library to provide appropriate data to the HAL.

Please see the release notes for more details.

## Documentation ##

* The [release notes][] provide information on the release.
* The [distribution package][] provides detailed information on how to use this delivery.

[release notes]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v1.1.0
[distribution package]: https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package_for_Android

## Dependencies ##

This module can not be used alone. It is part of the STMicroelectronics delivery for Android.

It provides the light library used by the default implementation of android.hardware.light@<version>-service which has to be added in device.mk as follow:
```
PRODUCT_PACKAGES += \
    lights.stm \
    liblightsconfig \
    android.hardware.light@<version>-service \
    android.hardware.light@<version>-impl
```
The file manifest.xml can be updated in consequence
The file lights.stm.xml shall be copied in /vendor/etc/ or in /system/etc/ device directory.

## Contents ##

This directory contains the sources and the associated Android makefile to generate the lights.stm and liblightsconfig libraries.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
