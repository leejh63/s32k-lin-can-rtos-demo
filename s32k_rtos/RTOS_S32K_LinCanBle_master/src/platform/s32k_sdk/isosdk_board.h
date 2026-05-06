#ifndef ISOSDK_BOARD_H
#define ISOSDK_BOARD_H

#include <stdint.h>

// IsoSdk_Board를 초기화한다.
uint8_t  IsoSdk_BoardInit(void);
// IsoSdk_Board LIN transceiver를 활성화한다.
void     IsoSdk_BoardEnableLinTransceiver(void);
void    *IsoSdk_BoardGetRgbLedPort(void);
uint32_t IsoSdk_BoardGetRgbLedRedPin(void);
uint32_t IsoSdk_BoardGetRgbLedGreenPin(void);
uint8_t  IsoSdk_BoardGetRgbLedActiveOnLevel(void);
// IsoSdk_Board slave1 버튼 입력을 읽는다.
uint8_t  IsoSdk_BoardReadSlave1ButtonPressed(void);
void     IsoSdk_GpioWritePin(void *gpio_port, uint32_t pin, uint8_t level);
void     IsoSdk_GpioSetPinsDirectionMask(void *gpio_port, uint32_t pin_mask);

#endif
