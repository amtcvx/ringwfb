#include <linux/inet.h>
#include <net/cfg80211.h>
#include <linux/rtnetlink.h>

			     
#include "mon_module.h"

static priv_t mypriv;
priv_t *ppriv = &mypriv;

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  memset(ppriv, 0, sizeof(priv_t));

  utils_init(ppriv);

  if (ppriv->wifinb) return -1;

  input_init(ppriv);

  if (!(output_init(ppriv))) return -1;

  utils_start();

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  printk(KERN_EMERG "WFB ENDING\n");

  utils_exit();

  output_exit(ppriv);

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Router");
