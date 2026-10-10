/*
 * Odin, a Sega Hikaru emulator.
 * Copyright (c) 2026 Réda Chérif-Touil
 */

#include "odin/eeprom_bridge.hpp"

#include "odin/eeprom_93c46.hpp"

#include <cstring>
#include <span>

struct odin_eeprom93c46 {
	odin::Eeprom93c46 chip;

	explicit odin_eeprom93c46(uint32_t clock_hz) : chip(clock_hz) {}
};

extern "C" {

odin_eeprom93c46_t *
odin_eeprom93c46_new(uint32_t clock_hz)
{
	return new odin_eeprom93c46(clock_hz);
}

void
odin_eeprom93c46_delete(odin_eeprom93c46_t *dev)
{
	delete dev;
}

void
odin_eeprom93c46_load(odin_eeprom93c46_t *dev, const uint8_t *data, size_t size)
{
	dev->chip.load(std::span<const uint8_t>(data, size));
}

void
odin_eeprom93c46_reset(odin_eeprom93c46_t *dev)
{
	dev->chip.reset();
}

void
odin_eeprom93c46_set_pins(odin_eeprom93c46_t *dev, int cs, int sk, int di, int org)
{
	dev->chip.set_pins(cs != 0, sk != 0, di != 0, org != 0);
}

int
odin_eeprom93c46_do(odin_eeprom93c46_t *dev)
{
	return dev->chip.do_read() ? 1 : 0;
}

void
odin_eeprom93c46_tick(odin_eeprom93c46_t *dev, uint32_t cycles)
{
	dev->chip.tick(cycles);
}

size_t
odin_eeprom93c46_state_size(void)
{
	return sizeof(odin::Eeprom93c46State);
}

void
odin_eeprom93c46_save_state(const odin_eeprom93c46_t *dev, void *out)
{
	odin::Eeprom93c46State state;
	std::memset(&state, 0, sizeof(state));
	dev->chip.save(state);
	std::memcpy(out, &state, sizeof(state));
}

void
odin_eeprom93c46_load_state(odin_eeprom93c46_t *dev, const void *in)
{
	odin::Eeprom93c46State state;
	std::memcpy(&state, in, sizeof(state));
	dev->chip.load(state);
}

} /* extern "C" */
