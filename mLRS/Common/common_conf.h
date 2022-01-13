//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// common config
//*******************************************************
#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H
#pragma once


#define VERSION             0023
#define VERSIONONLYSTR      "v0.0.23"
#define SETUPLAYOUT         001       // this should be changed then Setup struct and/or serial changes


//-------------------------------------------------------
// Setup
//-------------------------------------------------------

//#define POWER                          SX1280_POWER_m18_DBM
#define POWER                          SX1280_POWER_0_DBM
//#define POWER                          SX1280_POWER_12p5_DBM


#define SETUP_TX_USE_MBRIDGE            1 // 0: use UART2, 1: use mBridge

#define SETUP_TX_POWER                  POWER

#define SETUP_TX_SEND_RADIO_STATUS      1 // 0: off, 1: RADIO_STATUS, 2: RADIO_STATUS_V2

#define SETUP_TX_CHANNELS_SOURCE        1 // 0: none, 1: mBridge, 2: SPort In

#define SETUP_TX_CHANNE_ORDER           CHANNEL_ORDER_ETAR


#define SETUP_RX_POWER                  POWER

#define SETUP_RX_SERIAL_BAUDRATE        57600

#define SETUP_RX_SEND_RADIO_STATUS      0 // 0: off, 1: RADIO_STATUS, 2: RADIO_STATUS_V2

#define SETUP_RX_ANTENNA                0 // 0: left/default, 1: right

#define SETUP_RX_FAILSAFE_MODE          1 // 0: no signal 1: CH1-CH4 center signal

#define SETUP_RX_CHANNE_ORDER           CHANNEL_ORDER_AETR


#define BIND_DBLWORD                    0x12344281

//#define LORA_SYNCWORD                   0x56 // 0x12 // seems to have no effect !?
//#define FRAME_SYNCWORD                  0x4281
//#define SEED_DBLWORD                    0x12345678


//-------------------------------------------------------
// System Configs
//-------------------------------------------------------

#define FRAME_RATE_MS                   20 // 500 // 20 // 50 Hz


#define FRAME_TX_RX_LEN                 90 // we currently only support equal len


#define CONNECT_TMO_MS                  500

#define LQ_AVERAGING_MS                 1000


#define FHSS_NUM                        24
//#define FHSS_DISABLED


//-------------------------------------------------------
// Derived Defines
//-------------------------------------------------------

#define FRAME_SYNCWORD                  ((uint16_t)(BIND_DBLWORD & 0x0000FFFF))
#define FHSS_SEED                       BIND_DBLWORD


#define CONNECT_TMO_SYSTICKS            SYSTICK_DELAY_MS((uint16_t)( (float)CONNECT_TMO_MS + 0.75f*FRAME_RATE_MS ));

#define CONNECT_LISTEN_HOP_CNT          (uint8_t)(1.5f*FHSS_NUM)

#define CONNECT_SYNC_CNT                5 // (uint8_t)(1.5f * FHSS_NUM)

#define LQ_AVERAGING_PERIOD             (LQ_AVERAGING_MS/FRAME_RATE_MS)


#endif // COMMON_CONFIG_H