#include "pwm_ctrl.h"
#include "tim.h"
#include "cmsis_os.h"   /* osDelay */

/*
 * 通道映射表
 *
 * 为什么用结构体数组而不是 switch-case？
 * 数组下标即电机索引，查表 O(1)，添加/修改通道只改一行数据，不改逻辑。
 * 如果将来电机布局变化，只需修改这张表，上层控制代码不受影响。
 */
typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t           channel;
} MotorChannel_t;

static const MotorChannel_t s_channels[PWM_MOTOR_COUNT] = {
    { &htim1, TIM_CHANNEL_1 },  /* 电机 1 — PE9  */
    { &htim1, TIM_CHANNEL_2 },  /* 电机 2 — PA9  */
    { &htim1, TIM_CHANNEL_3 },  /* 电机 3 — PA10 */
    { &htim1, TIM_CHANNEL_4 },  /* 电机 4 — PA11 */
    { &htim8, TIM_CHANNEL_1 },  /* 电机 5 — PC6  */
    { &htim8, TIM_CHANNEL_2 },  /* 电机 6 — PC7  */
    { &htim8, TIM_CHANNEL_3 },  /* 电机 7 — PC8  */
};

/* ── 内部辅助：限幅 + 写 CCR ─────────────────────────────────────────── */
static inline void set_ccr(uint8_t idx, uint16_t pulse_us)
{
    if (pulse_us < PWM_PULSE_MIN_US) pulse_us = PWM_PULSE_MIN_US;
    if (pulse_us > PWM_PULSE_MAX_US) pulse_us = PWM_PULSE_MAX_US;

    /*
     * 为什么直接写 CCR 而不是调用 HAL_TIM_PWM_Start？
     * HAL_TIM_PWM_Start 在 PWM_Init 时已经启动了定时器和比较输出。
     * 运行中只需要修改比较值（CCR），HAL 提供 __HAL_TIM_SET_COMPARE 宏，
     * 本质上就是一条寄存器写入，比再次调用 Start 轻量得多。
     */
    __HAL_TIM_SET_COMPARE(s_channels[idx].htim, s_channels[idx].channel, pulse_us);
}

/* ── 公共接口实现 ─────────────────────────────────────────────────────── */

void PWM_Init(void)
{
    for (uint8_t i = 0; i < PWM_MOTOR_COUNT; i++)
    {
        /*
         * HAL_TIM_PWM_Start 做三件事：
         *   1. 配置输出比较模式为 PWM
         *   2. 使能对应通道的捕获/比较输出
         *   3. 启动定时器（如果还没启动）
         * 必须每个通道单独调用，TIM1/TIM8 共用同一个定时器计数器，
         * 多次调用 Start 不会重置计数，只是使能对应通道。
         */
        HAL_TIM_PWM_Start(s_channels[i].htim, s_channels[i].channel);

        /* 初始化为中性油门，防止 ESC 在解锁前收到随机脉宽 */
        set_ccr(i, PWM_PULSE_NEUTRAL_US);
    }
}

void PWM_ArmESC(void)
{
    /*
     * ESC 解锁原理：
     * 绝大多数航模 ESC 上电后进入"等待校准"状态，
     * 连续接收到稳定的中性油门信号（1500 µs）一段时间后才会解锁并响应油门变化。
     * 提前改变脉宽会导致 ESC 忽略指令或进入错误模式。
     */
    for (uint8_t i = 0; i < PWM_MOTOR_COUNT; i++)
        set_ccr(i, PWM_PULSE_NEUTRAL_US);

    osDelay(PWM_ARM_DELAY_MS);
}

void PWM_SetPulse(uint8_t motor, uint16_t pulse_us)
{
    if (motor < 1 || motor > PWM_MOTOR_COUNT)
        return;   /* 无效电机编号，静默忽略 */

    set_ccr(motor - 1, pulse_us);  /* 对外编号 1~7，内部索引 0~6 */
}

void PWM_SetAll(const uint16_t pulses[PWM_MOTOR_COUNT])
{
    for (uint8_t i = 0; i < PWM_MOTOR_COUNT; i++)
        set_ccr(i, pulses[i]);
}
