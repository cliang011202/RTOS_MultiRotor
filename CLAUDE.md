# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RTOS-based **heptarotor** (七旋翼) device for **浮式风机实时混合水池试验** (floating wind turbine real-time hybrid model test), running on the **正点原子阿波罗v2 (Alientek Apollo v2)** development board (**STM32H743IIT6**, ARM Cortex-M7 @ 200 MHz) with FreeRTOS v10.3.1 and lwIP networking.

**System workflow**: The upper computer runs **WEC-SIM MOST** (a Simulink-based open-source floating wind turbine simulation platform). Each timestep it computes 6-DOF tower-top loads, scales them, and sends them via UDP to the STM32. The STM32 applies inverse kinematics to decompose the 6-DOF loads into 7 PWM signals, each driving an ESC to control one motor and generate the desired load.

## Build System

This is an **STM32CubeIDE 1.17.0** managed-build project using the **built-in CubeMX** for MCU configuration. The IDE auto-generates makefiles under `Debug/`. Do not edit `Debug/makefile`, `Debug/sources.mk`, or `Debug/objects.mk` directly.

**All custom code must be placed inside `/* USER CODE BEGIN xxx */` / `/* USER CODE END xxx */` guard blocks.** CubeMX code regeneration overwrites everything outside these guards.

**To build from the command line:**
```bash
# From the Debug/ directory
cd Debug
make all

# Build a single file (example)
make Core/Src/freertos.o

# Clean
make clean
```

**Toolchain**: `arm-none-eabi-gcc` v12.3 must be on PATH.

**Key compiler flags**: `-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb`

**Output**: `Debug/RTOS_MultiRotor.elf` — flash with STM32CubeProgrammer or via the IDE's debug session.

## Hardware Configuration

Device configuration is managed via **STM32CubeMX** — open `RTOS_MultiRotor.ioc` to modify peripheral settings. Regenerating code from CubeMX will overwrite `Core/Src/` and `Core/Inc/` files; custom code inside `USER CODE BEGIN/END` blocks is preserved.

**Key peripherals:**
- **TIM1 / TIM8**: 7 PWM channels at 50 Hz (20 ms period) for ESC motor control. Pulse = 1000–2000 µs; neutral = 1500.
- **I2C** at 400 kHz: SCL = PH4, SDA = PH5. Shared bus for IMU, PCF8574, and other sensors.
- **ETH + YT8512C PHY**: Ethernet MAC for UDP telemetry/command (see below).
- **Clock**: 25 MHz HSE → PLL → 200 MHz system clock.

### Ethernet (YT8512C PHY)

STM32H743 has a built-in Ethernet MAC; the external PHY is **YT8512C** connected via **RMII** interface. Supports auto-MDIX (auto-detects crossover/straight-through cables), 10M/100M adaptive. RJ45 connector: **HR91105A** (with built-in network transformer).

**ETH pin mapping:**

| Signal | MCU Pin |
|--------|---------|
| ETH_MDIO | PA2 |
| ETH_MDC | PC1 |
| RMII_TXD0 | PG13 |
| RMII_TXD1 | PG14 |
| RMII_TX_EN | PB11 |
| RMII_RXD0 | PC4 |
| RMII_RXD1 | PC5 |
| RMII_CRS_DV | PA7 |
| RMII_REF_CLK | PA1 |
| ETH_RESET | PCF8574 P7 (via NPN inverter) |

**YT8512C PHY driver note**: CubeMX has no native YT8512C driver — it generates LAN8742-based code by default. After code generation, either modify the PHY register operations in `LWIP/App/ethernetif.c` or replace it with a YT8512C-specific driver. The **PHY address** (typically `0x00` or `0x01`) must be confirmed by checking the PHYADD pin levels on the board and set accordingly (e.g. `#define ETHERNET_PHY_ADDRESS 0x00` in `ethernetif.c` or the lwIP config).

**Pin conflicts (mutually exclusive — time-division multiplex OK):**
- **PA2**: ETH_MDIO ↔ USART2_TX
- **PB11**: ETH_TX_EN ↔ USART3_RX
- **PB12**: PCF8574 IIC_INT ↔ 1-Wire temperature/humidity sensor DQ

### PCF8574 IIC IO Expander

Provides 8 extra GPIO pins via the shared I2C bus (PH4/PH5). Interrupt output on **PB12**.

| PCF8574 Pin | Connected to |
|-------------|-------------|
| P0 | BEEP (buzzer) |
| P1 | AP_INT (light sensor) |
| P2 | DCMI_PWDN (OLED/Camera) |
| P3 | USB_PWR (USB HOST) |
| P4 | 6D_INT (6-axis IMU) |
| P5 | RS485_RE (RS485 direction) |
| P6 | EXIO (P3 header, spare) |
| P7 | ETH_RESET (via NPN inverter) |

## Architecture

### FreeRTOS Task Structure (`Core/Src/freertos.c`)

Three tasks run concurrently:

| Task | Priority | Stack | Role |
|------|----------|-------|------|
| `defaultTask` | Normal | 2 KB | Calls `MX_LWIP_Init()` to bring up lwIP network stack |
| `udpRxTask` | Normal | 4 KB | Receives 6-DOF load packets from WEC-SIM MOST upper computer |
| `pwmCtrlTask` | **High** | 2 KB | Applies inverse kinematics → drives 7-channel ESC PWM outputs |

`pwmCtrlTask` runs at high priority so motor control is never preempted by networking.

### Memory Layout (Linker Script: `STM32H743IITX_FLASH.ld`)

| Region | Base | Size | Use |
|--------|------|------|-----|
| FLASH | 0x08000000 | 2 MB | Code + read-only data |
| DTCMRAM | 0x20000000 | 128 KB | Stack, time-critical code |
| RAM_D1 | 0x24000000 | 512 KB | Heap + general RAM |
| RAM_D2 | 0x30000000 | 288 KB | Ethernet DMA buffers |
| RAM_D3 | 0x38000000 | 64 KB | Low-power RAM |

FreeRTOS heap (64 KB, `heap_4.c`) lives in RAM_D1. Ethernet DMA descriptors must stay in RAM_D2 (non-cached, MPU-configured).

### Networking (`LWIP/`)

lwIP is initialized in `defaultTask` via `MX_LWIP_Init()`. The `ethernetif.c` driver handles DMA-based Ethernet with RTOS integration (`WITH_RTOS=1`). UDP socket code belongs in `udpRxTask`.

### HAL Peripheral Init (`Core/Src/`)

Each peripheral has its own `MX_XXX_Init()` function called from `main.c` before the RTOS scheduler starts:
- `tim.c` — TIM1/TIM8 PWM setup (call `HAL_TIM_PWM_Start()` per channel to enable outputs)
- `i2c.c` — I2C master setup (PH4/PH5)
- `gpio.c` — Port A/B/C/G/H configuration

## Development Notes

- **CubeMX regeneration** preserves code only inside `/* USER CODE BEGIN xxx */` / `/* USER CODE END xxx */` guards. Always put custom code inside these guards — this is enforced by the IDE/CubeMX workflow.
- **MPU regions**: RAM_D2 (0x30000000, 256 KB) is marked non-executable/non-cacheable for safe DMA use. Do not place code there.
- **I-Cache and D-Cache** are enabled at startup. When using DMA, ensure buffers are cache-aligned or use `SCB_CleanDCache_by_Addr()` / `SCB_InvalidateDCache_by_Addr()` as appropriate.
- **PWM neutral position**: Timers are initialized with `Pulse = 1500` (1.5 ms). ESCs typically arm with a fixed neutral pulse before varying the signal.
- **Pin conflicts**: PA2 and PB11 are shared between ETH and UART peripherals — do not enable both simultaneously.
