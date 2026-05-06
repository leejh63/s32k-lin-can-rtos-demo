// line 지향 UART service API다.
// 상위 계층은 driver를 몰라도,
// buffered transmit과 line 입력, 간단한 오류 복구를 이 인터페이스로 처리한다.
#ifndef UART_SERVICE_H
#define UART_SERVICE_H

#include "../core/infra_types.h"
#include "uart_types.h"

// 공개 UART service API다.
// 호출자는 이 계층으로 초기화와 queue 기반 출력,
// line 지향 입력과 가벼운 transport 진단을 처리한다.
InfraStatus   UartService_Init(UartService *service);
// UartService를 지정 인스턴스로 초기화한다.
InfraStatus   UartService_InitWithInstance(UartService *service, uint32_t instance);
// UartService를 복구한다.
InfraStatus   UartService_Recover(UartService *service);
// UartService 수신 경로를 처리한다.
void          UartService_ProcessRx(UartService *service);
// UartService 송신 경로를 처리한다.
void          UartService_ProcessTx(UartService *service, uint32_t now_ms);
// UartService TX 요청을 적재한다.
InfraStatus   UartService_RequestTx(UartService *service, const char *text);
// UartService에 완성된 입력 줄이 있는지 확인한다.
uint8_t       UartService_HasLine(const UartService *service);
// 완성된 입력 한 줄을 읽고 RX 상태를 정리한다.
InfraStatus   UartService_ReadLine(UartService *service, char *out_buffer, uint16_t max_length);
InfraStatus   UartService_GetCurrentInputText(const UartService *service,
                                              char *out_buffer,
                                              uint16_t max_length);
// 현재 입력 중인 줄 길이를 조회한다.
uint16_t      UartService_GetCurrentInputLength(const UartService *service);
// TX queue에 쌓인 바이트 수를 조회한다.
uint16_t      UartService_GetTxQueueCount(const UartService *service);
// TX queue 총 용량을 조회한다.
uint16_t      UartService_GetTxQueueCapacity(const UartService *service);
// UART TX가 아직 진행 중인지 확인한다.
uint8_t       UartService_IsTxBusy(const UartService *service);
// UART service가 에러 상태인지 확인한다.
uint8_t       UartService_HasError(const UartService *service);
// 현재 UART service 에러 코드를 조회한다.
UartErrorCode UartService_GetErrorCode(const UartService *service);

#endif
