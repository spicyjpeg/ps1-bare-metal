

#include <stdint.h>
#include <stdbool.h>

void cdrom_init();
bool cdrom_readTest(uint8_t** outBuf, uint32_t* outLength);
