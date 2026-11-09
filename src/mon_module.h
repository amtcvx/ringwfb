#ifndef MON_MODULE_H
#define MON_MODULE_H

#include <linux/netdevice.h>

/************************************************************************************************/
#define DRONEID 1

/************************************************************************************************/
typedef struct {
  uint8_t padding;
  uint8_t droneid;
  uint16_t msglen;
  int32_t daddr;
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
  uint32_t txbytes;
  uint32_t txpktko;
  uint32_t rxbytes;
  uint32_t rxpktko;
} log_t;

typedef struct {
  uint8_t channb;
  chan_t chan[40];
  log_t log;
  struct net_device *ndev;
} wifidev_t;

typedef struct {
  uint8_t wifinb;
  uint32_t ipint;
  struct net_device *localdev;
  struct net *pnet;
  wifidev_t  wifidev[4];
} priv_t;

/************************************************************************************************/
extern priv_t *ppriv;
extern u8 *pipstr;


/************************************************************************************************/
bool utils_init(void);
void utils_start(void);
void utils_exit(void);
bool input_init(void);
void input_exit(void);
bool output_init(void); 
void output_exit(void); 

#endif // MON_MODULE_H
