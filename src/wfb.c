#include "wfb_utils.h"
#include <stdio.h>

int main(void) {

  wfb_utils_init_t u;
  wfb_utils_init(&u);
  wfb_utils_loop(&u);

  return(0);
}
