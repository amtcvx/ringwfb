#include <linux/inet.h>
#include <net/cfg80211.h>
#include <linux/rtnetlink.h>

			     
#include "mon_module.h"

#define TIMEOUT 1000

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  utils_init(&mypriv);

  input_init(mypriv);

  if (!(output_init(&mypriv))) return -1;

  utils_init();

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  printk(KERN_EMERG "WFB ENDING\n");

  utils_exit();

  output_exit(&mypriv);

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Router");
