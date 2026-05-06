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
InfraStatus   UartService_InitWithInstance(UartService *service, uint32_t instance);
InfraStatus   UartService_Recover(UartService *service);
void          UartService_ProcessRx(UartService *service);
void          UartService_ProcessTx(UartService *service, uint32_t now_ms);
InfraStatus   UartService_RequestTx(UartService *service, const char *text);
uint8_t       UartService_HasLine(const UartService *service);
InfraStatus   UartService_ReadLine(UartService *service, char *out_buffer, uint16_t max_length);
InfraStatus   UartService_GetCurrentInputText(const UartService *service,
                                              char *out_buffer,
                                              uint16_t max_length);
uint16_t      UartService_GetCurrentInputLength(const UartService *service);
uint16_t      UartService_GetTxQueueCount(const UartService *service);
uint16_t      UartService_GetTxQueueCapacity(const UartService *service);
uint8_t       UartService_IsTxBusy(const UartService *service);
uint8_t       UartService_HasError(const UartService *service);
UartErrorCode UartService_GetErrorCode(const UartService *service);

#endif
