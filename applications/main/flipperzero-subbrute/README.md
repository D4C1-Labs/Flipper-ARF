# SubGHz BruteForcer Application for Flipper Zero

SubGhz BruteForcer bundled with [The Doctor's Firmware](https://github.com/thedoctorz/doctor-firmware)

## Disclaimer

This software is for experimental purposes only and is not meant for any illegal activity/purposes.
We do not condone illegal activity and strongly encourage keeping transmissions to legal/valid uses allowed by law.

---

## Installation

The application is included in the standard firmware package of [The Doctor's Firmware](https://github.com/thedoctorz/doctor-firmware).
You just need to install the [latest firmware](https://github.com/thedoctorz/doctor-firmware/releases/latest).
<br />
You can also download a standalone release and unzip/untar it to the `SD Card/apps/Sub-GHz` directory.
<br />
> [!WARNING]
>
> The application is tested with [The Doctor's Firmware](https://github.com/thedoctorz/doctor-firmware)
> and [OFW](https://github.com/flipperdevices/flipperzero-firmware).

---

## User Guide

### Main Menu

<p>Here you can select the protocol and frequency that will be used for bruteforce.</p>
<p>According to our observations, <code>CAME 12bit 433MHz</code> is the most common protocol, so it is selected by default.</p>
<p></p>To identify other devices and protocols you should inspect the device.</p>
<p>According to the protocol, when probe a key, each value is sent 3 times.
For most of the devices this works but there are devices that don't work and more repetitions are needed.</p>
<p>The number of repetitions can be increased with the right button, the left button decreases the value.</p>
<p>But negative side of increasing the number of repetitions will be a longer key find time.</p>

---

#### Buttons

|           Button            |        Action         |
|:---------------------------:|:---------------------:|
|      :arrow_backward:       | Decrease repeat value |
|      :arrow_up_small:       |        Move up        |
|       :arrow_forward:       | Increase repeat value |
|     :arrow_down_small:      |       Move down       |
|       :record_button:       |    Select protocol    |
| :leftwards_arrow_with_hook: |   Close application   |

---

## Supported Protocols

### CAME

- CAME 12bit 303MHz
- CAME 12bit 307MHz
- CAME 12bit 315MHz
- CAME 12bit 330MHz
- CAME 12bit 433MHz
- CAME 12bit 868MHz

### NICE

- NICE 12bit 433MHz
- NICE 12bit 868MHz

### Ansonic

- Ansonic 12bit 433.075MHz
- Ansonic 12bit 433.920MHz
- Ansonic 12bit 434.075MHz

### Holtek

- Holtek HT12X 12bit FM 433.920MHz (TE: 204μs)
- Holtek HT12X 12bit AM 433.920MHz (TE: 433μs)
- Holtek HT12X 12bit AM 315MHz (TE: 433μs)
- Holtek HT12X 12bit AM 868MHz (TE: 433μs)
- Holtek HT12X 12bit AM 915MHz (TE: 433μs)

### Chamberlain

- Chamberlain 9bit 300MHz
- Chamberlain 9bit 315MHz
- Chamberlain 9bit 318MHz
- Chamberlain 9bit 390MHz
- Chamberlain 9bit 433MHz
- Chamberlain 8bit 300MHz
- Chamberlain 8bit 315MHz
- Chamberlain 8bit 390MHz
- Chamberlain 7bit 300MHz
- Chamberlain 7bit 315MHz
- Chamberlain 7bit 390MHz

### Linear

- Linear 10bit 300MHz
- Linear 10bit 310MHz
- Linear Delta 3 8bit 310MHz

### UNILARM

> [!NOTE]
>
> Only DIP switch combinations, not full 25bit bruteforce

- UNILARM 25bit 330MHz (TE: 209μs)
- UNILARM 25bit 433MHz (TE: 209μs)

### SMC5326

> [!NOTE]
>
> Only DIP switch combinations, not full 25bit bruteforce

- SMC5326 25bit 330MHz (TE: 320μs)
- SMC5326 25bit 433MHz (TE: 320μs)

### PT2260

> [!NOTE]
>
> Only for 8 DIP switch remote, not full 24bit bruteforce

- PT2260 24bit 315MHz (TE: 286μs)
- PT2260 24bit 330MHz (TE: 286μs)
- PT2260 24bit 390MHz (TE: 286μs)
- PT2260 24bit 433MHz (TE: 286μs)

### Additional

- BF Existing dump works for most other static protocols supported by Flipper Zero
