
# Pilkki firmware

Firmware for **Pilkki** flasher.

This is CMake project.

## Build project

Make sure you cloned submodules

`git clone --recurse-submodules`

or if already cloned, from within the repo directory 

`git submodule update --init`

------------
Inside *./firmware* directory:

To configure project
`mkdir build`
`cmake -B ./build`

To build and generate .bin file
`cmake --build ./build --target pilkki-firmware-bin`

To build and flash firmware
`cmake --build ./build --target pilkki-firmware-dfu`
see [Flashing/dfu-util](#dfu-util) section

## Flashing

To switch the device to *Device Firmware Upgrade* mode:

 - Unplug USB.
 - Holding button plug USB in.

Lack of startup blinking indicates DFU mode.

### dfu-util

Get and install free flashing software
https://dfu-util.sourceforge.net/

To flash `.bin` file use this command line

`dfu-util -a 0 -i 0 -s 0x08000000 -D ./build/app/pilkki-firmware.bin`

or input path to `.bin` file downloaded form *Releases*

If have problem finding USB device use `--help` command.

On Windows you have to register the device with the WinUSB driver. See https://github.com/libusb/libusb/wiki/Windows.

### STM32CubeProgrammer

Get and install free flashing software

https://www.st.com/en/development-tools/stm32cubeprog.html

 - On the right panel in the drop down list **type of connection**
   choose **USB**.
 - Find device in USB configuration panel: PID `0xdf11`, VID `0x0483`.
 - Click **connect**
 - In **Erasing & Programming** tab, browse file located at
   `./build/app/pilkki-firmware.bin` or path to `.bin` file downloaded from *Releases*
 - Press **Start Programming**
 