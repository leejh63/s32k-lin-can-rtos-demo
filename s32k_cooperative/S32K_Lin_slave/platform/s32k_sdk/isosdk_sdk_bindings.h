#ifndef ISOSDK_SDK_BINDINGS_H
#define ISOSDK_SDK_BINDINGS_H

#include "sdk_project_config.h"
#include "status.h"

// SDK generated 변수명은 이 파일에서만 직접 참조한다.
// Config Tools 재생성으로 이름이 바뀌어도 상위 IsoSdk 구현은
// 이 매핑만 수정하면 유지되도록 의도한 내부 전용 바인딩 헤더다.
#define ISOSDK_SDK_CLOCK_CONFIGS            g_clockManConfigsArr
#define ISOSDK_SDK_CLOCK_CONFIG_COUNT       CLOCK_MANAGER_CONFIG_CNT
#define ISOSDK_SDK_CLOCK_CALLBACKS          g_clockManCallbacksArr
#define ISOSDK_SDK_CLOCK_CALLBACK_COUNT     CLOCK_MANAGER_CALLBACK_CNT
#define ISOSDK_SDK_PIN_CONFIG_COUNT         NUM_OF_CONFIGURED_PINS0
#define ISOSDK_SDK_PIN_CONFIGS              g_pin_mux_InitConfigArr0

#define ISOSDK_SDK_LPTMR_INSTANCE           INST_LPTMR_1
#define ISOSDK_SDK_LPTMR_CONFIG             lptmr_1_config0
#define ISOSDK_SDK_LPTMR_IRQ                LPTMR0_IRQn

#ifdef INST_FLEXCAN_CONFIG_1
#define ISOSDK_SDK_HAS_CAN                  1
#define ISOSDK_SDK_CAN_INSTANCE             INST_FLEXCAN_CONFIG_1
#define ISOSDK_SDK_CAN_STATE                flexcanState0
#define ISOSDK_SDK_CAN_INIT_CONFIG          flexcanInitConfig0
#endif

#ifdef INST_LIN2
#define ISOSDK_SDK_HAS_LIN                  1
#define ISOSDK_SDK_LIN_INSTANCE             INST_LIN2
#define ISOSDK_SDK_LIN_STATE                lin2_State
#define ISOSDK_SDK_LIN_INIT_CONFIG          lin2_InitConfig0
#endif

#ifdef INST_ADC_CONFIG_1
#define ISOSDK_SDK_HAS_ADC                  1
#define ISOSDK_SDK_ADC_INSTANCE             INST_ADC_CONFIG_1
#define ISOSDK_SDK_ADC_CONVERTER_CONFIG     adc_config_1_ConvConfig0
#define ISOSDK_SDK_ADC_CHANNEL_CONFIG       adc_config_1_ChnConfig0
#endif

#ifdef INST_LPUART_1
#define ISOSDK_SDK_HAS_UART                 1
#define ISOSDK_SDK_UART_INSTANCE            INST_LPUART_1
#define ISOSDK_SDK_UART_STATE               lpUartState1
#define ISOSDK_SDK_UART_INIT_CONFIG         lpuart_1_InitConfig0
#endif

#endif
