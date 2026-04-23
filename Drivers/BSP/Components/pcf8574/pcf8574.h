#ifndef PCF8574_H
#define PCF8574_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h7xx_hal.h"

/* ── 1. I2C 设备地址 ────────────────────────────────────────────────────────
 * PCF8574 地址格式：0100 A2 A1 A0（7-bit）
 * 正点原子 Apollo V2 上 A0=A1=A2=0，7-bit 地址 = 0x20
 * STM32 HAL I2C 函数要求传入 8-bit 地址（7-bit << 1），故为 0x40。
 */
#define PCF8574_I2C_ADDR    (0x20U << 1)    /* 0x40 */
#define PCF8574_I2C_TIMEOUT 10U             /* ms */

/* ── 2. 引脚位掩码 ──────────────────────────────────────────────────────────
 * PCF8574 是字节宽度的 IO 口，读写都是一次传输整个字节。
 * 用位掩码表示引脚，便于 SetPin/ResetPin 做位操作。
 */
#define PCF8574_PIN_BEEP        (1U << 0)   /* P0 — 蜂鸣器（低电平响，PNP 反相）*/
#define PCF8574_PIN_AP_INT      (1U << 1)   /* P1 — 光线传感器中断（输入） */
#define PCF8574_PIN_DCMI_PWDN   (1U << 2)   /* P2 — 摄像头/OLED 掉电（高有效） */
#define PCF8574_PIN_USB_PWR     (1U << 3)   /* P3 — USB HOST 电源控制 */
#define PCF8574_PIN_6D_INT      (1U << 4)   /* P4 — 六轴 IMU 中断（输入） */
#define PCF8574_PIN_RS485_RE    (1U << 5)   /* P5 — RS485 方向控制（0=接收，1=发送） */
#define PCF8574_PIN_EXIO        (1U << 6)   /* P6 — 备用 IO（P3 扩展口） */
#define PCF8574_PIN_ETH_RESET   (1U << 7)   /* P7 — PHY 硬件复位（直连 RESET#，无反相）
                                             * P7=1 → RESET# 高 → PHY 正常工作
                                             * P7=0 → RESET# 低 → PHY 处于复位 */

/* ── 3. 上电初始状态 ────────────────────────────────────────────────────────
 * PCF8574 上电默认所有引脚为高（0xFF）。
 * 硬件确认：P7 直连（无 NPN 反相）YT8512C RESET# 引脚：
 *   P7=1 → RESET# HIGH → PHY 正常工作（输出 REFCLK）
 *   P7=0 → RESET# LOW  → PHY 处于复位（无 REFCLK）
 * Init 函数必须尽快写入安全初始值：
 *   P0=1  BEEP 关（PNP 截止，低电平响，须置高才静音）
 *   P1=1  输入引脚置高（准双向口读取前须先写1）
 *   P2=1  DCMI 掉电（不用摄像头，节省功耗）
 *   P3=0  USB 电源关
 *   P4=1  输入引脚置高
 *   P5=0  RS485 接收模式
 *   P6=1  备用高（输入模式）
 *   P7=1  ★ 释放 PHY 复位（RESET# HIGH），以太网才能正常工作
 *
 * 0b 1_1_0_1_0_1_1_1 = 0xD7
 */
#define PCF8574_INIT_STATE  (PCF8574_PIN_BEEP      | \
                             PCF8574_PIN_AP_INT    | \
                             PCF8574_PIN_DCMI_PWDN | \
                             PCF8574_PIN_6D_INT    | \
                             PCF8574_PIN_EXIO      | \
                             PCF8574_PIN_ETH_RESET) /* 0xD7：P7=1 释放 PHY RESET# */

/* ── 4. 返回值 ──────────────────────────────────────────────────────────── */
#define PCF8574_STATUS_OK       ( 0)
#define PCF8574_STATUS_ERROR    (-1)

/* ── 5. 驱动对象 ────────────────────────────────────────────────────────────
 * 为什么需要 OutputLatch（影子寄存器）？
 * PCF8574 是准双向口：写1时引脚可做输入，写0时强驱低。
 * 芯片没有独立的"输出数据寄存器"可读回，读操作返回的是引脚实际电平。
 * 要做 SetPin/ResetPin（只改某一位），必须先知道其他位的当前输出值，
 * 所以驱动在本地维护一个影子字节，记录最后一次写入的输出状态。
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t           DevAddr;
    uint8_t            OutputLatch;  /* 影子寄存器：最后一次写出的字节 */
} PCF8574_Object_t;

/* ── 6. API 声明 ─────────────────────────────────────────────────────────── */

/* 初始化：绑定 I2C 句柄，写入安全初始状态（含释放 PHY 复位） */
int32_t PCF8574_Init(PCF8574_Object_t *pObj, I2C_HandleTypeDef *hi2c, uint16_t devAddr);

/* 底层读写：操作全部 8 位 */
int32_t PCF8574_WriteAll(PCF8574_Object_t *pObj, uint8_t val);
int32_t PCF8574_ReadAll (PCF8574_Object_t *pObj, uint8_t *val);

/* 位操作：pin 为引脚位掩码（PCF8574_PIN_xxx），可多位 OR 同时操作 */
int32_t PCF8574_SetPin  (PCF8574_Object_t *pObj, uint8_t pin);
int32_t PCF8574_ResetPin(PCF8574_Object_t *pObj, uint8_t pin);
int32_t PCF8574_ReadPin (PCF8574_Object_t *pObj, uint8_t pin, uint8_t *state);

/* 具名外设控制 */
int32_t PCF8574_ETH_Reset  (PCF8574_Object_t *pObj);           /* 硬件复位 PHY */
void    PCF8574_BEEP_Set   (PCF8574_Object_t *pObj, uint8_t on); /* 1=响，0=停 */
void    PCF8574_RS485_SetTx(PCF8574_Object_t *pObj);           /* 切换到发送模式 */
void    PCF8574_RS485_SetRx(PCF8574_Object_t *pObj);           /* 切换到接收模式 */

#ifdef __cplusplus
}
#endif
#endif /* PCF8574_H */
