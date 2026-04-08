\# InkLab

\*\*Advanced Open-Source Heterogeneous Development Platform\*\*


[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)



InkLab started as a continuous glucose monitoring wearable, but it quickly grew into something much more flexible. It is now a general purpose embedded platform that combines a high performance MCU, an FPGA co-processor, and a browser-based interface with no installation required.



The goal is simple. Let researchers and developers focus on their algorithms instead of dealing with DMA, RTOS internals, or low level register work.



It is especially useful for areas like biomechanics, embedded AI, and medical hardware, where iteration speed matters more than boilerplate.



\## System Architecture



InkLab splits the system into four layers that work together:



\* \*\*Hardware acceleration (Mesa and iCE40 FPGA)\*\*

&#x20; Handles deterministic timing, parallel processing, display driving, and custom DSP logic.

\* \*\*RTOS layer (Vela on Zephyr)\*\*

&#x20; Manages system state, scheduling, power, and communication over USB.

\* \*\*Web interface (DevConsole)\*\*

&#x20; A Vue-based UI that runs in the browser and connects over USB using WebSerial.

\* \*\*Execution layer (WAMR and macros)\*\*

&#x20; Runs user logic without requiring a full firmware rebuild, either through WebAssembly or SD-based scripts.



\## Hardware Platform: Mesa (v2)



Mesa is the physical base of InkLab, built around the STM32H562.



\### Compute Core



\* \*\*Primary MCU\*\*

&#x20; STMicroelectronics STM32H562RIV running at 250 MHz with 2 MB dual bank flash and 640 KB RAM. Includes TrustZone, FPU, and DSP instructions.

\* \*\*FPGA Co-Processor\*\*

&#x20; Lattice iCE40UP5K with 5280 LUTs, 8 DSP blocks, and 128 Kbit BRAM. Built using the open source OSS CAD Suite.

\* \*\*Embedded SWD Debug Core\*\*

&#x20; A modified Black Magic Probe runs on the MCU and routes SWD over SPI into the FPGA. This allows external parallel programming and reaches around 100 KB/s.



\### Power and Measurement



\* \*\*PMIC\*\*

&#x20; Texas Instruments BQ25798 with USB-C input and Li-Ion power path management.

\* \*\*Power Profiling\*\*

&#x20; INA4235 allows real time monitoring of all main rails including MCU, FPGA core, FPGA IO, and sensors.

\* \*\*Protection\*\*

&#x20; TPS25210L e-fuses and TPS22913B load switches provide programmable protection and fault handling.



\### Display and Peripherals



\* \*\*E-Paper Pipeline\*\*

&#x20; The display is driven directly from the FPGA using an 8-bit parallel interface, reaching around 10 to 30 FPS. Unchanged pixels are suppressed at the FPGA level to reduce panel stress.

\* \*\*E-Ink Power\*\*

&#x20; TPS65185 generates the required high voltage rails.

\* \*\*I/O\*\*

&#x20; MicroSD slot, 4 switches (1 with wake-up support), and RGB LEDs for basic interaction.



> \*\*Note:\*\* The previous v1 hardware uses an STM32G0B1. It is still supported, but newer features like WAMR are focused on v2.


!\[Mesa v2 Hardware. Top view](docs/assets/InkLab\_topview.png)

!\[Mesa v2 Hardware. Bottom view](docs/assets/InkLab\_botview.png)

!\[Mesa v2 Hardware. Power tree](docs/assets/power\_tree.png)




\## Firmware: Vela



Vela is the firmware stack running on the MCU, built on Zephyr.



\* \*\*FPGA Bitstream Slots\*\*

&#x20; Up to 16 bitstreams can be stored on the SD card and loaded dynamically. Switching does not require a reboot.

\* \*\*WebAssembly Runtime\*\*

&#x20; WAMR allows running sandboxed logic compiled from Rust, Python, Go, or C. Hardware access is exposed through a native API bridge.

\* \*\*Telemetry\*\*

&#x20; A COBS-framed binary protocol is used over USB to keep throughput high and avoid unnecessary overhead in the browser.



\## Web Interface: InkLab DevConsole



DevConsole is a Vue-based web app and acts as the main interface.



\* \*\*Config Mode\*\*

&#x20; Manage FPGA bitstreams, update firmware, configure routing, and control power modes.

\* \*\*Debug Mode\*\*

&#x20; Includes a terminal, JS automation tools, SPI DMA testing, and live power monitoring.

\* \*\*App Mode\*\*

&#x20; A simplified interface intended for non-technical users, hiding development features.





## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

