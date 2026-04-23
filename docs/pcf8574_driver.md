# PCF8574 IO 扩展驱动说明

## 1. 概述

`pcf8574` 是针对 **NXP PCF8574** 8位 I2C IO 扩展芯片的驱动。它通过共享 I2C 总线（I2C2，SCL=PH4 / SDA=PH5）提供 8 个额外的 GPIO，并将以太网 PHY 的硬件复位集成到 Ethernet 初始化流程中。

**关键作用**：PCF8574 的 P7 脚经 NPN 三极管反相后控制 YT8512C 的 `RESET#` 引脚。上电时 PCF8574 所有脚默认为高（0xFF），导致 PHY 一直处于复位状态，MDIO 总线无响应。**驱动初始化必须在 YT8512C_Init 之前执行**，否则以太网无法工作。

---

## 2. 文件位置

```
Drivers/
└── BSP/
    └── Components/
        └── pcf8574/
            ├── pcf8574.h   ← 引脚定义、结构体、API 声明
            └── pcf8574.c   ← 实现
```

集成点：

```
LWIP/Target/ethernetif.c
  ETH_PHY_IO_Init()          ← PCF8574_Init + PCF8574_ETH_Reset 在此调用
  USER CODE BEGIN 3           ← static PCF8574_Object_t PCF8574_Dev 声明
```

---

## 3. 硬件信息

**I2C 地址**：PCF8574（非 A 型），地址引脚 A0=A1=A2=0（接地）

| 地址格式 | 值 |
|----------|----|
| 7-bit 地址 | `0x20` |
| HAL 8-bit 地址（传入 HAL I2C 函数）| `0x40`（= 0x20 << 1） |
| 宏定义 | `PCF8574_I2C_ADDR` |

**中断引脚**：`/INT` → MCU PB12（低有效，任意引脚电平发生变化时触发）

---

## 4. 引脚映射

| PCF8574 引脚 | 位掩码宏 | 连接外设 | 默认初始状态 | 说明 |
|-------------|----------|----------|-------------|------|
| P0 | `PCF8574_PIN_BEEP` | 蜂鸣器 | 0（关） | 高电平响 |
| P1 | `PCF8574_PIN_AP_INT` | 光线传感器中断 | 1（输入） | 读取前须置1 |
| P2 | `PCF8574_PIN_DCMI_PWDN` | 摄像头/OLED 掉电 | 1（掉电） | 高有效掉电，不用摄像头时保持1 |
| P3 | `PCF8574_PIN_USB_PWR` | USB HOST 电源 | 0（关） | 按需使能 |
| P4 | `PCF8574_PIN_6D_INT` | 六轴 IMU 中断 | 1（输入） | 读取前须置1 |
| P5 | `PCF8574_PIN_RS485_RE` | RS485 方向控制 | 0（接收） | 0=接收，1=发送 |
| P6 | `PCF8574_PIN_EXIO` | P3 扩展备用 IO | 1（输入） | 当前未使用 |
| P7 | `PCF8574_PIN_ETH_RESET` | YT8512C RESET# | 0（正常）| **★ 关键** |

**P7 与 ETH_RESET 的逻辑关系：**

```
P7 = 1  →  PHY RESET# = 低  →  PHY 处于复位状态
P7 = 0  →  PHY RESET# = 高  →  PHY 正常运行,输出 50 MHz REFCLK
```

**上电初始状态字节（`PCF8574_INIT_STATE`）：**

```
bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
  0     1     0     1     0     1     1     0   = 0x57
 P7=0  P6=1  P5=0  P4=1  P3=0  P2=1  P1=1  P0=1
```

---

## 5. 影子寄存器（OutputLatch）

PCF8574 没有独立的"输出数据寄存器"——读操作返回的是引脚**实际电平**（外部可拉低），而非上次写入的值。因此做位操作（SetPin/ResetPin）时，无法通过读芯片来获知其他位的输出状态。

驱动在对象结构体中维护 `OutputLatch` 字节，记录最后一次写出的完整字节。每次位操作都是"修改影子寄存器 → 写入芯片"，确保其他位不受影响。

---

## 6. 公共 API

### 6.1 初始化

```c
extern I2C_HandleTypeDef hi2c2;
static PCF8574_Object_t PCF8574_Dev;

/* 绑定 I2C 句柄，写入安全初始状态（P7=0 释放 PHY 复位） */
PCF8574_Init(&PCF8574_Dev, &hi2c2, PCF8574_I2C_ADDR);

/* 对 YT8512C 执行一次完整的硬件复位（通常紧跟 Init 调用） */
PCF8574_ETH_Reset(&PCF8574_Dev);
```

### 6.2 函数说明

| 函数 | 说明 |
|------|------|
| `PCF8574_Init(pObj, hi2c, devAddr)` | 绑定 I2C 句柄和地址，写入 `PCF8574_INIT_STATE`（0x56）到芯片 |
| `PCF8574_WriteAll(pObj, val)` | 覆盖写全部 8 位，更新影子寄存器 |
| `PCF8574_ReadAll(pObj, &val)` | 读取所有引脚实际电平（非影子寄存器） |
| `PCF8574_SetPin(pObj, pin)` | 将指定位置 1，其余位不变。`pin` 可多位 OR |
| `PCF8574_ResetPin(pObj, pin)` | 将指定位清 0，其余位不变 |
| `PCF8574_ReadPin(pObj, pin, &state)` | 读取指定引脚实际电平，返回 0 或 1 |
| `PCF8574_ETH_Reset(pObj)` | 硬件复位 YT8512C：P7=1 持续 10 ms → P7=0 → 等待 50 ms |
| `PCF8574_BEEP_Set(pObj, on)` | 控制蜂鸣器，on=1 响，on=0 停 |
| `PCF8574_RS485_SetTx(pObj)` | RS485 切换到发送模式（P5=1） |
| `PCF8574_RS485_SetRx(pObj)` | RS485 切换到接收模式（P5=0） |

### 6.3 返回值

| 返回值 | 值 | 说明 |
|--------|-----|------|
| `PCF8574_STATUS_OK` | 0 | 成功 |
| `PCF8574_STATUS_ERROR` | -1 | I2C 通信失败 |

---

## 7. 与 ethernetif.c 的集成

PCF8574 的初始化被插入 `ETH_PHY_IO_Init()` 中，该函数是 YT8512C 驱动的 IO 层回调，在 `YT8512C_Init` 扫描 MDIO 总线之前自动被调用。

**调用时序：**

```
MX_LWIP_Init()
  └─ low_level_init()
       └─ YT8512C_RegisterBusIO()      注入 IO 函数指针
       └─ YT8512C_Init()
            └─ ETH_PHY_IO_Init()       ← PCF8574_Init + PCF8574_ETH_Reset
            └─ MDIO 扫描（0~31地址）
            └─ 软件复位 + 等待
```

**ethernetif.c 中的修改位置（共 5 处）：**

| 位置 | 修改内容 |
|------|---------|
| `#include` | `lan8742.h` → `yt8512c.h` + `pcf8574.h` |
| 全局变量 | `lan8742_Object_t / lan8742_IOCtx_t` → `yt8512c_Object_t / yt8512c_IOCtx_t` |
| `USER CODE BEGIN 3` | 新增 `static PCF8574_Object_t PCF8574_Dev` |
| `ETH_PHY_IO_Init()` | 新增 PCF8574 初始化和硬件复位调用 |
| `low_level_init()` 和 `ethernet_link_thread()` | 所有 `LAN8742_*` → `YT8512C_*` |

---

## 8. STM32CubeIDE 编译配置

如果编译报 `pcf8574.h: No such file or directory`，手动添加包含路径：

1. 右键项目 → **Properties → C/C++ Build → Settings → MCU GCC Compiler → Include paths**
2. 添加：`../Drivers/BSP/Components/pcf8574`
3. 右键 `pcf8574` 文件夹 → **Add/Remove from Build Path** → 勾选加入编译

---

## 9. 注意事项

- **I2C 总线共享**：PCF8574 与 IMU 等其他设备共用 I2C2（PH4/PH5），在 FreeRTOS 环境中如有多任务访问需加互斥锁（当前 PCF8574 仅在以太网初始化时使用，不存在并发问题）。
- **读输入引脚前须先写 1**：AP_INT（P1）、6D_INT（P4）等输入引脚在 Init 时已置 1，可直接调用 `PCF8574_ReadPin` 读取。
- **`PCF8574_ETH_Reset` 使用 `HAL_Delay`**：可在调度器启动前后均调用，但会阻塞 CPU 约 60 ms，仅在初始化时调用一次，不影响实时性。
