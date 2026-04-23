#include "pcf8574.h"

/* ── 内部辅助：将影子寄存器写入芯片 ───────────────────────────────────────
 * PCF8574 的写操作极简：I2C Start → 设备地址+W → 1字节数据 → Stop。
 * HAL_I2C_Master_Transmit 封装了这个时序，直接调用即可。
 *
 * 返回值映射：HAL_OK(0) → PCF8574_STATUS_OK(0)，其他 → PCF8574_STATUS_ERROR。
 */
static int32_t write_latch(PCF8574_Object_t *pObj)
{
    if (HAL_I2C_Master_Transmit(pObj->hi2c,
                                pObj->DevAddr,
                                &pObj->OutputLatch,
                                1,
                                PCF8574_I2C_TIMEOUT) != HAL_OK)
        return PCF8574_STATUS_ERROR;

    return PCF8574_STATUS_OK;
}

/* ── PCF8574_Init ───────────────────────────────────────────────────────────
 * 为什么 Init 要立刻写 INIT_STATE？
 * PCF8574 上电后所有引脚默认为高（0xFF）。
 * P7 直连 YT8512C RESET# 引脚（无反相）：P7=0 = RESET# 低 = PHY 复位。
 * 上电后 PCF8574 默认全高（0xFF），P7=1 → PHY 正常，但蜂鸣器（P0=1）也安全。
 * 写入 INIT_STATE(0xD7) 保持 P7=1 并关闭蜂鸣器（P0=1）及其他安全初始值。
 */
int32_t PCF8574_Init(PCF8574_Object_t *pObj, I2C_HandleTypeDef *hi2c, uint16_t devAddr)
{
    if (!pObj || !hi2c)
        return PCF8574_STATUS_ERROR;

    pObj->hi2c     = hi2c;
    pObj->DevAddr  = devAddr;
    pObj->OutputLatch = PCF8574_INIT_STATE;  /* 影子寄存器与硬件同步 */

    return write_latch(pObj);
}

/* ── PCF8574_WriteAll ───────────────────────────────────────────────────────
 * 直接覆盖写全部 8 位，同时更新影子寄存器。
 */
int32_t PCF8574_WriteAll(PCF8574_Object_t *pObj, uint8_t val)
{
    pObj->OutputLatch = val;
    return write_latch(pObj);
}

/* ── PCF8574_ReadAll ────────────────────────────────────────────────────────
 * 读取引脚实际电平（非影子寄存器）。
 *
 * 为什么读到的值可能与写入值不同？
 * PCF8574 是准双向口：写1后引脚由内部弱上拉维持高电平，外部可拉低。
 * 因此读回的值能反映外部信号（如中断引脚 AP_INT、6D_INT 被外部拉低）。
 * 输出为0的引脚，读回固定是0（强驱低，外部无法拉高）。
 */
int32_t PCF8574_ReadAll(PCF8574_Object_t *pObj, uint8_t *val)
{
    if (HAL_I2C_Master_Receive(pObj->hi2c,
                               pObj->DevAddr,
                               val,
                               1,
                               PCF8574_I2C_TIMEOUT) != HAL_OK)
        return PCF8574_STATUS_ERROR;

    return PCF8574_STATUS_OK;
}

/* ── PCF8574_SetPin ─────────────────────────────────────────────────────────
 * 将指定位置1，其余位保持不变（通过影子寄存器实现"读-改-写"）。
 * pin 可以是多个 PCF8574_PIN_xxx 的 OR。
 */
int32_t PCF8574_SetPin(PCF8574_Object_t *pObj, uint8_t pin)
{
    pObj->OutputLatch |= pin;
    return write_latch(pObj);
}

/* ── PCF8574_ResetPin ───────────────────────────────────────────────────────
 * 将指定位清0，其余位保持不变。
 */
int32_t PCF8574_ResetPin(PCF8574_Object_t *pObj, uint8_t pin)
{
    pObj->OutputLatch &= ~pin;
    return write_latch(pObj);
}

/* ── PCF8574_ReadPin ────────────────────────────────────────────────────────
 * 读取指定引脚的实际电平。
 * state: 0 = 低，1 = 高。
 *
 * 注意：读取前该引脚必须已被写1（处于输入模式），否则固定返回0。
 */
int32_t PCF8574_ReadPin(PCF8574_Object_t *pObj, uint8_t pin, uint8_t *state)
{
    uint8_t val;

    if (PCF8574_ReadAll(pObj, &val) != PCF8574_STATUS_OK)
        return PCF8574_STATUS_ERROR;

    *state = (val & pin) ? 1U : 0U;
    return PCF8574_STATUS_OK;
}

/* ── PCF8574_ETH_Reset ──────────────────────────────────────────────────────
 * 对 YT8512C PHY 执行一次硬件复位。
 *
 * 硬件确认：P7 直连（无 NPN 反相）YT8512C RESET# 引脚。
 * 时序：P7=0（RESET# 低，断言复位）→ 等待 ≥10 ms → P7=1（RESET# 高，释放）→ 等待 PLL 锁定。
 * 为什么等 10 ms？YT8512C 数据手册要求 RESET# 低电平持续至少 10 ms。
 * 为什么释放后再等 50 ms？PHY 内部 PLL 和寄存器初始化需要时间，过早访问
 * MDIO 会得到错误读值，导致 YT8512C_Init 找不到 PHY。
 */
int32_t PCF8574_ETH_Reset(PCF8574_Object_t *pObj)
{
    int32_t ret;

    ret = PCF8574_ResetPin(pObj, PCF8574_PIN_ETH_RESET); /* P7=0：RESET# 低，断言复位 */
    if (ret != PCF8574_STATUS_OK)
        return ret;

    HAL_Delay(10);

    ret = PCF8574_SetPin(pObj, PCF8574_PIN_ETH_RESET);   /* P7=1：RESET# 高，释放复位 */
    if (ret != PCF8574_STATUS_OK)
        return ret;

    HAL_Delay(50);  /* 等待 PHY PLL 锁定并输出稳定 REFCLK */
    return PCF8574_STATUS_OK;
}

/* ── PCF8574_BEEP_Set ───────────────────────────────────────────────────────
 * 控制蜂鸣器。on=1 响，on=0 停。
 */
void PCF8574_BEEP_Set(PCF8574_Object_t *pObj, uint8_t on)
{
    /* PNP 反相：P0=0 响，P0=1 静音 */
    if (on)
        PCF8574_ResetPin(pObj, PCF8574_PIN_BEEP);
    else
        PCF8574_SetPin(pObj, PCF8574_PIN_BEEP);
}

/* ── RS485 方向控制 ─────────────────────────────────────────────────────────
 * RS485 收发器（如 MAX485）的 RE/DE 引脚：
 * RE=0（接收使能）同时 DE=0（发送禁止）→ 接收模式
 * RE=1（接收禁止）同时 DE=1（发送使能）→ 发送模式
 * PCF8574 P5 同时控制 RE 和 DE（板上 RE 与 DE 短接），0=接收，1=发送。
 */
void PCF8574_RS485_SetTx(PCF8574_Object_t *pObj)
{
    PCF8574_SetPin(pObj, PCF8574_PIN_RS485_RE);
}

void PCF8574_RS485_SetRx(PCF8574_Object_t *pObj)
{
    PCF8574_ResetPin(pObj, PCF8574_PIN_RS485_RE);
}
