/**
 * yt8512c.c — YT8512C PHY 驱动实现
 *
 * 驱动的职责：
 *   通过 MDIO 总线（以太网管理接口）读写 YT8512C 内部寄存器，
 *   向上层（ethernetif.c / lwIP）提供标准化的链路状态查询接口。
 *
 * MDIO 通信原理：
 *   STM32 ETH MAC 内置 MDIO 控制器，调用 HAL_ETH_ReadPHYRegister(DevAddr, RegAddr)
 *   会在 MDIO/MDC 线上产生标准的 IEEE 802.3 管理帧，PHY 收到后返回寄存器内容。
 *   驱动不需要自己操作 GPIO，HAL 已经封装好了，这就是 IO 函数指针的来源。
 */

#include "yt8512c.h"

/* PHY 地址扫描范围（IEEE 802.3 规定地址 0~31）*/
#define YT8512C_MAX_DEV_ADDR  31U

/* 软件复位超时：等待 BCR[15] 自动清零的最长时间（ms）
 * 为什么要有超时？防止硬件故障时程序死等，造成系统挂死。 */
#define YT8512C_RESET_TIMEOUT 500U

/**
 * YT8512C_RegisterBusIO — 注入 IO 函数指针
 *
 * 为什么要有这一步？
 * 驱动本身不知道也不需要知道底层用什么总线、什么 MCU。
 * ethernetif.c 把具体的读写函数（HAL_ETH_ReadPHYRegister）作为参数传进来，
 * 驱动只调用函数指针。这就是"依赖注入"，让驱动与平台解耦。
 */
int32_t YT8512C_RegisterBusIO(yt8512c_Object_t *pObj, yt8512c_IOCtx_t *ioctx)
{
    if (!pObj || !ioctx->ReadReg || !ioctx->WriteReg || !ioctx->GetTick)
        return YT8512C_STATUS_ERROR;

    pObj->IO = *ioctx;
    return YT8512C_STATUS_OK;
}

/**
 * YT8512C_Init — 初始化 PHY
 *
 * 做了三件事：
 *   1. 扫描 MDIO 总线，找到 YT8512C 的 PHY 地址
 *   2. 软件复位（通过 BCR 寄存器），确保芯片处于已知初始状态
 *   3. 等待复位完成
 *
 * 为什么要扫描而不是直接用固定地址？
 * PHY 地址由板子上 PHYADD 引脚的电平决定，驱动不应该硬编码假设。
 * 扫描的方式更健壮：读每个地址的 PHY ID 寄存器，找到 YT8512C 的 OUI 就确认了。
 * 注意：如果你确定地址是 0x00，也可以跳过扫描直接用固定地址，但要写在注释里说明原因。
 */
int32_t YT8512C_Init(yt8512c_Object_t *pObj)
{
    uint32_t regval = 0;
    uint32_t addr;
    int32_t  status = YT8512C_STATUS_ERROR;
    uint32_t tickstart;

    if (pObj->Is_Initialized)
        return YT8512C_STATUS_OK;

    if (pObj->IO.Init)
        pObj->IO.Init();

    /* ── 步骤1：扫描 PHY 地址 ─────────────────────────────────────────────
     * 读 PHY ID2 寄存器（0x03）识别芯片。
     * 为什么用 ID2 而不是 ID1？
     * ID2 的 [9:4] 是厂商型号编码，可以精确识别芯片型号。
     * 如果能读到任何有效值（不是全0或全F），说明该地址有 PHY 应答。
     *
     * YT8512C 的 PHY ID：
     *   PHYID1(0x02) = 0x0000（Motorcomm OUI 高位部分）
     *   PHYID2(0x03) = 0x0128（型号编码，不同硅版本低4位可能不同）
     * 为了兼容不同版本，只检查型号字段 [9:4] = 0x12 即可。
     */
    pObj->DevAddr = YT8512C_MAX_DEV_ADDR + 1;  /* 初始值=无效，用于判断是否找到 */

    for (addr = 0; addr <= YT8512C_MAX_DEV_ADDR; addr++)
    {
        if (pObj->IO.ReadReg(addr, YT8512C_PHYID2, &regval) < 0)
            continue;   /* 该地址无应答，继续扫描下一个 */

        /* 检查型号字段 [9:4]，YT8512C = 0x12 */
        if (((regval >> 4) & 0x3FU) == 0x12U)
        {
            pObj->DevAddr = addr;
            status = YT8512C_STATUS_OK;
            break;
        }
    }

    if (pObj->DevAddr > YT8512C_MAX_DEV_ADDR)
        return YT8512C_STATUS_ADDRESS_ERROR;

    /* ── 步骤2：软件复位 ──────────────────────────────────────────────────
     * 为什么要复位？
     * 上电后 PHY 可能处于不确定状态（尤其是热复位或调试时重下载）。
     * 软件复位让所有寄存器回到数据手册定义的默认值，保证行为可预期。
     *
     * 操作方法：写 BCR[15]=1，芯片完成复位后硬件自动将该位清零。
     * 所以要"先写1触发，再轮询等它变0"，而不是自己写回0。
     */
    if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_BCR, &regval) < 0)
        return YT8512C_STATUS_READ_ERROR;

    regval |= YT8512C_BCR_SOFT_RESET;

    if (pObj->IO.WriteReg(pObj->DevAddr, YT8512C_BCR, regval) < 0)
        return YT8512C_STATUS_WRITE_ERROR;

    /* ── 步骤3：等待复位完成 ──────────────────────────────────────────────
     * 为什么用 GetTick() 而不是简单的 for 循环延时？
     * for 循环延时在不同优化等级下行为不可预测，而且浪费 CPU。
     * 用系统时钟 tick 计时，精度准确，也符合驱动"只依赖注入接口"的原则。
     */
    tickstart = (uint32_t)pObj->IO.GetTick();

    do {
        if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_BCR, &regval) < 0)
            return YT8512C_STATUS_READ_ERROR;

        if ((uint32_t)pObj->IO.GetTick() - tickstart > YT8512C_RESET_TIMEOUT)
            return YT8512C_STATUS_RESET_TIMEOUT;

    } while (regval & YT8512C_BCR_SOFT_RESET);  /* BCR[15] 清零 = 复位完成 */

    pObj->Is_Initialized = 1;
    return YT8512C_STATUS_OK;
}

/**
 * YT8512C_DeInit
 */
int32_t YT8512C_DeInit(yt8512c_Object_t *pObj)
{
    if (pObj->Is_Initialized)
    {
        if (pObj->IO.DeInit && pObj->IO.DeInit() < 0)
            return YT8512C_STATUS_ERROR;
        pObj->Is_Initialized = 0;
    }
    return YT8512C_STATUS_OK;
}

/**
 * YT8512C_GetLinkState — 查询当前链路状态
 *
 * 这是驱动中最关键的函数，也是与 LAN8742 差异最大的地方。
 *
 * 返回值会被 ethernetif.c 的 ethernet_link_thread 每 100ms 调用一次，
 * 用来决定是否需要更新 MAC 速率配置和 lwIP 网络接口状态。
 *
 * 判断逻辑（分三层）：
 *   第一层：BSR[2] = 0 → 物理链路断开（网线没插或对端断电）
 *   第二层：BCR[12] = 0 → 未开启自动协商（手动模式，直接读 BCR 判断速率）
 *   第三层：PHYSTS[14:13] → 自动协商完成后的实际速率和双工结果
 *
 * ★ LAN8742 vs YT8512C 的核心差异在第三层：
 *   LAN8742：读 PHYSCSR(0x1F) bit[4:2]
 *   YT8512C：读 PHYSTS(0x11)  bit[14:13]
 */
int32_t YT8512C_GetLinkState(yt8512c_Object_t *pObj)
{
    uint32_t bsr, bcr, physts;

    /* ── 第一层：检查物理链路状态（BSR 寄存器）──────────────────────────
     * 为什么要读两次 BSR？
     * BSR[2](Link Status) 是一个"锁存低"位：一旦链路曾经断开，
     * 即使现在已恢复，第一次读时该位仍然是0（保留了断开的历史）。
     * 读完一次会自动清除锁存，第二次读才反映当前真实状态。
     * 这是 IEEE 802.3 的规定，不是 YT8512C 特有的行为。
     */
    if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_BSR, &bsr) < 0)
        return YT8512C_STATUS_READ_ERROR;
    if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_BSR, &bsr) < 0)  /* 第二次读取实时状态 */
        return YT8512C_STATUS_READ_ERROR;

    if (!(bsr & YT8512C_BSR_LINK_STATUS))
        return YT8512C_STATUS_LINK_DOWN;

    /* ── 第二层：读 BCR 判断是否使用自动协商 ────────────────────────── */
    if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_BCR, &bcr) < 0)
        return YT8512C_STATUS_READ_ERROR;

    if (!(bcr & YT8512C_BCR_AUTONEGO_EN))
    {
        /* 手动模式：直接从 BCR 读速率和双工配置 */
        if ((bcr & YT8512C_BCR_SPEED_SELECT) && (bcr & YT8512C_BCR_DUPLEX_MODE))
            return YT8512C_STATUS_100MBITS_FULLDUPLEX;
        if (bcr & YT8512C_BCR_SPEED_SELECT)
            return YT8512C_STATUS_100MBITS_HALFDUPLEX;
        if (bcr & YT8512C_BCR_DUPLEX_MODE)
            return YT8512C_STATUS_10MBITS_FULLDUPLEX;
        return YT8512C_STATUS_10MBITS_HALFDUPLEX;
    }

    /* ── 第三层：自动协商模式——读 YT8512C 私有寄存器 0x11 ─────────────
     * ★ 这里是与 LAN8742 驱动的唯一核心差异 ★
     *
     * 自动协商完成后，PHY 把"实际协商结果"写入私有寄存器。
     * YT8512C 选择把这个信息放在 PHYSTS(0x11):
     *   bit[14] = 1: 100Mbps     bit[14] = 0: 10Mbps
     *   bit[13] = 1: 全双工       bit[13] = 0: 半双工
     *   bit[12] = 1: 协商已完成
     *
     * LAN8742 把相同信息放在 PHYSCSR(0x1F) bit[4:2]，编码方式也不同。
     * 如果不换这里，就会读错寄存器，得到错误的速率判断，
     * MAC 配置错误后以太网帧会大量出错。
     */
    if (pObj->IO.ReadReg(pObj->DevAddr, YT8512C_PHYSTS, &physts) < 0)
        return YT8512C_STATUS_READ_ERROR;

    if (!(physts & YT8512C_PHYSTS_AUTONEGO_OK))
        return YT8512C_STATUS_AUTONEGO_NOTDONE;   /* 协商尚未完成，等待 */

    if ((physts & YT8512C_PHYSTS_SPEED) && (physts & YT8512C_PHYSTS_DUPLEX))
        return YT8512C_STATUS_100MBITS_FULLDUPLEX;
    if (physts & YT8512C_PHYSTS_SPEED)
        return YT8512C_STATUS_100MBITS_HALFDUPLEX;
    if (physts & YT8512C_PHYSTS_DUPLEX)
        return YT8512C_STATUS_10MBITS_FULLDUPLEX;
    return YT8512C_STATUS_10MBITS_HALFDUPLEX;
}
