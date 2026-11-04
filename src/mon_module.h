#ifndef MON_MODULE_H
#define MON_MODULE_H

#include <linux/netdevice.h>

/************************************************************************************************/
typedef struct {
  uint8_t padding;
  uint8_t droneid;
  uint16_t msglen;
  int32_t backfreq;
  uint64_t seq;
  uint16_t dummy;
} __attribute__((packed)) pph_t;

/************************************************************************************************/
typedef struct {
  uint8_t value;
  uint16_t freq;
} chan_t;

typedef struct {
  uint8_t curchan;
  uint16_t pkrcv;
} log_t;

typedef struct {
  uint8_t channb;
  chan_t chan[40];
  log_t log;
  struct net_device *ndev;
} wifidev_t;

typedef struct {
  uint8_t wifinb;
  uint32_t localipint;
  struct net_device *localdev;
  wifidev_t  wifidev[4];
} priv_t;

/************************************************************************************************/
extern priv_t *ppriv;
extern uint16_t tunport; 


/************************************************************************************************/
bool utils_init(struct net *);
void utils_start(void);
void utils_exit(void);
bool input_init(void);
void input_exit(void);
bool output_init(void); 
void output_exit(void); 

#endif // MON_MODULE_H
