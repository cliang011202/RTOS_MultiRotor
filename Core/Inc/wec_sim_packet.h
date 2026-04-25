#ifndef WEC_SIM_PACKET_H
#define WEC_SIM_PACKET_H

#include <stdint.h>

/* UDP 通信协议
 * 方向：WEC-SIM MOST (Simulink, PC) → STM32 heptarotor
 * 端口：STM32 监听 8080
 * 包长：28 字节（固定）
 * 字节序：小端（x86 Simulink 与 ARM Cortex-M7 均为小端，无需转换）
 */
#define WEC_SIM_UDP_PORT    8080U
#define WEC_SIM_PACKET_SIZE 28U     /* sizeof(WecSimPacket_t)，静态断言保证 */

typedef struct __attribute__((packed)) {
    uint32_t seq;   /* 帧序号：每发一帧加1，用于检测丢包和乱序 */
    float    fx;    /* 塔顶力  X 分量 (N) */
    float    fy;    /* 塔顶力  Y 分量 (N) */
    float    fz;    /* 塔顶力  Z 分量 (N) */
    float    mx;    /* 塔顶力矩 X 分量 (N·m) */
    float    my;    /* 塔顶力矩 Y 分量 (N·m) */
    float    mz;    /* 塔顶力矩 Z 分量 (N·m) */
} WecSimPacket_t;

/* 编译期检查：包大小必须恰好为 28 字节 */
_Static_assert(sizeof(WecSimPacket_t) == WEC_SIM_PACKET_SIZE,
               "WecSimPacket_t size mismatch");

#endif /* WEC_SIM_PACKET_H */
