/***************************************************************************//**
 * @file
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#ifndef SILABS_SERIAL_ADAPTER_H
#define SILABS_SERIAL_ADAPTER_H

typedef uint8_t SerialBaudRate;
typedef uint8_t SerialParity;

sl_status_t sli_legacy_serial_init(uint8_t port,
                                   SerialBaudRate rate,
                                   SerialParity parity,
                                   uint8_t stopBits);

sl_status_t sli_legacy_serial_write_byte(uint8_t port, uint8_t dataByte);

uint16_t sli_legacy_serial_write_available(uint8_t port);

sl_status_t sli_legacy_serial_read_byte(uint8_t port, uint8_t *dataByte);

sl_status_t sli_legacy_serial_write_string(uint8_t port, const char *string);

void sli_serial_adapter_tick_callback(void);

#define SERIAL_PORT_NAME_MAX_LEN 4096
extern char serialPort[SERIAL_PORT_NAME_MAX_LEN];

#endif // SILABS_SERIAL_ADAPTER_H
