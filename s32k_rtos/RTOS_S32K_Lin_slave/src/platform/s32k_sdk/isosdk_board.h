#ifndef ISOSDK_BOARD_H
#define ISOSDK_BOARD_H

#include <stdint.h>

uint8_t  IsoSdk_BoardInit(void);
void     IsoSdk_BoardEnableLinTransceiver(void);
uint8_t  IsoSdk_PwmIsSupported(void);
uint8_t  IsoSdk_PwmInit(void);
uint8_t  IsoSdk_PwmWriteLogicalDuty(uint8_t channel_index,
                                    uint16_t logical_active_duty,
                                    uint8_t active_on_level);
uint16_t IsoSdk_PwmGetMaxDuty(void);
void    *IsoSdk_BoardGetRgbLedPort(void);
uint32_t IsoSdk_BoardGetRgbLedRedPin(void);
uint32_t IsoSdk_BoardGetRgbLedGreenPin(void);
uint8_t  IsoSdk_BoardGetRgbLedActiveOnLevel(void);
uint8_t  IsoSdk_BoardReadSlave1ButtonPressed(void);
void     IsoSdk_GpioWritePin(void *gpio_port, uint32_t pin, uint8_t level);
void     IsoSdk_GpioSetPinsDirectionMask(void *gpio_port, uint32_t pin_mask);

#endif
