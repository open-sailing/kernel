#include "hisi_help.h"
#include "hisi_power.h"
#include "hisi_reg.h"

unsigned int get_power_mode(void)
{
	return FIELD_GET(PEEK32(POWER_MODE_CTRL), POWER_MODE_CTRL, MODE);
}

/*
 * It can operate in one of three modes: 0, 1 or Sleep.
 * On hardware reset, power mode 0 is default.
 */
void set_power_mode(unsigned int power_mode)
{
	unsigned int control_value = 0;

	control_value = PEEK32(POWER_MODE_CTRL);


	switch (power_mode) {
	case POWER_MODE_CTRL_MODE_MODE0:
		control_value = FIELD_SET(control_value, POWER_MODE_CTRL,
			MODE, MODE0);
		break;

	case POWER_MODE_CTRL_MODE_MODE1:
		control_value = FIELD_SET(control_value, POWER_MODE_CTRL,
			MODE, MODE1);
		break;

	case POWER_MODE_CTRL_MODE_SLEEP:
		control_value = FIELD_SET(control_value, POWER_MODE_CTRL,
			MODE, SLEEP);
		break;

	default:
		break;
	}

    /* Set up other fields in Power Control Register */
	if (power_mode == POWER_MODE_CTRL_MODE_SLEEP) {
		control_value = FIELD_SET(control_value, POWER_MODE_CTRL,
			OSC_INPUT,  OFF);
	} else {
		control_value = FIELD_SET(control_value, POWER_MODE_CTRL,
			OSC_INPUT,  ON);
	}
    /* Program new power mode. */
	POKE32(POWER_MODE_CTRL, control_value);
}

void set_current_gate(unsigned int gate)
{
	unsigned int gate_reg;
	unsigned int mode;

	/* Get current power mode. */
	mode = get_power_mode();

	switch (mode) {
	case POWER_MODE_CTRL_MODE_MODE0:
		gate_reg = MODE0_GATE;
		break;

	case POWER_MODE_CTRL_MODE_MODE1:
		gate_reg = MODE1_GATE;
		break;

	default:
		gate_reg = MODE0_GATE;
		break;
	}
	POKE32(gate_reg, gate);
}

/* This function enable/disable the 2D engine. */
void enable_2d_engine(unsigned int enable)
{
	uint32_t gate;

	gate = PEEK32(CURRENT_GATE);
	if (enable) {
		gate = FIELD_SET(gate, CURRENT_GATE, DE, ON);
		gate = FIELD_SET(gate, CURRENT_GATE, CSC, ON);
	} else {
		gate = FIELD_SET(gate, CURRENT_GATE, DE, OFF);
		gate = FIELD_SET(gate, CURRENT_GATE, CSC, OFF);
	}

	set_current_gate(gate);
}

void enable_dma(unsigned int enable)
{
	uint32_t gate;
    /* Enable DMA Gate */
	gate = PEEK32(CURRENT_GATE);
	if (enable)
		gate = FIELD_SET(gate, CURRENT_GATE, DMA, ON);
	else
		gate = FIELD_SET(gate, CURRENT_GATE, DMA, OFF);

	set_current_gate(gate);
}

