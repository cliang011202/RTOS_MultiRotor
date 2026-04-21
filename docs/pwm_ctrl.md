# PWM 电机控制驱动说明

## 1. 概述

`pwm_ctrl` 封装了 STM32H743 上 TIM1 / TIM8 的 7 路 PWM 输出，向上层任务提供以电机编号为索引的脉宽控制接口。上层只需关心"给几号电机输出多少微秒"，无需了解定时器句柄、通道号或寄存器细节。

**适用硬件**：正点原子阿波罗v2，STM32H743IIT6。  
**ESC 协议**：标准航模 PWM，50 Hz，脉宽 1000–2000 µs。

---

## 2. 文件位置

```
Core/
├── Inc/
│   └── pwm_ctrl.h   ← 接口声明、宏常量
└── Src/
    └── pwm_ctrl.c   ← 实现：通道映射表 + 四个函数
```

依赖：`Core/Inc/tim.h`（CubeMX 生成，提供 `htim1`、`htim8` 句柄）

---

## 3. 定时器配置

| 参数 | 值 | 说明 |
|------|----|------|
| 时钟源 | APB2 → 200 MHz → TIM 倍频 → **100 MHz** | |
| 预分频 PSC | 100 − 1 | 计数时钟 = 100 MHz ÷ 100 = **1 MHz** |
| 自动重载 ARR | 20000 − 1 | 周期 = 20000 × 1 µs = **20 ms = 50 Hz** |
| 脉宽单位 | 1 计数 = **1 µs** | CCR 数值直接等于脉宽微秒数 |
| 初始脉宽 | 1500（CubeMX 配置） | 中性油门 |

---

## 4. 电机通道映射

| 电机编号 | 定时器 | 通道 | MCU 引脚 |
|----------|--------|------|---------|
| 1 | TIM1 | CH1 | PE9 |
| 2 | TIM1 | CH2 | PE11 |
| 3 | TIM1 | CH3 | PE13 |
| 4 | TIM1 | CH4 | PA11 |
| 5 | TIM8 | CH1 | PC6 |
| 6 | TIM8 | CH2 | PC7 |
| 7 | TIM8 | CH3 | PC8 |

映射在 `pwm_ctrl.c` 的 `s_channels[]` 结构体数组中定义，修改布局只需改此表，上层逻辑不受影响。

---

## 5. 公共 API

### 5.1 初始化流程（按顺序调用）

```c
/* 在 pwmCtrlTask 开头调用，调度器启动后 */
PWM_Init();     /* 启动 7 路 PWM 输出 */
PWM_ArmESC();   /* 输出中性油门，等待 ESC 解锁（阻塞 2000 ms） */
```

### 5.2 函数说明

| 函数 | 说明 |
|------|------|
| `PWM_Init()` | 对 7 个通道依次调用 `HAL_TIM_PWM_Start()`，并将初始脉宽设为 1500 µs |
| `PWM_ArmESC()` | 全通道输出 1500 µs，调用 `osDelay(2000)` 等待 ESC 解锁。**只能在 FreeRTOS 任务中调用** |
| `PWM_SetPulse(motor, us)` | 设置单路脉宽。`motor` 范围 1–7；超出 [1000, 2000] 自动限幅 |
| `PWM_SetAll(pulses[7])` | 一次性更新全部 7 路，`pulses[0]` 对应电机 1，以此类推。**控制循环中使用** |

### 5.3 宏常量

| 宏 | 值 | 说明 |
|----|----|------|
| `PWM_MOTOR_COUNT` | 7 | 电机总数 |
| `PWM_PULSE_MIN_US` | 1000 | 最小脉宽（最小油门） |
| `PWM_PULSE_MAX_US` | 2000 | 最大脉宽（最大油门） |
| `PWM_PULSE_NEUTRAL_US` | 1500 | 中性油门 |
| `PWM_ARM_DELAY_MS` | 2000 | ESC 解锁等待时间 |

---

## 6. 在 pwmCtrlTask 中的使用方式

```c
/* Core/Src/freertos.c — StartpwmCtrlTask */
#include "pwm_ctrl.h"

void StartpwmCtrlTask(void *argument)
{
    PWM_Init();
    PWM_ArmESC();   /* 阻塞 2000 ms，等待 ESC 解锁 */

    uint16_t pulses[PWM_MOTOR_COUNT];

    for (;;)
    {
        /* 从 Queue 接收逆运动学结果（Queue 实现后替换 osDelay） */
        // xQueueReceive(xLoadQueue, &load, portMAX_DELAY);
        // inverse_kinematics(&load, pulses);
        PWM_SetAll(pulses);

        osDelay(20);   /* 占位：50 Hz 节拍 */
    }
}
```

---

## 7. 内部实现要点

### 为什么用 `__HAL_TIM_SET_COMPARE` 而不是重复调用 `HAL_TIM_PWM_Start`？

`HAL_TIM_PWM_Start` 在 `PWM_Init` 时已启动定时器和比较输出。运行中只需修改比较值（CCR 寄存器），`__HAL_TIM_SET_COMPARE` 宏本质上是一条内存写入，开销极低，适合 50 Hz 控制循环。

### 为什么用结构体数组而不是 switch-case？

```c
static const MotorChannel_t s_channels[PWM_MOTOR_COUNT] = {
    { &htim1, TIM_CHANNEL_1 },  /* 电机 1 */
    ...
};
```

数组下标即电机索引，查表 O(1)。布局变化（如第 3 路电机改用 TIM8）只修改一行数据，逻辑代码零改动。

### 为什么 `PWM_SetPulse` 电机编号从 1 开始？

对外接口编号 1–7，与物理电机编号一致，避免调用方写 `motor - 1`。内部 `set_ccr(motor - 1, ...)` 完成转换，边界检查在入口处做。

### 限幅逻辑在哪里？

在内部函数 `set_ccr()` 中统一做，`PWM_SetPulse` 和 `PWM_SetAll` 都经过此函数，不会遗漏。

---

## 8. ESC 解锁说明

大多数航模 ESC 上电后进入等待状态，需要接收到一段时间的中性油门（1500 µs）才会解锁并响应油门变化。`PWM_ArmESC` 即实现此流程：

```
上电 → PWM_Init（输出 1500 µs）→ PWM_ArmESC（持续 2000 ms）→ ESC 解锁 → 开始控制
```

若实际使用的 ESC 解锁时间不同，修改 `PWM_ARM_DELAY_MS` 宏即可。
