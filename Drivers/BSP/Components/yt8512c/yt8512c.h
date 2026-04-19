/**
 * yt8512c.h — YT8512C PHY driver for STM32H743 + lwIP
 *
 * 为什么需要这个文件？
 * ethernetif.c 需要知道：PHY 有哪些寄存器地址、有哪些操作函数、返回值是什么含义。
 * 头文件就是这份"说明书"，.c 文件是实现，两者分开是 C 语言的基本模块化方式。
 */

#ifndef YT8512C_H
#define YT8512C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── 1. 标准寄存器地址（IEEE 802.3，所有 PHY 通用）──────────────────────────
 * 为什么写成宏？
 * 避免在代码里出现 0x00、0x01 这种"魔术数字"，让阅读者一眼知道含义。
 * 这些地址与 LAN8742 完全一致，因为 IEEE 802.3 规定了标准寄存器。
 */
#define YT8512C_BCR     0x00U   /* Basic Control Register    软件复位、速率、双工、自协商 */
#define YT8512C_BSR     0x01U   /* Basic Status Register     链路状态、自协商能力 */
#define YT8512C_PHYID1  0x02U   /* PHY Identifier 1          厂商 OUI 高16位 */
#define YT8512C_PHYID2  0x03U   /* PHY Identifier 2          厂商 OUI 低6位+型号+版本 */
#define YT8512C_ANAR    0x04U   /* Auto-Neg Advertisement    本端能力通告 */
#define YT8512C_ANLPAR  0x05U   /* Auto-Neg Link Partner Ability 对端能力 */

/* ── 2. YT8512C 厂商私有寄存器────────────────────────────────────────────────
 * 为什么要单独列出来？
 * 这是 LAN8742 与 YT8512C 驱动的核心差异所在。
 * LAN8742 在 0x1F(PHYSCSR) 查询协商后的速率和双工结果。
 * YT8512C 把同样的信息放在 0x11(PHYSTS)，位域定义也不同。
 * 不改这里就会读错寄存器，导致链路状态永远判断错误。
 */
#define YT8512C_PHYSTS  0x11U   /* PHY Specific Status Register（厂商私有）*/

/* ── 3. BCR 位定义（标准，与 LAN8742 一致）──────────────────────────────────
 * 为什么用移位而不是直接写数字？
 * 位操作时语义清晰：(1U << 15) 明确表示第15位，不容易写错。
 */
#define YT8512C_BCR_SOFT_RESET      (1U << 15)  /* 写1触发软件复位，复位完成后硬件自动清零 */
#define YT8512C_BCR_SPEED_SELECT    (1U << 13)  /* 手动速率选择：1=100Mbps, 0=10Mbps */
#define YT8512C_BCR_AUTONEGO_EN     (1U << 12)  /* 自动协商使能 */
#define YT8512C_BCR_POWER_DOWN      (1U << 11)  /* 掉电模式 */
#define YT8512C_BCR_DUPLEX_MODE     (1U <<  8)  /* 手动双工：1=全双工, 0=半双工 */

/* ── 4. BSR 位定义（标准）───────────────────────────────────────────────────*/
#define YT8512C_BSR_AUTONEGO_CPLT   (1U <<  5)  /* 自动协商完成标志 */
#define YT8512C_BSR_LINK_STATUS     (1U <<  2)  /* 链路状态：1=UP, 0=DOWN
                                                  * 注意：BSR 需要读两次！
                                                  * 第一次读会清除"曾经断开"的锁存位，
                                                  * 第二次读才是当前实时状态。 */

/* ── 5. YT8512C 私有状态寄存器位定义（0x11）────────────────────────────────
 * 为什么这里和 LAN8742 不同？
 * 这是芯片厂商自定义的寄存器，Motorcomm 选择把速率/双工的协商结果放在 bit14:13。
 * LAN8742 把相同信息放在 0x1F 的 bit4:2。所以两个驱动在 GetLinkState 里
 * 读的寄存器地址和位域都不同，这是最关键的移植点。
 */
#define YT8512C_PHYSTS_SPEED        (1U << 14)  /* 协商后速率：1=100Mbps, 0=10Mbps */
#define YT8512C_PHYSTS_DUPLEX       (1U << 13)  /* 协商后双工：1=全双工, 0=半双工 */
#define YT8512C_PHYSTS_AUTONEGO_OK  (1U << 12)  /* 自动协商完成（与 BSR bit5 含义相同） */

/* ── 6. 驱动返回值定义──────────────────────────────────────────────────────
 * 为什么返回值要和 LAN8742 保持一致？
 * ethernetif.c 里用 switch-case 判断链路状态（见 ethernet_link_thread）。
 * 如果我们改变返回值的数值，就必须同时修改 ethernetif.c，改动范围变大。
 * 保持兼容的返回值，ethernetif.c 只需要改 include 和函数名，不改逻辑。
 */
#define YT8512C_STATUS_READ_ERROR           (-5)
#define YT8512C_STATUS_WRITE_ERROR          (-4)
#define YT8512C_STATUS_ADDRESS_ERROR        (-3)
#define YT8512C_STATUS_RESET_TIMEOUT        (-2)
#define YT8512C_STATUS_ERROR                (-1)
#define YT8512C_STATUS_OK                   ( 0)
#define YT8512C_STATUS_LINK_DOWN            ( 1)
#define YT8512C_STATUS_100MBITS_FULLDUPLEX  ( 2)
#define YT8512C_STATUS_100MBITS_HALFDUPLEX  ( 3)
#define YT8512C_STATUS_10MBITS_FULLDUPLEX   ( 4)
#define YT8512C_STATUS_10MBITS_HALFDUPLEX   ( 5)
#define YT8512C_STATUS_AUTONEGO_NOTDONE     ( 6)

/* ── 7. 驱动对象结构体──────────────────────────────────────────────────────
 * 为什么用结构体封装而不用全局变量？
 * 面向对象的 C 风格。把"这个 PHY 的地址、是否初始化、读写函数"
 * 打包成一个对象，将来如果板子上有两个 PHY，可以创建两个对象分别管理。
 * 函数指针（IO 层）是关键：让驱动本身不依赖具体的 HAL，
 * 由 ethernetif.c 在运行时注入 HAL_ETH_ReadPHYRegister/WritePHYRegister，
 * 这样同一个驱动可以在不同 MCU 平台上复用，只换 IO 函数。
 */
typedef int32_t (*yt8512c_Init_Func)(void);
typedef int32_t (*yt8512c_DeInit_Func)(void);
typedef int32_t (*yt8512c_ReadReg_Func)(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
typedef int32_t (*yt8512c_WriteReg_Func)(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
typedef int32_t (*yt8512c_GetTick_Func)(void);

typedef struct {
    yt8512c_Init_Func      Init;
    yt8512c_DeInit_Func    DeInit;
    yt8512c_WriteReg_Func  WriteReg;
    yt8512c_ReadReg_Func   ReadReg;
    yt8512c_GetTick_Func   GetTick;
} yt8512c_IOCtx_t;

typedef struct {
    uint32_t       DevAddr;         /* PHY 地址（由 PHYADD 引脚决定） */
    uint32_t       Is_Initialized;  /* 防止重复初始化 */
    yt8512c_IOCtx_t IO;             /* IO 函数指针集合 */
} yt8512c_Object_t;

/* ── 8. 对外接口声明──────────────────────────────────────────────────────── */
int32_t YT8512C_RegisterBusIO(yt8512c_Object_t *pObj, yt8512c_IOCtx_t *ioctx);
int32_t YT8512C_Init(yt8512c_Object_t *pObj);
int32_t YT8512C_DeInit(yt8512c_Object_t *pObj);
int32_t YT8512C_GetLinkState(yt8512c_Object_t *pObj);

#ifdef __cplusplus
}
#endif
#endif /* YT8512C_H */
