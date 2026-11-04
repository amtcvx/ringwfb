#include "mon_module.h"

uint16_t tunport = 9999;

static priv_t mypriv;
priv_t *ppriv = &mypriv;

/******************************************************************************/
static int __init wfb_nfkernel_init(void) {

  memset(ppriv, 0, sizeof(priv_t));
  if (!(utils_init(&init_net))) return -1;
  if (!(input_init())) return -1;
  if (!(output_init())) return -1;
  utils_start();

  return 0;
}

/******************************************************************************/
static void __exit wfb_nfkernel_exit(void) {

  printk(KERN_EMERG "WFB ENDING\n");

  utils_exit();
  output_exit();
  input_exit();

}

/******************************************************************************/
module_init(wfb_nfkernel_init);
module_exit(wfb_nfkernel_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Router");
