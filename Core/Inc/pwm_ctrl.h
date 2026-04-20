#ifndef PWM_CTRL_H
#define PWM_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 电机总数 */
#define PWM_MOTOR_COUNT     7U

/*
 * 脉宽范围（单位：微秒）
 * 定时器配置：PSC=100-1，ARR=20000-1，计数时钟 = 1 MHz
 * 因此 1 个计数单位 = 1 µs，脉宽数值直接等于计数值。
 *
 * 为什么是 1000–2000 µs？
 * 这是航模 ESC 的标准脉宽协议。1000 µs = 最小油门，2000 µs = 最大油门。
 * 超出范围会导致 ESC 报错或行为不可预测，所以驱动层做限幅。
 */
#define PWM_PULSE_MIN_US    1000U
#define PWM_PULSE_MAX_US    2000U
#define PWM_PULSE_NEUTRAL_US 1500U

/*
 * ESC 解锁等待时间（ms）
 * ESC 上电后需要先接收到一段稳定的中性油门才会完成解锁。
 * 不同品牌 ESC 有所不同，2000 ms 是保守安全值。
 */
#define PWM_ARM_DELAY_MS    2000U

/*
 * PWM_Init — 启动全部 7 路 PWM 输出
 *
 * 必须在 FreeRTOS 调度器启动后、pwmCtrlTask 开始控制前调用一次。
 * 调用后各通道立即输出当前 CCR 寄存器的脉宽（CubeMX 配置的初始值 1500 µs）。
 */
void PWM_Init(void);

/*
 * PWM_ArmESC — ESC 解锁序列
 *
 * 所有通道输出 PWM_PULSE_NEUTRAL_US，持续 PWM_ARM_DELAY_MS ms。
 * 必须在 PWM_Init 之后调用，等待完成后方可改变油门。
 * 此函数内部调用 osDelay，因此只能在 FreeRTOS 任务中使用。
 */
void PWM_ArmESC(void);

/*
 * PWM_SetPulse — 设置单路电机脉宽
 *
 * motor: 电机编号，1–7
 * pulse_us: 脉宽，单位微秒。超出 [1000, 2000] 范围会被自动限幅，不会报错。
 */
void PWM_SetPulse(uint8_t motor, uint16_t pulse_us);

/*
 * PWM_SetAll — 一次性更新全部 7 路脉宽
 *
 * pulses: 长度为 PWM_MOTOR_COUNT 的数组，pulses[0] 对应电机1，以此类推。
 * 在控制循环中使用，避免 7 次单独调用的开销。
 */
void PWM_SetAll(const uint16_t pulses[PWM_MOTOR_COUNT]);

#ifdef __cplusplus
}
#endif
#endif /* PWM_CTRL_H */
