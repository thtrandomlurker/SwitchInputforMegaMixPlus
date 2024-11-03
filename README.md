# Switch Input for Mega Mix+ (SIMM)
A set including a Homebrew Applet for Nintendo Switch and a plugin for Hatsune Miku: Project DIVA Mega Mix+ designed to transmit inputs over a USB or WIFI connection

## Features

* Joycon Input
  * Sends inputs to an emulated X360 controller using ViGEm
  * Supports injecting Tilt angle for use with Mix Mode if using Stewie1.0's Mix Mode patch
* USB Transmission
  * Supports Hot plugging (Kind of, can be buggy, can crash either MM+ or the switch. WIP)
  * Zero input latency
  * Requires to be built without defining USE_NETWORK
* WiFi Transmission
  * Must set switch IP Address through the mod's config.toml
  * If directly connected to a PC broadcasting a signal from its own WiFi adapter, latency is fairly low
    * However, It tends to very often lag behind, causing significant latency at intervals of 2 minutes exactly, with smaller drops every other minute. i.e. you start the game, for one minute it will work fine (or it might lag immediately?), then it will drop slightly at exactly 1 minute, then after another minute it will drop *very* hard, repeating in a fixed pattern.
  * Requires to be built with USE_NETWORK
* Sensitivity
  * By accessing the Mix Mode settings menu in Stewie1.0's patch using F7, you can adjust the sensitivity of the joycon tilt input, which will be used to adjust how fast/slow the cursors move.
    * Please note that very low values will break the cursor input.
      * Possibly the same with very high values as well.

## Planned Features
* Rumble Support

## Building
This project was made using Visual Studio, but relies on devkitPro to compile SIMM-Server (The switch applet), and also makes use of vcpkg to manage packages, primarily for libusb and tomlplusplus in SIMM-Client (The MM+ Plugin). ViGEmClient is included as a submodule.

### SIMM-Server
You will need devkitPro installed to continue, to build after cloning open devkitPro's msys2, cd to the SIMM-Server directory, and run make.

```
cd /path/to/SwitchInputforMegaMix+/SIMM-Server
make
```

### SIMM-Client
Before continuing, make sure to restore submodules if you never cloned using `--recurse-submodules` using `git submodule update --init --recursive` from within the SwitchInputforMegaMix+ directory.

To begin, building currently only works on Release. It's probably a simple fix, but i really want to get this out before i work on it more. Besides, it's stable *enough* i hope.

You will need vcpkg installed. Open the solution in Visual Studio 2022 or Later, and open a terminal window using Alt+`. then install libusb and tomlplusplus

```
vcpkg install libusb tomlplusplus
```

You will also need to build ViGEmClient ahead of time too. Simply open ViGEmClient's solution and build it within visual studio targetting Release_LIB and Debug_LIB, which should produce our desired libraries. I'm sure there's a better way to go about it but it works for now, refinements can come later.

Afterwards simply build SIMM-Client as normal, which should output SIMM-Client.dll, with libusb-1.0.dll in the same folder copied from the vcpkg install. Make sure to copy both to the mod directory as libusb is required.

## Other Requirements
You will need to have the ViGEm Bus Driver installed to emulate the X360 controller for inputs. in an ideal world i would understand how to modify the input state directly such that ViGEm would be unnecessary, however any attempt i've made to try modifying it has resulted in a crash. As such, ViGEm is simpler to work with, and is fairly reliable regardless, likely moreso than directly modifying the input state would ever be.