//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
//
/********************************************************

v0.0.00:
*/

#define DBG_MAIN(x)
#define DBG_MAIN_SLIM(x)


// we set the priorities here to have an overview
#define SX_DIO1_EXTI_IRQ_PRIORITY   11
#define UART_IRQ_PRIORITY           10 // mbridge, this needs to be high, when lower than DIO1, the module could stop sending via the bridge
#define UARTB_IRQ_PRIORITY          14 // serial
#define UARTC_IRQ_PRIORITY          14

#include "..\Common\common_conf.h"
#include "..\Common\hal\glue.h"
#include "..\modules\stm32ll-lib\src\stdstm32.h"
#include "..\modules\stm32ll-lib\src\stdstm32-peripherals.h"
#include "..\Common\hal\hal.h"
#include "..\modules\stm32ll-lib\src\stdstm32-delay.h"
#include "..\modules\stm32ll-lib\src\stdstm32-spi.h"
#include "..\modules\sx12xx-lib\src\sx128x.h"
#include "..\modules\stm32ll-lib\src\stdstm32-uartb.h"
#include "..\modules\stm32ll-lib\src\stdstm32-uartc.h"
#include "..\Common\fhss.h"
#define FASTMAVLINK_IGNORE_WADDRESSOFPACKEDMEMBER
#include "..\Common\mavlink\out\mlrs\mlrs.h"
#include "..\Common\common.h"

#include "mbridge_interface.h"
#include "txstats.h"


#define CLOCK_TIMx                  TIM3

void clock_init(void)
{
  tim_init_1us_freerunning(CLOCK_TIMx);
}

uint16_t micros(void)
{
  return CLOCK_TIMx->CNT;
}


void init(void)
{
  leds_init();
  button_init();
  pos_switch_init();

  delay_init();
  clock_init();
  serial.Init();

  uartc_init();

  sx.Init();
}


//-------------------------------------------------------
// Statistics for Transmitter
//-------------------------------------------------------

static inline bool connected(void);

class TxStats : public TxStatsBase
{
  bool is_connected(void) override { return connected(); }
};

TxStats txstats;


//-------------------------------------------------------
// mavlink
//-------------------------------------------------------

#include "mavlink_interface.h"


//-------------------------------------------------------
// SX1280
//-------------------------------------------------------

volatile uint16_t irq_status;

IRQHANDLER(
void SX_DIO1_EXTI_IRQHandler(void)
{
  LL_EXTI_ClearFlag_0_31(SX_DIO1_EXTI_LINE_x);
  //LED_RIGHT_RED_TOGGLE;
  irq_status = sx.GetIrqStatus();
  sx.ClearIrqStatus(SX1280_IRQ_ALL);
  if (irq_status & SX1280_IRQ_RX_DONE) {
    sx.ReadFrame((uint8_t*)&rxFrame, FRAME_TX_RX_LEN);
  }
})


typedef enum {
    CONNECT_STATE_LISTEN = 0,
    CONNECT_STATE_SYNC,
    CONNECT_STATE_CONNECTED,
} CONNECT_STATE_ENUM;

typedef enum {
    LINK_STATE_IDLE = 0,
    LINK_STATE_TRANSMIT,
    LINK_STATE_TRANSMIT_WAIT,
    LINK_STATE_RECEIVE,
    LINK_STATE_RECEIVE_WAIT,
    LINK_STATE_RECEIVE_DONE,
} LINK_STATE_ENUM;


void do_transmit(bool set_ack) // we send a TX frame to receiver
{
  uint8_t payload[FRAME_TX_PAYLOAD_LEN] = {0};
  uint8_t payload_len = 0;

  if (connected()) {
    for (uint8_t i = 0; i < FRAME_TX_PAYLOAD_LEN; i++) {
#if (SETUP_TX_USE_MBRIDGE == 1)
      if (!bridge.available()) break;
      payload[payload_len] = bridge.getc();
#else
      if (!serial.available()) break;
      payload[payload_len] = serial.getc();
#endif
      payload_len++;
    }
  } else {
#if (SETUP_TX_USE_MBRIDGE == 1)
    bridge.flush();
#else
      serial.flush();
#endif
  }

  stats.AddBytesTransmitted(payload_len);

  tFrameStats frame_stats;
  frame_stats.seq_no = stats.tx_seq_no; stats.tx_seq_no++;
  frame_stats.ack = (set_ack) ? 1 : 0;
  frame_stats.antenna = ANTENNA_1;
  frame_stats.rssi = stats.last_rx_rssi;
  frame_stats.snr = stats.last_rx_snr;
  frame_stats.LQ = txstats.GetLQ();

  pack_tx_frame(&txFrame, &frame_stats, &rcData, payload, payload_len);
  sx.SendFrame((uint8_t*)&txFrame, FRAME_TX_RX_LEN, 10); // 10 ms tmo
}


uint8_t check_received_frame(void) // we receive a RX frame from receiver
{
  uint8_t err = check_rx_frame(&rxFrame);

  if (err) {
    DBG_MAIN(uartc_puts("fail "); uartc_putc('\n');)
uartc_puts("fail "); uartc_puts(u8toHEX_s(err));uartc_putc('\n');
  }

  return err;
}


void process_received_frame(void)
{
  stats.received_antenna = rxFrame.status.antenna;
  stats.received_rssi = -(rxFrame.status.rssi_u7);
  stats.received_LQ = rxFrame.status.LQ;

  stats.received_seq_no = rxFrame.status.seq_no;
  stats.received_ack = rxFrame.status.ack;

  for (uint8_t i = 0; i < rxFrame.status.payload_len; i++) {
    uint8_t c = rxFrame.payload[i];
#if (SETUP_TX_USE_MBRIDGE == 1)
    bridge.putc(c); // send to radio
#else
    serial.putc(c); // send to serial
#endif

    uint8_t res = fmav_parse_to_frame_buf(&f_result, f_buf, &f_status, c);
    if (res == FASTMAVLINK_PARSE_RESULT_OK) { // we have a complete mavlink frame
      if (inject_radio_status) {
        inject_radio_status = false;
#if (SETUP_TX_SEND_RADIO_STATUS == 1)
        send_radio_status();
#elif (SETUP_TX_SEND_RADIO_STATUS == 2)
        send_radio_status_v2();
#endif
      }
    }
  }

  stats.AddBytesReceived(rxFrame.status.payload_len);

  DBG_MAIN(char s[16];
  uartc_puts("got "); uartc_puts(s); uartc_puts(": ");
  for (uint8_t i = 0; i < rxFrame.status.payload_len; i++) uartc_putc(rxFrame.payload[i]);
  uartc_putc('\n');)
}


bool do_receive(void)
{
bool ok = false;

  uint8_t res = check_received_frame(); // returns CHECK enum

  if (res == CHECK_OK) {
    process_received_frame();
	  txstats.SetValidFrameReceived();
	  ok = true;
  }

  if (res != CHECK_ERROR_SYNCWORD) {
    // read it here, we want to have it even if it's a bad packet, but it should be for us
    sx.GetPacketStatus(&stats.last_rx_rssi, &stats.last_rx_snr);

    // we count all received frames, which are at least for us
    txstats.SetFrameReceived();
  }

  return ok;
}


//##############################################################################################################
//*******************************************************
// MAIN routine
//*******************************************************

uint16_t led_blink;
uint16_t tick_1hz;

uint16_t tx_tick;
uint16_t link_state;
uint8_t connect_state;
uint16_t connect_tmo_cnt;
uint8_t connect_sync_cnt;


static inline bool connected(void)
{
  return (connect_state == CONNECT_STATE_CONNECTED);
}


int main_main(void)
{
  init();
#if (SETUP_TX_USE_MBRIDGE == 1)
  bridge.Init();
#endif

  DBG_MAIN(uartc_puts("\n\n\nHello\n\n");)

  // startup sign of life
  LED_RED_OFF;
  for (uint8_t i = 0; i < 7; i++) { LED_RED_TOGGLE; delay_ms(50); }

  // start up sx
  if (!sx.isOk()) {
    while (1) { LED_RED_TOGGLE; delay_ms(50); } // fail!
  }
  sx.StartUp();
  fhss.Init(FHSS_SEED);
  fhss.StartTx();
  sx.SetRfFrequency(fhss.GetCurrFreq());

//  for (uint8_t i = 0; i < fhss.Cnt(); i++) {
//    uartc_puts("c = "); uartc_puts(u8toBCD_s(fhss.ch_list[i])); uartc_puts(" f = "); uartc_puts(u32toBCD_s(fhss.fhss_list[i])); uartc_puts("\n"); delay_ms(50);
//  }

  tx_tick = 0;
  link_state = LINK_STATE_IDLE;
  connect_state = CONNECT_STATE_LISTEN;
  connect_tmo_cnt = 0;
  connect_sync_cnt = 0;

  txstats.Init(LQ_AVERAGING_PERIOD);

  f_init();

  led_blink = 0;
  tick_1hz = 0;
  doSysTask = 0; // helps in avoiding too short first loop
  while (1) {

    //-- SysTask handling

    if (doSysTask) {
      doSysTask = 0;

      if (connect_tmo_cnt) {
        connect_tmo_cnt--;
      }

      DECc(tick_1hz, SYSTICK_DELAY_MS(1000));
      DECc(tx_tick, SYSTICK_DELAY_MS(FRAME_RATE_MS));
      if (connected()) {
        DECc(led_blink, SYSTICK_DELAY_MS(500));
      } else {
        DECc(led_blink, SYSTICK_DELAY_MS(200));
      }

      if (!led_blink) {
        if (connected()) LED_GREEN_TOGGLE; else LED_RED_TOGGLE;
      }
      if (connected()) { LED_RED_OFF; } else { LED_GREEN_OFF; }

      if (!connected()) {
        stats.Clear();
        f_init();
      }

      if (!tick_1hz) {
        txstats.Update1Hz();
        if (connected()) inject_radio_status = true;

        uartc_puts("TX: ");
        uartc_puts(u8toBCD_s(txstats.GetRawLQ())); uartc_putc(',');
        uartc_puts(u8toBCD_s(stats.rx_LQ));
        uartc_puts(" (");
        uartc_puts(u8toBCD_s(stats.LQ_received)); uartc_putc(',');
        uartc_puts(u8toBCD_s(stats.LQ_valid_received));
        uartc_puts("),");
        uartc_puts(u8toBCD_s(stats.received_LQ)); uartc_puts(", ");

        uartc_puts(s8toBCD_s(stats.last_rx_rssi)); uartc_putc(',');
        uartc_puts(s8toBCD_s(stats.received_rssi)); uartc_puts(", ");
        uartc_puts(s8toBCD_s(stats.last_rx_snr)); uartc_puts("; ");

        uartc_puts(u16toBCD_s(stats.bytes_per_sec_transmitted)); uartc_puts(", ");
        uartc_puts(u16toBCD_s(stats.bytes_per_sec_received)); uartc_puts("; ");
        uartc_putc('\n');
      }

      if (!tx_tick) {
        // trigger next cycle
        if (connected() && !connect_tmo_cnt) {
          connect_state = CONNECT_STATE_LISTEN;
        }
        if (connected() && (link_state != LINK_STATE_RECEIVE_DONE)) {
          // frame missed
          connect_sync_cnt = 0;
        }
/*
        static uint16_t tlast_us = 0;
        uint16_t tnow_us = micros();
        uint16_t dt = tnow_us - tlast_us;
        tlast_us = tnow_us;

        uartc_puts(" ");
        uartc_puts(u16toBCD_s(tnow_us)); uartc_puts(", "); uartc_puts(u16toBCD_s(dt)); uartc_puts("; ");
        switch (link_state) {
        case LINK_STATE_IDLE: uartc_puts("i  "); break;
        case LINK_STATE_TRANSMIT: uartc_puts("t  "); break;
        case LINK_STATE_TRANSMIT_WAIT: uartc_puts("tw "); break;
        case LINK_STATE_RECEIVE: uartc_puts("r  "); break;
        case LINK_STATE_RECEIVE_WAIT: uartc_puts("rw "); break;
        case LINK_STATE_RECEIVE_DONE: uartc_puts("rd "); break;
        }
        switch (connect_state) {
        case CONNECT_STATE_LISTEN: uartc_puts("L "); break;
        case CONNECT_STATE_SYNC: uartc_puts("S "); break;
        case CONNECT_STATE_CONNECTED: uartc_puts("C "); break;
        }
        uartc_puts(connected() ? "c " : "d ");
        uartc_puts("\n");
*/
        txstats.Next();
        link_state = LINK_STATE_TRANSMIT;
      }
    }

    //-- SX handling

    switch (link_state) {
    case LINK_STATE_IDLE:
    case LINK_STATE_RECEIVE_DONE:
      break;

    case LINK_STATE_TRANSMIT:
      fhss.HopToNext();
      sx.SetRfFrequency(fhss.GetCurrFreq());
      do_transmit(false);
      link_state = LINK_STATE_TRANSMIT_WAIT;
      DBG_MAIN_SLIM(uartc_puts(">");)
      break;

    case LINK_STATE_RECEIVE:
      // datasheet says "As soon as a packet is detected, the timer is automatically
      // disabled to allow complete reception of the packet." Why does then 5 ms not work??
      sx.SetToRx(10); // we wait 10 ms for the start for the frame, 5 ms does not work ??
      link_state = LINK_STATE_RECEIVE_WAIT;
      break;
    }

    if (irq_status) {
      if (link_state == LINK_STATE_TRANSMIT_WAIT) {
        if (irq_status & SX1280_IRQ_TX_DONE) {
          irq_status = 0;
          link_state = LINK_STATE_RECEIVE;
          DBG_MAIN_SLIM(uartc_puts("!");)
        }
      }
      else
      if (link_state == LINK_STATE_RECEIVE_WAIT) {
        if (irq_status & SX1280_IRQ_RX_DONE) {
          irq_status = 0;
          if (do_receive()) {
            if (connect_state == CONNECT_STATE_LISTEN) {
              connect_state = CONNECT_STATE_SYNC;
              connect_sync_cnt = 0;
            } else
            if (connect_state == CONNECT_STATE_SYNC) {
              connect_sync_cnt++;
              if (connect_sync_cnt >= CONNECT_SYNC_CNT) connect_state = CONNECT_STATE_CONNECTED;
            } else {
              connect_state = CONNECT_STATE_CONNECTED;
            }
            connect_tmo_cnt = CONNECT_TMO_SYSTICKS;
            link_state = LINK_STATE_RECEIVE_DONE; // ready for next frame
          } else {
            link_state = LINK_STATE_IDLE; // ready for next frame
          }
          DBG_MAIN_SLIM(uartc_puts("<\n");)
        }
      }

      if (irq_status & SX1280_IRQ_RX_TX_TIMEOUT) {
        irq_status = 0;
        link_state = LINK_STATE_IDLE;
      }

      if (irq_status & SX1280_IRQ_RX_DONE) {
        LED_GREEN_OFF;
        while (1) { LED_RED_ON; delay_ms(5); LED_RED_OFF; delay_ms(5); }
      }
      if (irq_status & SX1280_IRQ_TX_DONE) {
        LED_RED_OFF;
        while (1) { LED_GREEN_ON; delay_ms(5); LED_GREEN_OFF; delay_ms(5); }
      }
    }

    //-- MBridge handling
#if (SETUP_TX_USE_MBRIDGE == 1)
    if (bridge.channels_updated) {
      bridge.channels_updated = 0;
      // when we receive channels packet from transmitter, we send link stats to transmitter
      tMBridgeLinkStats lstats = {0};
      lstats.rssi = stats.last_rx_rssi;
      lstats.LQ = txstats.GetLQ();
      lstats.snr = stats.last_rx_snr;
      lstats.rssi2 = INT8_MAX;
      lstats.ant_no = 0;
      lstats.receiver_rssi = stats.received_rssi;
      lstats.receiver_LQ = stats.received_LQ;
      lstats.receiver_snr = INT8_MAX;
      lstats.receiver_rssi2 = INT8_MAX;
      lstats.receiver_ant_no = 0;
      lstats.LQ_received_ma = stats.GetTransmitBandwidthUsage();
      lstats.LQ_received = stats.LQ_received;
      lstats.LQ_valid_received = stats.GetReceiveBandwidthUsage();
      bridge.cmd_to_transmitter(MBRIDGE_CMD_TX_LINK_STATS, (uint8_t*)&lstats, sizeof(tMBridgeLinkStats));
#  if (SETUP_TX_CHANNELS_SOURCE == 1)
      // update channels
      fill_rcdata_from_mbridge(&rcData, &(bridge.channels));
#  endif
    }

    if (bridge.cmd_received) {
      uint8_t cmd;
      uint8_t payload[MBRIDGE_COMMANDPACKET_RX_SIZE];
      bridge.cmd_from_transmitter(&cmd, payload);
    }
#endif

  }//end of while(1) loop

}//end of main
