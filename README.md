<div align="center"><img width="128" height="128" alt="sidestick" src="https://github.com/user-attachments/assets/3a1a8f65-2939-492b-80dc-d20493a8e5b9" /></div>


<h1 align="center"> Sidestick </h1>

<img width="1920" height="1040" alt="Sidestick Full Screenshot" src="https://github.com/user-attachments/assets/229d778e-427e-48ea-938a-9830a025f9e4" />

## What is Sidestick?

Sidestick is an unofficial Linux native sample management tool for the Behringer LM Drum that seeks to replicate the core functionality of Behringer's official SynthTribe application, while adding a few additional features that are useful for getting the most out of the device.

## What Functionality Does it Have?

<img width="988" height="592" alt="Sidestick Edit Samples" src="https://github.com/user-attachments/assets/2d03011b-f94e-4002-84a3-c7a129e65cf6" />

- ### Basic Functionality:
  - Sample Upload
  - Bank Management
  - Kit Management
  - Device Backup and Restore
  - Bank Backup and Restore
  
- ### Extended Functionality
  - 12-bit Preview
  - Start/End Trimming
  - Automatic Silence Trimming
  - Normalization Per Sample
  - Normalization Per Upload Group
  

## What Has Been Omitted When Compared with SynthTribe?
- ### The Settings Menu
  - Everything useful can be set from the device itself
- ### Firmware Updating
  - Without support from Behringer, this likely won't be possible
- ### Sequencer
  - Omitted because I don't use it, if there's any demand I can revisit it
- ### Automatic Sample Trimming
  - SynthTribe inexplicably trims about 6ms off of the end of uploaded samples. While this is probably harmless for most samples, I have chosen not to replicate this behavior
  

---

## Targeted Devices
- Any computer running a decently modern Linux distro (Tested on Ubuntu 22.04 and Slackware -current)
- Any Behringer LM Drum running the 1.2.2 firmware
  - There is a hidden setting in the Help>About Menu to allow running on other system firmware versions. Proceed at your own risk, but if you do successfully interface with another firmware version let me know, I will build it in as supported.
- ***Anything else is untested***

---

## Install and Run

**Device access (all methods):** your user needs access to the LM Drum’s MIDI port. On most distros this works automatically when the device is plugged in. On Slackware, add your user to the audio group (usermod -aG audio <user>, then log out and back in).

### AppImage
The AppImage is provided for convenience and *should* run on most distros from roughly 2022 onwards.

1. Download Sidestick-x86_64.AppImage from the [Releases](https://github.com/syntheticErik/Sidestick/releases) page.
2. Make it executable and run it:
```
chmod +x Sidestick-x86_64.AppImage
./Sidestick-x86_64.AppImage
```

The AppImage carries its own copy of Qt, so it looks a little plainer than a native build, but it uses your desktop’s own file dialogs.
To uninstall, delete the file.

### Slackbuild (Slackware -current)
1. Download the latest Slackbuild from the [Releases](https://github.com/syntheticErik/Sidestick/releases) page.

```
tar xf sidestick-1.0.0-slackbuild.tar.gz
cd sidestick
wget https://github.com/syntheticErik/Sidestick/releases/download/v1.0.0/Sidestick-1.0.0.tar.gz
su -
cd /path/to/sidestick && sh sidestick.SlackBuild
installpkg /tmp/sidestick-1.0.0-x86_64-1_SBo.tgz
```

- **Upgrade:** build the new version the same way, then upgradepkg /tmp/sidestick-<version>-x86_64-1_SBo.tgz
- **Uninstall:** removepkg sidestick

### Build From Source
**Requirements:** CMake 3.20 or newer, a C++20 compiler (GCC 11+ or Clang 14+), Qt 6 (Widgets and Multimedia), libsndfile, libsamplerate, and the ALSA development files. Sidestick has been tested with Qt 6.8 and 6.11. Specific packages may vary by distro.

Build:
```
git clone https://github.com/syntheticErik/Sidestick.git
cd Sidestick
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run it straight from the build folder, without installing:
```
./build/sidestick
```

Or install it system-wide (to /usr/local):
```
sudo cmake --install build
```

Uninstall:
```
sudo xargs rm < build/install_manifest.txt
```

---

## Usage Instructions

*Sidestick is provided without warranty; while I haven't had any issues in usage or testing I can't guarantee that you won't. I recommend making a safety backup of your device with the official SynthTribe app before using Sidestick.*

Sidestick generally follows the 3 pane layout of the official application. Your overall pool of samples is in the left pane, with a tickbox to show or hide the factory samples. The bank you're currently working on is shown in the middle pane. The kit you're building, or the samples you've actually assigned to pads are in the right pane. Above the three management panes is a drop down box to select a bank to work on. Banks can be renamed by selecting Bank>Rename from the menu bar. 

Everything flows right, samples can be left clicked and dragged from the pool into a bank, and from the bank into the kit. Right clicking a sample in the kit removes it from the kit, right clicking a sample in the bank removes it from the bank and shows a warning if the sample is assigned to a kit voice.

When building your kit, there are a few limitations to be aware of. The three tom slots share one sample and are just pitched differently on the device. Likewise, the conga slots share one sample and are just pitched differently on the device. During my own testing these were noted to just be device limitations. Surprisingly however, and in contrast to SynthTribe's kit building mechanism, the Closed and Open Hi Hat slots do not have to share sample, but the closed hi hat voice does have a permanent invisible envelope applied to it; turning up decay will extend sample ring-out a little but it will always be cut short by the invisible envelope. So, while long samples can't be placed in the closed hi hat slot without getting cut short, the two hi hat slots can in fact be different samples.

Samples can be uploaded to the device by either selecting Samples>Upload Samples in the menu bar, or simply by dragging files from your file manager onto the left pane. Either option will trigger the Prepare upload window. The Prepare upload window has some basic tools that can be used to further prepare samples before committing them to your device. The left pane lists the samples in your upload group, their respective lengths, and has a play icon to preview each one. The right pane shows further information about the selected sample and everything you can do with it.

The main focal point of the right pane of the Prepare upload window is the sample waveform with trim handles for the start and end. Below the waveform is information about the sample that recalculates as you make adjustments. Below the sample information are adjustment boxes for the Start and End trim handles for making precise adjustments, in addition to a trim silence button that trims any part of the sample that drops below -60dBFS, and a reset button that resets any trimming back to the default. Below the trim options is a checkbox to apply a 5ms fade to the end of a trimmed sample to avoid clicking and popping. Below the fade checkbox are two level options; As-is will pass the sample on to the device without any normalization, while normalize peak will normalize the sample to -0.1dBFS before uploading it to the device. Further down is a box to rename your samples, names will be reformatted to all caps on the fly and must be 16 characters or less. Below the name box is a preview button and a checkbox for 12-bit. When the 12-bit checkbox is unticked the preview button will play the sample as-is in full quality, when the checkbox is ticked it will play an approximation of what the sample will sound like once it's on the device.

Finally along the bottom of the Prepare upload window is a button for "Trim silence on all" which will trim parts of all samples in the upload group where they drop below -60dBFS; a button labeled "Make Names Unique" which will append numbers on to the ends of the names of your samples in the event of same-name samples within the upload group, or same-name samples already on the device (the LM Drum will refuse same-name samples); a checkbox labeled "Normalize as a Group" which seeks to normalize the upload group proportionately (useful for upload groups with a high volume range between samples); a short bit of text explaining the number of samples that will be uploaded, the space they will take up, and the space remaining; and finally Upload and Cancel buttons. Upload will send your samples to the device, and Cancel will take you back to the main Sidestick window without any changes being made. Differing from SynthTribe, uploaded samples will be dropped into the pool but **not** automatically assigned to any bank. 

Back on the main application window a few more useful settings are available tucked away in the menu bar. Device>Backup can be used to perform a full device backup, likewise Device>Restore can be used to restore a full device backup. Device backups should be interchangeable with backups made through the official app. Factory samples, patterns, songs and global settings are not included in backups. Similarly, individual banks can be exported and imported by selecting Bank>Export and Bank>Import. Lastly, Bank>Clear Kit can be used to clear the right pane and set it back to default for the selected bank, and Bank>Clear Bank can be used to clear out the entire bank, resetting both the middle and right pane.

---

## Known Limitations

- Samples can't be deleted from the device via the software
  - In defense of Sidestick, SynthTribe can't do this either. Deleting a sample from the device doesn't send out any monitor-able MIDI signal, so cracking this is likely going to mean sending random delete commands to the device until a sample is deleted, then working backwards from that. Unfortunately I don't consider my device sacrificial so i'm uncomfortable with doing that.
- Samples can't be renamed via the software
  
---

## Other Notes

- This application could **probably** be adapted to work with the Behringer BMX, as it has a very similar firmware and workflow to LM Drum. I unfortunately don't have a BMX to develop against at this time.

---

## Disclosure and Division of Labor
- Performed by a human
  - Application idea, layout, and functionality
  - Testing
  - This Guide
- Performed by AI
  - All coding and library selection
  - Icon design

---

## License

Sidestick is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE).

Sidestick uses third-party libraries under their own licences; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## Disclaimer

This is an unofficial application, not affiliated with, endorsed by, or supported by Behringer or Music Tribe. LM Drum, BMX, and SynthTribe are trademarks of their owners.
