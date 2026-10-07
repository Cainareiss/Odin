/*
 * Odin, a Sega Hikaru emulator.
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * C entry points for the 93C46 EEPROM of the main board (SH-4 port A).
 */

#ifndef ODIN_EEPROM_BRIDGE_HPP
#define ODIN_EEPROM_BRIDGE_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct odin_eeprom93c46 odin_eeprom93c46_t;

/* `clock_hz` is the clock of the cycles passed to odin_eeprom93c46_tick. */
odin_eeprom93c46_t *odin_eeprom93c46_new(uint32_t clock_hz);
void odin_eeprom93c46_delete(odin_eeprom93c46_t *dev);
void odin_eeprom93c46_load(odin_eeprom93c46_t *dev, const uint8_t *data, size_t size);
void odin_eeprom93c46_reset(odin_eeprom93c46_t *dev);
void odin_eeprom93c46_set_pins(odin_eeprom93c46_t *dev, int cs, int sk, int di, int org);
int odin_eeprom93c46_do(odin_eeprom93c46_t *dev);
void odin_eeprom93c46_tick(odin_eeprom93c46_t *dev, uint32_t cycles);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_EEPROM_BRIDGE_HPP */
