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
- **USART1**: Debug serial, PA9 (TX) / PA10 (RX) — see "Debug Serial" section below.
- **I2C2** at 400 kHz: SCL = PH4, SDA = PH5. Shared bus for IMU, PCF8574, and other sensors.
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
| ETH_RESET | PCF8574 P7 (direct, no inverter) |

**YT8512C PHY driver**: Implemented at `Drivers/BSP/Components/yt8512c/`. Uses dependency injection — `ethernetif.c` registers `HAL_ETH_ReadPHYRegister`/`WritePHYRegister` as I/O callbacks. The driver auto-scans MDIO addresses 0–31 to find the PHY (reads PHYID1/2 OUI) rather than using a hardcoded address. Key difference from the CubeMX-generated LAN8742 code: link speed/duplex are read from register `0x11` (PHYSTS, vendor-specific) not `0x1F`.

**Hardware init sequence constraint**: PCF8574 P7 is the direct RESET# line of the YT8512C (P7=1 = normal, P7=0 = reset). `PCF8574_Init()` must be called **before** ETH MAC init to release RESET#, followed by at least 150 ms delay for the PHY's REFCLK (50 MHz) to stabilize. `main.c` does this with a 300 ms `HAL_Delay` between PCF8574 init and the RTOS scheduler start.

**Pin conflicts (mutually exclusive — time-division multiplex OK):**
- **PA2**: ETH_MDIO ↔ USART2_TX
- **PB11**: ETH_TX_EN ↔ USART3_RX
- **PB12**: PCF8574 IIC_INT ↔ 1-Wire temperature/humidity sensor DQ

### Debug Serial (USART1)

板载 **CH340** USB 转串口芯片通过 **P11 跳线排** 连接到 STM32 的 USART1：

| 信号 | MCU Pin | 说明 |
|------|---------|------|
| USART1_TX | PA9 | STM32 发送 → CH340 RXD |
| USART1_RX | PA10 | STM32 接收 ← CH340 TXD |

P11 跳帽短接 = USB 串口直通；拔掉跳帽可改接外部 TTL 设备，或把开发板当 USB 转 TTL 使用。

**调试 printf 路由**：`Core/Src/syscalls.c` 的 `__io_putchar` 应调用 `HAL_UART_Transmit(&huart1, ...)` 而非 ITM（SWV 在部分环境下不可用）。

**⚠ 引脚冲突（重要）**：PA9 = TIM1_CH2（电机 2），PA10 = TIM1_CH3（电机 3）。启用 USART1 调试时，**必须**在 CubeMX 中把这两路 PWM 重映射到空闲引脚：
- TIM1_CH2 → **PE11**
- TIM1_CH3 → **PE13**

重映射后，电机 2/3 的 ESC 信号线改接 PE11/PE13；PA9/PA10 专用于 USB 串口调试。

### PCF8574 IIC IO Expander

Provides 8 extra GPIO pins via the shared I2C2 bus (PH4/PH5). Interrupt output on **PB12**. Driver at `Drivers/BSP/Components/pcf8574/`. Uses an `OutputLatch` shadow register in the driver object because the chip cannot read back its own output state (reads return actual pin levels, not latch values).

| PCF8574 Pin | Connected to |
|-------------|-------------|
| P0 | BEEP (buzzer — active low; P0=1 = silent) |
| P1 | AP_INT (light sensor, input) |
| P2 | DCMI_PWDN (OLED/Camera) |
| P3 | USB_PWR (USB HOST) |
| P4 | 6D_INT (6-axis IMU, input) |
| P5 | RS485_RE (RS485 direction: 0=Rx, 1=Tx) |
| P6 | EXIO (P3 header, spare) |
| P7 | ETH_RESET (direct RESET# line: P7=1=normal, P7=0=reset) |

`PCF8574_INIT_STATE = 0xD7`: P0=1(beep off), P1=1(input), P2=1(camera off), P3=0(USB off), P4=1(input), P5=0(RS485 Rx), P6=1(spare high), P7=1(PHY running).

## Architecture

### FreeRTOS Task Structure (`Core/Src/freertos.c`)

Three tasks run concurrently:

| Task | Priority | Stack | Role |
|------|----------|-------|------|
| `defaultTask` | Normal | 2 KB | Calls `MX_LWIP_Init()` then polls link status; logs `g_eth_rx_irq_count` every 2 s |
| `udpRxTask` | Normal | 4 KB | Receives 6-DOF load packets from WEC-SIM MOST upper computer; currently logs them |
| `pwmCtrlTask` | **High** | 2 KB | Runs sweep test on startup, then loops at 50 Hz waiting for IK result (Queue pending) |

`pwmCtrlTask` runs at high priority so motor control is never preempted by networking.

**⚠ Pending work**: The udpRxTask→pwmCtrlTask data path is not yet wired. `pwmCtrlTask` has a `TODO` block for `xQueueReceive(xLoadQueue, &load, portMAX_DELAY)` + `inverse_kinematics(&load, pulses)` + `PWM_SetAll(pulses)`. A FreeRTOS queue (`xLoadQueue`) and the IK function need to be implemented.

### UDP Packet Format (`Core/Inc/wec_sim_packet.h`)

Fixed 28-byte little-endian packet from WEC-SIM MOST to STM32 UDP port 8080:

| Field | Type | Description |
|-------|------|-------------|
| `seq` | uint32 | Frame counter (monotonic, for drop detection) |
| `fx` | float32 | Tower-top force X (N) |
| `fy` | float32 | Tower-top force Y (N) |
| `fz` | float32 | Tower-top force Z (N) |
| `mx` | float32 | Tower-top moment X (N·m) |
| `my` | float32 | Tower-top moment Y (N·m) |
| `mz` | float32 | Tower-top moment Z (N·m) |

No byte-swap needed (both x86 Simulink host and ARM Cortex-M7 are little-endian).

### Motor Channel Mapping (`Core/Src/pwm_ctrl.c`)

| Motor | Timer | Channel | Pin |
|-------|-------|---------|-----|
| 1 | TIM1 | CH1 | PE9 |
| 2 | TIM1 | CH2 | PE11 |
| 3 | TIM1 | CH3 | PE13 |
| 4 | TIM1 | CH4 | PA11 |
| 5 | TIM8 | CH1 | PC6 |
| 6 | TIM8 | CH2 | PC7 |
| 7 | TIM8 | CH3 | PC8 |

Timer config: PSC=99, ARR=19999 → 1 MHz counter clock → 1 count = 1 µs → CCR value equals pulse width in µs directly. `PWM_SetAll()` writes all 7 CCRs via `__HAL_TIM_SET_COMPARE` (not `HAL_TIM_PWM_Start`, which is only needed once in `PWM_Init`).

### Memory Layout (Linker Script: `STM32H743IITX_FLASH.ld`)

| Region | Base | Size | Use |
|--------|------|------|-----|
| FLASH | 0x08000000 | 2 MB | Code + read-only data |
| DTCMRAM | 0x20000000 | 128 KB | Stack, time-critical code |
| RAM_D1 | 0x24000000 | 512 KB | Heap + general RAM |
| RAM_D2 | 0x30000000 | 288 KB | Ethernet DMA buffers |
| RAM_D3 | 0x38000000 | 64 KB | Low-power RAM |

FreeRTOS heap (64 KB, `heap_4.c`) lives in RAM_D1. lwIP heap is at `0x30004000` in RAM_D2. Ethernet DMA descriptors must stay in RAM_D2 (non-cached, MPU-configured).

### Custom BSP Drivers (`Drivers/BSP/Components/`)

| Driver | Files | Notes |
|--------|-------|-------|
| YT8512C PHY | `yt8512c/yt8512c.{c,h}` | MDIO over dependency-injected IO callbacks; auto-scans PHY address |
| PCF8574 IO expander | `pcf8574/pcf8574.{c,h}` | Shadow register for write-back; controls ETH_RESET, BEEP, RS485 |

Driver docs in `docs/` (yt8512c_driver.md, pcf8574_driver.md, pwm_ctrl.md).

### Networking (`LWIP/`)

lwIP 2.1.2 is initialized in `defaultTask` via `MX_LWIP_Init()`. `LWIP/Target/ethernetif.c` handles DMA-based Ethernet with RTOS integration (`WITH_RTOS=1`) and hardware checksum offload (`CHECKSUM_BY_HARDWARE=1`). UDP socket code belongs in `udpRxTask` using the lwIP socket API (`lwip/sockets.h`).

### HAL Peripheral Init (`Core/Src/`)

Each peripheral has its own `MX_XXX_Init()` function called from `main.c` before the RTOS scheduler starts:
- `tim.c` — TIM1/TIM8 PWM setup (call `HAL_TIM_PWM_Start()` per channel in `PWM_Init()` to enable outputs)
- `i2c.c` — I2C2 master setup (PH4/PH5) — used by PCF8574 and IMU
- `gpio.c` — Port A/B/C/G/H configuration

## Development Notes

- **CubeMX regeneration** preserves code only inside `/* USER CODE BEGIN xxx */` / `/* USER CODE END xxx */` guards. Always put custom code inside these guards — this is enforced by the IDE/CubeMX workflow.
- **MPU regions**: RAM_D2 (0x30000000, 256 KB) is marked non-executable/non-cacheable for safe DMA use. Do not place code there.
- **I-Cache and D-Cache** are enabled at startup. When using DMA, ensure buffers are cache-aligned or use `SCB_CleanDCache_by_Addr()` / `SCB_InvalidateDCache_by_Addr()` as appropriate.
- **PWM neutral position**: Timers are initialized with `Pulse = 1500` (1.5 ms). ESCs arm after receiving a stable 1500 µs signal for ~2 s (`PWM_ARM_DELAY_MS`).
- **Pin conflicts**: PA2/PB11 shared with ETH; PA9/PA10 shared between USART1 and TIM1 CH2/CH3 — remap TIM1 CH2→PE11, CH3→PE13 before enabling USART1 debug.
- **printf debug**: 使用 USART1（USB 串口，CH340，P11 跳帽）而非 ITM/SWV；`syscalls.c` 中 `__io_putchar` 调用 `HAL_UART_Transmit(&huart1, ...)`。
