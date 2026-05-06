#ifndef ISOSDK_BOARD_H
#define ISOSDK_BOARD_H

#include <stdint.h>

uint8_t  IsoSdk_BoardInit(void);
void     IsoSdk_BoardEnableLinTransceiver(void);
void    *IsoSdk_BoardGetRgbLedPort(void);
uint32_t IsoSdk_BoardGetRgbLedRedPin(void);
uint32_t IsoSdk_BoardGetRgbLedGreenPin(void);
uint8_t  IsoSdk_BoardGetRgbLedActiveOnLevel(void);
uint8_t  IsoSdk_BoardReadSlave1ButtonPressed(void);
void     IsoSdk_GpioWritePin(void *gpio_port, uint32_t pin, uint8_t level);
void     IsoSdk_GpioSetPinsDirectionMask(void *gpio_port, uint32_t pin_mask);

#endif
