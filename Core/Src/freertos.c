/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "pwm_ctrl.h"
#include "usart.h"
#include "lwip.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "wec_sim_packet.h"
extern struct netif gnetif;
volatile uint32_t g_eth_rx_irq_count = 0;  /* incremented in RxCpltCallback */

/* Stack overflow hook: bypass mutex/printf (kernel state is already trashed),
 * raw-poll UART so the task name reaches the console before we halt. */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    const char *prefix = "\r\n*** STACK OVERFLOW: ";
    HAL_UART_Transmit(&huart1, (uint8_t *)prefix, (uint16_t)strlen(prefix), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)pcTaskName,
                      (uint16_t)strlen(pcTaskName), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)" ***\r\n", 6, 100);
    __disable_irq();
    for (;;) {}
}
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Thread-safe printf: serialises all UART output through a single mutex.
 * HAL_UART_Transmit is NOT re-entrant; calling it from two tasks concurrently
 * corrupts huart1.gState and causes UART_WaitOnFlagUntilTimeout to spin
 * forever (especially with HAL_MAX_DELAY). */
#define LOG(...)  do {                                      \
    osMutexAcquire(g_uart_mutex, osWaitForever);            \
    printf(__VA_ARGS__);                                    \
    osMutexRelease(g_uart_mutex);                           \
} while (0)
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osMutexId_t g_uart_mutex;   /* extern'd in lwip.c */
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for udpRxTask */
osThreadId_t udpRxTaskHandle;
const osThreadAttr_t udpRxTask_attributes = {
  .name = "udpRxTask",
  .stack_size = 2048 * 4,   /* 8 KB: printf with 6 doubles (newlib full) needs ~3 KB stack */
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for pwmCtrlTask */
osThreadId_t pwmCtrlTaskHandle;
const osThreadAttr_t pwmCtrlTask_attributes = {
  .name = "pwmCtrlTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartudpRxTask(void *argument);
void StartpwmCtrlTask(void *argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  g_uart_mutex = osMutexNew(NULL);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of udpRxTask */
  udpRxTaskHandle = osThreadNew(StartudpRxTask, NULL, &udpRxTask_attributes);

  /* creation of pwmCtrlTask */
  pwmCtrlTaskHandle = osThreadNew(StartpwmCtrlTask, NULL, &pwmCtrlTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN StartDefaultTask */
  /* Wait up to 5 s for Ethernet link (ethernet_link_thread polls PHY every 100 ms) */
  for (int i = 0; i < 50; i++) {
      if (netif_is_link_up(&gnetif)) break;
      osDelay(100);
  }

  if (netif_is_link_up(&gnetif))
      LOG("[ETH] Link UP  IP=%s\r\n", ip4addr_ntoa(netif_ip4_addr(&gnetif)));
  else
      LOG("[ETH] Link DOWN after 5 s\r\n");

  for(;;)
  {
    LOG("[ETH] RxIRQ=%lu\r\n", g_eth_rx_irq_count);
    osDelay(2000);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartudpRxTask */
/**
* @brief Function implementing the udpRxTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartudpRxTask */
void StartudpRxTask(void *argument)
{
  /* USER CODE BEGIN StartudpRxTask */
  int sock;
  struct sockaddr_in local, remote;
  socklen_t remote_len = sizeof(remote);
  WecSimPacket_t pkt;
  int rxlen;
  uint32_t last_seq = 0;

  /* 创建 UDP socket */
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
      LOG("[UDP] socket() failed\r\n");
      vTaskDelete(NULL);
      return;
  }

  /* 绑定到 0.0.0.0:8080 */
  memset(&local, 0, sizeof(local));
  local.sin_family      = AF_INET;
  local.sin_addr.s_addr = INADDR_ANY;
  local.sin_port        = htons(WEC_SIM_UDP_PORT);

  if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
      LOG("[UDP] bind() failed\r\n");
      close(sock);
      vTaskDelete(NULL);
      return;
  }

  /* 设置接收超时：防止 recvfrom 在网络异常时永久阻塞 */
  struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  LOG("[UDP] Listening on port %u\r\n", WEC_SIM_UDP_PORT);

  for (;;) {
      rxlen = recvfrom(sock, &pkt, sizeof(pkt), 0,
                       (struct sockaddr *)&remote, &remote_len);

      /* recvfrom 超时或错误（errno=EAGAIN/ETIMEDOUT）：静默继续，不 printf 防止 mutex 争用 */
      if (rxlen < 0) {
          continue;
      }

      /* 长度校验 */
      if (rxlen != (int)sizeof(pkt)) {
          LOG("[UDP] Bad len=%d (expect %u)\r\n", rxlen, WEC_SIM_PACKET_SIZE);
          continue;
      }

      /* 丢包检测 */
      if (pkt.seq != last_seq + 1 && last_seq != 0)
          LOG("[UDP] Drop! expect seq=%lu got %lu\r\n",
                 (unsigned long)(last_seq + 1), (unsigned long)pkt.seq);
      last_seq = pkt.seq;

      /* 打印 6-DOF 载荷（整数格式：避免 %f 触发 dtoa 占用 ~3 KB 栈） */
      LOG("[UDP] seq=%-6lu  F=(%d %d %d) N   M=(%d %d %d) Nm\r\n",
             (unsigned long)pkt.seq,
             (int)pkt.fx, (int)pkt.fy, (int)pkt.fz,
             (int)pkt.mx, (int)pkt.my, (int)pkt.mz);
  }
  /* USER CODE END StartudpRxTask */
}

/* USER CODE BEGIN Header_StartpwmCtrlTask */
/**
* @brief Function implementing the pwmCtrlTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartpwmCtrlTask */
void StartpwmCtrlTask(void *argument)
{
  /* USER CODE BEGIN StartpwmCtrlTask */
  /* Verify TIM1 config before starting PWM — expected: PSC=99 ARR=19999 CCR1=1500 */
  LOG("[PWM] TIM1 PSC=%lu  ARR=%lu  CCR1=%lu\r\n",
      TIM1->PSC, TIM1->ARR, TIM1->CCR1);

  PWM_Init();
  LOG("[PWM] Init done - 7 channels active\r\n");
  PWM_ArmESC();   /* 输出 1500 µs 中性油门，等待 ESC 解锁 */
  LOG("[PWM] ESC armed\r\n");

  /* ── 扫频验证：给示波器一个可观测的变化波形 ──────────────────── */
  LOG("[PWM] Sweep start: 1000->2000->1500 us\r\n");
  uint16_t sweep[PWM_MOTOR_COUNT];
  for (uint16_t p = 1000; p <= 2000; p += 10) {
      for (int j = 0; j < PWM_MOTOR_COUNT; j++) sweep[j] = p;
      PWM_SetAll(sweep);
      osDelay(20);
  }
  for (uint16_t p = 2000; p >= 1000; p -= 10) {
      for (int j = 0; j < PWM_MOTOR_COUNT; j++) sweep[j] = p;
      PWM_SetAll(sweep);
      osDelay(20);
  }
  for (int j = 0; j < PWM_MOTOR_COUNT; j++) sweep[j] = PWM_PULSE_NEUTRAL_US;
  PWM_SetAll(sweep);
  LOG("[PWM] Sweep done -- back to neutral 1500 us\r\n");
  /* ────────────────────────────────────────────────────────────── */

  /* 控制循环：等待逆运动学结果（Queue 待实现），更新 7 路脉宽 */
  for(;;)
  {
    /* TODO: xQueueReceive(xLoadQueue, &load, portMAX_DELAY);
     *       inverse_kinematics(&load, pulses);
     *       PWM_SetAll(pulses);
     */
    osDelay(20);  /* 占位：50 Hz 节拍，待 Queue 实现后替换 */
  }
  /* USER CODE END StartpwmCtrlTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

