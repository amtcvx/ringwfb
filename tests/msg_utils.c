/*

gcc -g -O2 -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -c msg_utils.c -o msg_utils.o

*/

#include "msg_utils.h"

#include <stdint.h>

/*****************************************************************************/
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04

#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_SGI      0x04

#define IEEE80211_RADIOTAP_MCS_STBC_1  1
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

#define MCS_INDEX  2


uint8_t radiotaphd_tx[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
uint8_t ieeehd_tx[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};
uint8_t llchd_tx[4];

const struct iovec iov_radiotaphd_tx = { .iov_base = radiotaphd_tx, .iov_len = sizeof(radiotaphd_tx)};
const struct iovec iov_ieeehd_tx =     { .iov_base = ieeehd_tx,     .iov_len = sizeof(ieeehd_tx)};
const struct iovec iov_llchd_tx =      { .iov_base = llchd_tx,      .iov_len = sizeof(llchd_tx)};

uint8_t payloadbuf_tx[1400] = {-1};
const struct iovec iovpay_tx = { .iov_base = payloadbuf_tx, .iov_len = sizeof(payloadbuf_tx) };
struct iovec iovtab_tx[4] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovpay_tx };

/*****************************************************************************/
#define RADIOTAPSIZE 35

uint8_t radiotaphd_rx[RADIOTAPSIZE];
uint8_t ieeehd_rx[24];
uint8_t llchd_rx[4];

const struct iovec iov_radiotaphd_rx = { .iov_base = radiotaphd_rx, .iov_len = sizeof(radiotaphd_rx)};
const struct iovec iov_ieeehd_rx =     { .iov_base = ieeehd_rx,     .iov_len = sizeof(ieeehd_rx)};
const struct iovec iov_llchd_rx =      { .iov_base = llchd_rx,      .iov_len = sizeof(llchd_rx)};

uint8_t payloadbuf_rx[1400] = {-1};
const struct iovec iovpay_rx = { .iov_base = payloadbuf_rx, .iov_len = sizeof(payloadbuf_rx) };
struct iovec iovtab_rx[4] = { iov_radiotaphd_rx, iov_ieeehd_rx, iov_llchd_tx, iovpay_rx };

/*****************************************************************************/
void msg_utils_init(msg_utils_init_t *u) {

  u->msg_out.msg_iov = iovtab_tx; u->msg_out.msg_iovlen = 4;
  u->msg_in.msg_iov = iovtab_rx; u->msg_in.msg_iovlen = 4;
}
