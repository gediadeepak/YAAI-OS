#ifndef NETWORK_H
#define NETWORK_H
#include <stdint.h>
void network_initialize(void);
void network_poll(void);
const char *network_status(void);
const char *network_address(void);
#endif
