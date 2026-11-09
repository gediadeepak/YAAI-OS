#ifndef DEVICES_H
#define DEVICES_H
#include <stdint.h>
typedef struct { int supported, present, charging, online, percent; } PowerStatus;
PowerStatus devices_power(void);
uint32_t devices_pci_read(uint32_t address, unsigned offset);
void devices_pci_enable(uint32_t address);
#endif
