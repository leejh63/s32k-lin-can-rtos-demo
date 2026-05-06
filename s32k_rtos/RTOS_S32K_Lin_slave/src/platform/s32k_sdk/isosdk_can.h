#ifndef ISOSDK_CAN_H
#define ISOSDK_CAN_H

#include <stdint.h>

typedef enum
{
    ISOSDK_CAN_TRANSFER_DONE = 0,
    ISOSDK_CAN_TRANSFER_BUSY,
    ISOSDK_CAN_TRANSFER_ERROR
} IsoSdkCanTransferState;

uint8_t                IsoSdk_CanIsSupported(void);
uint8_t                IsoSdk_CanGetDefaultInstance(void);
uint8_t                IsoSdk_CanInitController(uint8_t instance);
uint8_t                IsoSdk_CanInitTxMailbox(uint8_t instance, uint8_t tx_mb_index);
uint8_t                IsoSdk_CanInitRxMailbox(uint8_t instance, uint8_t rx_mb_index);
uint8_t                IsoSdk_CanConfigRxAcceptAll(uint8_t instance, uint8_t rx_mb_index);
uint8_t                IsoSdk_CanStartReceive(uint8_t instance, uint8_t rx_mb_index);
IsoSdkCanTransferState IsoSdk_CanGetTransferState(uint8_t instance, uint8_t mb_index);
uint8_t                IsoSdk_CanReadRxFrame(uint32_t now_ms,
                                             uint32_t *out_id,
                                             uint8_t *out_dlc,
                                             uint8_t *out_is_extended_id,
                                             uint8_t *out_is_remote_frame,
                                             uint8_t *out_data,
                                             uint8_t data_capacity);
uint8_t                IsoSdk_CanSend(uint8_t instance,
                                      uint8_t tx_mb_index,
                                      uint32_t id,
                                      uint8_t dlc,
                                      const uint8_t *data,
                                      uint8_t is_extended_id,
                                      uint8_t is_remote_frame);

#endif
