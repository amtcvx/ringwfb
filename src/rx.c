#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/unaligned.h>

#include "mon_module.h"

/******************************************************************************/
// sudo insmod mon_module.ko target_mac=0x00,0x11,0x22,0x33,0x44,0x55
//
static unsigned char target_mac[6] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
/*
module_param_array(target_mac, byte, NULL, 0644);
MODULE_PARM_DESC(target_mac, "Adresse MAC de la cible à intercepter (6 octets)");
*/
/******************************************************************************/
rx_handler_result_t input_proc(struct sk_buff **pskb) {

  struct sk_buff *skb = *pskb;

  if (unlikely(!skb)) return RX_HANDLER_CONSUMED;

  if (unlikely(skb->len < 36)) return RX_HANDLER_PASS;

  u16 rt_len = get_unaligned_le16(skb->data + 2);
  if (unlikely(rt_len + 24 > skb->len)) return RX_HANDLER_PASS;

  u8 *ieee_hdr = skb->data + rt_len;
  if (unlikely(ieee_hdr[10] == 0x00 && ieee_hdr[11] == 0x11 && ieee_hdr[12] == 0x22))
    return RX_HANDLER_PASS;

  u8 *dst_mac = ieee_hdr + 4;
  if (likely(memcmp(dst_mac, target_mac, 6) != 0)) return RX_HANDLER_PASS; 

  if ((*ieee_hdr & 0x0C) == 0x08) {
    if (skb_cloned(skb) || skb_shared(skb)) {
       struct sk_buff *nst = skb_unshare(skb, GFP_ATOMIC);
       if (unlikely(!nst)) return RX_HANDLER_CONSUMED;
       *pskb = nst;
        skb = nst;
        ieee_hdr = skb->data + rt_len;
    }

    ieee_hdr[10] = 0x00;
    ieee_hdr[11] = 0x11;
    ieee_hdr[12] = 0x22;
/*
    skb->ip_summed = CHECKSUM_NONE;
    skb->dev = monitor_dev;
    dev_direct_xmit(skb, 0); 
*/
    return RX_HANDLER_CONSUMED;
  }

 return RX_HANDLER_PASS;
}

