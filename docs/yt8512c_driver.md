# YT8512C PHY 驱动说明

## 1. 概述

`yt8512c` 是为 **YT8512C（Motorcomm 裕太微）** 以太网 PHY 芯片编写的 MDIO 驱动。  
它是 STM32CubeMX 自动生成的 LAN8742 驱动的**直接替换品**，接口与返回值数值保持完全兼容，因此 `ethernetif.c` 只需修改 `include` 和函数名，无需改动任何逻辑。

**适用硬件**：正点原子阿波罗v2（Apollo V2）开发板，MCU STM32H743IIT6。  
**PHY 接口**：RMII，支持 Auto-MDIX（自动识别交叉/直连网线），10M/100M 自适应。  
**RJ45 连接器**：HR91105A（内置网络变压器）。

---

## 2. 文件位置

```
Drivers/
└── BSP/
    └── Components/
        └── yt8512c/
            ├── yt8512c.h   ← 寄存器定义、结构体、函数声明
            └── yt8512c.c   ← 驱动实现
```

调用层（对比 LAN8742 的修改）：

```
LWIP/Target/ethernetif.c   ← 已将所有 LAN8742_* 替换为 YT8512C_*
```

---

## 3. 硬件连接（RMII 引脚映射）

| 信号 | MCU 引脚 | 说明 |
|------|---------|------|
| ETH_MDIO | PA2 | 管理数据线（与 USART2_TX 分时复用） |
| ETH_MDC | PC1 | 管理时钟线 |
| RMII_TXD0 | PG13 | 发送数据 0 |
| RMII_TXD1 | PG14 | 发送数据 1 |
| RMII_TX_EN | PB11 | 发送使能（与 USART3_RX 分时复用） |
| RMII_RXD0 | PC4 | 接收数据 0 |
| RMII_RXD1 | PC5 | 接收数据 1 |
| RMII_CRS_DV | PA7 | 载波侦听/数据有效 |
| RMII_REF_CLK | PA1 | 50 MHz 参考时钟（由 PHY 提供） |
| ETH_RESET | PCF8574 P7（NPN 反相后接 PHY RESET#） | 硬件复位 |

> **ETH_RESET 说明**：复位线通过 PCF8574 IIC IO 扩展芯片的 P7 脚控制，经 NPN 三极管反相后驱动 PHY 的低有效 RESET# 引脚。驱动代码使用软件复位（BCR[15]=1）代替，PCF8574 操作可选实现。

---

## 4. 与 LAN8742 的核心差异

| 对比项 | LAN8742 | YT8512C |
|--------|---------|---------|
| 链路状态寄存器 | `PHYSCSR` @ `0x1F`，bits[4:2] | `PHYSTS` @ `0x11`，bits[14:13] |
| 速率位 | PHYSCSR[2] | PHYSTS[14]：1=100M, 0=10M |
| 双工位 | PHYSCSR[4:3] 编码 | PHYSTS[13]：1=全双工, 0=半双工 |
| 协商完成位 | PHYSCSR[4:2]≠0 | PHYSTS[12] |
| PHY ID 识别 | SMR 寄存器型号字段 | PHYID2[9:4] == `0x12` |

**这是移植的唯一技术关键点**：其他所有标准寄存器（BCR/BSR/PHYID1/PHYID2，地址 0x00–0x05）两款芯片完全相同，由 IEEE 802.3 规定。

---

## 5. 公共 API

### 5.1 初始化流程（按顺序调用）

```c
#include "yt8512c.h"

static yt8512c_Object_t YT8512C;
static yt8512c_IOCtx_t  YT8512C_IOCtx = {
    ETH_PHY_IO_Init,
    ETH_PHY_IO_DeInit,
    ETH_PHY_IO_WriteReg,
    ETH_PHY_IO_ReadReg,
    ETH_PHY_IO_GetTick,
};

/* 步骤1：注入 IO 函数指针 */
YT8512C_RegisterBusIO(&YT8512C, &YT8512C_IOCtx);

/* 步骤2：初始化（扫描地址 + 软件复位） */
YT8512C_Init(&YT8512C);
```

### 5.2 函数说明

| 函数 | 说明 |
|------|------|
| `YT8512C_RegisterBusIO(pObj, ioctx)` | 将平台 IO 函数指针注入驱动对象。必须最先调用。 |
| `YT8512C_Init(pObj)` | 扫描 MDIO 总线找到 PHY 地址，执行软件复位，等待完成。 |
| `YT8512C_DeInit(pObj)` | 反初始化，清除 `Is_Initialized` 标志。 |
| `YT8512C_GetLinkState(pObj)` | 查询当前链路状态，返回下表状态码之一。 |

### 5.3 返回值（状态码）

| 返回值 | 数值 | 含义 |
|--------|------|------|
| `YT8512C_STATUS_OK` | 0 | 操作成功 |
| `YT8512C_STATUS_LINK_DOWN` | 1 | 链路断开（网线未插或对端断电） |
| `YT8512C_STATUS_100MBITS_FULLDUPLEX` | 2 | 100 Mbps 全双工 |
| `YT8512C_STATUS_100MBITS_HALFDUPLEX` | 3 | 100 Mbps 半双工 |
| `YT8512C_STATUS_10MBITS_FULLDUPLEX` | 4 | 10 Mbps 全双工 |
| `YT8512C_STATUS_10MBITS_HALFDUPLEX` | 5 | 10 Mbps 半双工 |
| `YT8512C_STATUS_AUTONEGO_NOTDONE` | 6 | 自动协商进行中，请等待 |
| `YT8512C_STATUS_ERROR` | -1 | 通用错误 |
| `YT8512C_STATUS_RESET_TIMEOUT` | -2 | 软件复位超时（>500 ms） |
| `YT8512C_STATUS_ADDRESS_ERROR` | -3 | MDIO 总线上未找到 PHY |
| `YT8512C_STATUS_WRITE_ERROR` | -4 | MDIO 写寄存器失败 |
| `YT8512C_STATUS_READ_ERROR` | -5 | MDIO 读寄存器失败 |

> 状态码数值与 LAN8742 驱动完全一致，`ethernetif.c` 的 `switch-case` 判断逻辑无需修改。

---

## 6. 依赖注入（IO 层）

驱动本身不直接调用任何 HAL 函数，而是通过 `yt8512c_IOCtx_t` 函数指针间接操作硬件。这样做的好处：驱动与平台解耦，同一个驱动可以在不同 MCU 上复用，只需替换 IO 函数。

```c
typedef struct {
    int32_t (*Init)(void);
    int32_t (*DeInit)(void);
    int32_t (*WriteReg)(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
    int32_t (*ReadReg) (uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
    int32_t (*GetTick) (void);
} yt8512c_IOCtx_t;
```

本项目中，`ethernetif.c` 提供了具体实现：

| 函数指针 | 实际实现 |
|----------|---------|
| `ReadReg` | `HAL_ETH_ReadPHYRegister()` |
| `WriteReg` | `HAL_ETH_WritePHYRegister()` |
| `GetTick` | `HAL_GetTick()` |

---

## 7. 关键寄存器说明

### BCR（Basic Control Register，0x00）

| 位 | 宏定义 | 说明 |
|----|--------|------|
| [15] | `YT8512C_BCR_SOFT_RESET` | 写 1 触发软件复位；芯片复位完成后**硬件自动清零**，程序轮询等待 |
| [13] | `YT8512C_BCR_SPEED_SELECT` | 手动速率：1=100 Mbps, 0=10 Mbps（仅在自协商关闭时有效） |
| [12] | `YT8512C_BCR_AUTONEGO_EN` | 自动协商使能 |
| [11] | `YT8512C_BCR_POWER_DOWN` | 掉电模式 |
| [8] | `YT8512C_BCR_DUPLEX_MODE` | 手动双工：1=全双工, 0=半双工 |

### BSR（Basic Status Register，0x01）

| 位 | 宏定义 | 说明 |
|----|--------|------|
| [5] | `YT8512C_BSR_AUTONEGO_CPLT` | 自动协商完成标志 |
| [2] | `YT8512C_BSR_LINK_STATUS` | 链路状态（**注意：该位是锁存低位，必须连续读两次**，第一次清除历史锁存，第二次才是实时值） |

### PHYSTS（PHY Specific Status，0x11，厂商私有）

| 位 | 宏定义 | 说明 |
|----|--------|------|
| [14] | `YT8512C_PHYSTS_SPEED` | 协商后速率：1=100 Mbps, 0=10 Mbps |
| [13] | `YT8512C_PHYSTS_DUPLEX` | 协商后双工：1=全双工, 0=半双工 |
| [12] | `YT8512C_PHYSTS_AUTONEGO_OK` | 自动协商已完成 |

---

## 8. STM32CubeIDE 编译配置

CubeMX 自动生成的 `Debug/sources.mk` 已包含 `Drivers/BSP/Components/yt8512c` 目录（可在文件中确认）。  
如果编译时报 `yt8512c.h: No such file or directory`，需在 IDE 中手动添加：

1. 右键项目 → **Properties**
2. **C/C++ Build → Settings → MCU GCC Compiler → Include paths**
3. 添加路径：`../Drivers/BSP/Components/yt8512c`
4. 右键 `yt8512c` 文件夹 → **Add/Remove from Build Path** → 勾选加入编译

---

## 9. 移植到其他 LAN8742 项目的步骤

1. 复制 `Drivers/BSP/Components/yt8512c/` 整个目录到目标项目
2. 在 `ethernetif.c` 中做以下替换：

   | 替换前 | 替换后 |
   |--------|--------|
   | `#include "lan8742.h"` | `#include "yt8512c.h"` |
   | `lan8742_Object_t LAN8742` | `yt8512c_Object_t YT8512C` |
   | `lan8742_IOCtx_t LAN8742_IOCtx` | `yt8512c_IOCtx_t YT8512C_IOCtx` |
   | `LAN8742_RegisterBusIO(...)` | `YT8512C_RegisterBusIO(...)` |
   | `LAN8742_Init(...)` | `YT8512C_Init(...)` |
   | `LAN8742_GetLinkState(...)` | `YT8512C_GetLinkState(...)` |
   | `LAN8742_STATUS_*` | `YT8512C_STATUS_*` |

3. 将 `yt8512c/` 添加到编译路径（见第 8 节）
