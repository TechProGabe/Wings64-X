/* nv2a_fault.h - GPU fault records from the patched pbkit (nv2a_fault.c). */
#ifndef NV2A_FAULT_H
#define NV2A_FAULT_H
#include <stdint.h>

extern volatile uint32_t nv2a_fault_frame, nv2a_fault_draw;
uint32_t nv2a_fault_count(void);
void nv2a_fault_log_first(uint32_t pushbuffer_phys);
void nv2a_fault_log_ctx(void);
void nv2a_irq_recover(void);

#endif
