#ifndef HISI_POWER_H__
#define HISI_POWER_H__



unsigned int get_power_mode(void);

/* This function sets the current power mode */
void set_power_mode(unsigned int powerMode);

/* This function sets current gate */
void set_current_gate(unsigned int gate);

/* This function enable/disable the 2D engine. */
void enable_2d_engine(unsigned int enable);
/* This function enable/disable the DMA Engine */
void enable_dma(unsigned int enable);
#endif
