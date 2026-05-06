// LIN sensor slave 슬림 프로젝트 전용 설정 헤더다.
// 센서 노드가 실제로 쓰는 로컬 node id와 task 주기,
// ADC/LIN 바인딩에 필요한 최소 상수만 남긴다.
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_NODE_ID_SLAVE2       3U

#define APP_TASK_LIN_FAST_MS      1U
#define APP_TASK_ADC_MS           20U
#define APP_TASK_LIN_POLL_MS      20U
#define APP_TASK_LED_MS           100U
#define APP_TASK_HEARTBEAT_MS     1000U

#endif
