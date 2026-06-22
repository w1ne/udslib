#ifndef SIM_TESTER_H
#define SIM_TESTER_H

#include <stdint.h>

void sim_tester_init(const uint8_t *secret, const uint8_t *seed);
void sim_tester_rx(const uint8_t *data, uint8_t len);
void sim_tester_poll(void);

#endif /* SIM_TESTER_H */
