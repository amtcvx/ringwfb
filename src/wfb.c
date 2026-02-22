#include "wfb_utils.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/uio.h>

#define PAY_MTU 1400
#define ONLINE_MTU 1400

typedef struct {
  uint8_t droneid;
  uint8_t type;
  uint16_t len;
  uint32_t seq;
} __attribute__((packed)) wfb_utils_heads_pay_t;

/*****************************************************************************/
int main(void) {

  wfb_utils_init_t u;
  wfb_utils_init(&u);

  uint8_t payloadbuf_in[2], payloadbuf_out[WFB_NB][ONLINE_MTU];

  wfb_utils_heads_pay_t headspay_in[2], headspay_out = { .droneid = DRONEID };
  struct iovec iovheadpay_out = { .iov_base = &headspay_out, .iov_len = sizeof(wfb_utils_heads_pay_t) };
  struct iovec iovheadpay_in[2] = { 
    { .iov_base = &headspay_in[0], .iov_len = sizeof(wfb_utils_heads_pay_t) },
    { .iov_base = &headspay_in[1], .iov_len = sizeof(wfb_utils_heads_pay_t) }};

  struct iovec iovpay_in[2] = { 
    { .iov_base = NULL, .iov_len = 0 },
    { .iov_base = NULL, .iov_len = 0 } };

  struct iovec iovpart_in[2][2]= {
    { iovheadpay_in[0], iovpay_in[0] },
    { iovheadpay_in[1], iovpay_in[1] }};

  struct msghdr msg_in[2] = { 
    { .msg_iov = iovpart_in[0], .msg_iovlen = 2 },
    { .msg_iov = iovpart_in[1], .msg_iovlen = 2 }};

  uint32_t sequence = 0;

  uint64_t exptime;
  ssize_t len = 0, lentab[WFB_NB] = { 0 };

  for(;;) {
    if (0 != poll(u.readsets, u.readnb, -1)) {
      for (uint8_t cpt=0; cpt<u.readnb; cpt++) {
        if (u.readsets[cpt].revents == POLLIN) {
	  if ( cpt == 0 ) {
	    len = read(u.devtab[cpt].fd.id, &exptime, sizeof(uint64_t)); 
	    wfb_utils_periodic(&u);
	  } else {

            if (cpt < EXT_NB) {

              memset(&headspay_in[cpt - 1],0,sizeof(wfb_utils_heads_pay_t));
              memset(&payloadbuf_in[cpt - 1],0,sizeof(payloadbuf_in[cpt - 1]));
  
	      if ((len = recvmsg(u.devtab[cpt].fd.id, &msg_in[cpt - 1], MSG_DONTWAIT) - sizeof(wfb_utils_heads_pay_t)) > 0) {

                if (headspay_in[cpt - 1].len > 0) {
#if DRONEID == 0
	          if (headspay_in[cpt - 1].droneid < MAXDRONE) {
	            if (headspay_in[cpt - 1].type == WFB_NB) {
		      len = sendto(u.devdrone[ headspay_in[cpt - 1].droneid ][WFB_VID].fd.id, 
		        iovpay_in[cpt - 1].iov_base, iovpay_in[cpt - 1].iov_len,MSG_DONTWAIT, 
		        (struct sockaddr *)&u.devdrone[ headspay_in[cpt - 1].droneid ][WFB_VID].fd.outaddr, 
			sizeof(struct sockaddr_in));
		      u.log.len += sprintf((char *)u.log.buf + u.log.len, "sendto (%ld)\n",len);
		    }
		  }
#endif
		}
              }
	    } else if (cpt < (EXT_NB + WFB_NB)) {
#if DRONEID > 0
              if (cpt  == (EXT_NB + WFB_VID)) {

                memset(&payloadbuf_out[WFB_VID][0],0,ONLINE_MTU);
                struct iovec iov;
                iov.iov_base = &payloadbuf_out[WFB_VID][0];
                iov.iov_len = PAY_MTU;
                lentab[WFB_VID] = readv( u.devtab[cpt].fd.id, &iov, 1);
//                u.log.len += sprintf((char *)u.log.buf + u.log.len, "readv (%ld)\n",lentab[WFB_VID]);
                printf("len(%ld)\n",lentab[WFB_VID]);

              }
#endif
	    }
	  }
	}
      }

#if DRONEID > 0
      if (lentab[WFB_VID] > 0) {

        headspay_out.type = WFB_VID;
        headspay_out.seq = sequence ++;

        struct iovec iovpay_out = { .iov_base = &payloadbuf_out[WFB_VID][0], .iov_len = lentab[WFB_VID] };
        struct iovec iovpart[2] = { iovheadpay_out, iovpay_out };
	struct msghdr msg = { .msg_iov = iovpart, .msg_iovlen=2, .msg_namelen=sizeof(struct sockaddr_in)}; 

        msg.msg_name = &u.devtab[1].fd.outaddr;
        len = sendmsg(u.devtab[1].fd.id, (const struct msghdr *)&msg, MSG_DONTWAIT);
        printf("sendmsg (%ld)(%d)(%s)\n",len,u.devtab[1].fd.id,u.devtab[1].fd.ipstr);

        msg.msg_name = &u.devtab[2].fd.outaddr;
        len = sendmsg(u.devtab[2].fd.id, (const struct msghdr *)&msg, MSG_DONTWAIT);
        printf("sendmsg (%ld)(%d)(%s)\n",len,u.devtab[2].fd.id,u.devtab[2].fd.ipstr);

        lentab[WFB_VID] = 0;
      }
#endif
    }
  }

  return(0);
}
