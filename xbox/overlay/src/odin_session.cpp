/*
 * The panel of the loaded game (odin_session.hpp). GPL-3.0-or-later.
 *
 * JVS layout of CabinetInputs (hikaru_host.hpp): the player word's high byte
 * holds START (bit 7), SERVICE (6), UP (5), DOWN (4), LEFT (3), RIGHT (2),
 * PUSH1 (1), PUSH2 (0). A pressed button is 1 here; the mapping is checked
 * against a real game in M6 (no game exists on the development PC).
 */

#include "odin_session.hpp"

namespace odinc {

namespace {

constexpr uint16_t JVS_START = 0x8000, JVS_SERVICE = 0x4000, JVS_UP = 0x2000, JVS_DOWN = 0x1000, JVS_LEFT = 0x0800,
                   JVS_RIGHT = 0x0400, JVS_PUSH1 = 0x0200, JVS_PUSH2 = 0x0100;

/* A pad's value when the pad is connected, else the rest value 0 */
int
value_of(const OdinPad &pad, int value)
{
	return pad.connected ? value : 0;
}

/* A stick value (-32768..32767) on the 0x8000-centred analog channels */
uint16_t
analog_of(int value)
{
	const int v = 0x8000 + value / 2;

	return static_cast<uint16_t>(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v);
}

} /* namespace */

void
PadPanel::set(const OdinPad *pads, uint32_t count)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	for (uint32_t i = 0; i < count && i < 2; i++)
		m_pads[i] = pads[i];
}

void
PadPanel::read_inputs(odin::CabinetInputs &inputs)
{
	OdinPad pads[2];

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pads[0] = m_pads[0];
		pads[1] = m_pads[1];
	}
	inputs.mainboard_test = 0;
	inputs.mainboard_service = 0;
	inputs.jvs_system = 0;
	for (int p = 0; p < 2; p++) {
		const OdinPad &pad = pads[p];
		const bool on = pad.connected != 0;
		const uint32_t b = on ? pad.buttons : 0;
		uint16_t word = 0;

		if (b & ODIN_PAD_START)
			word |= JVS_START;
		if (b & ODIN_PAD_UP)
			word |= JVS_UP;
		if (b & ODIN_PAD_DOWN)
			word |= JVS_DOWN;
		if (b & ODIN_PAD_LEFT)
			word |= JVS_LEFT;
		if (b & ODIN_PAD_RIGHT)
			word |= JVS_RIGHT;
		if (b & ODIN_PAD_A)
			word |= JVS_PUSH1;
		if (b & ODIN_PAD_B)
			word |= JVS_PUSH2;
		inputs.jvs_player[p] = word;

		/* the coin: one per press of View, not one per frame it is held */
		const bool down = (b & ODIN_PAD_BACK) != 0;

		inputs.jvs_coin[p] = (down && !m_coin_down[p]) ? 1 : 0;
		m_coin_down[p] = down;
	}
	/* channel 0: left stick X, channel 1: left stick Y (up is positive on
	 * the cabinet: the pad's +Y is down), 2 and 3: the triggers */
	inputs.jvs_analog[0] = analog_of(value_of(pads[0], pads[0].lx));
	inputs.jvs_analog[1] = analog_of(-value_of(pads[0], pads[0].ly));
	inputs.jvs_analog[2] = static_cast<uint16_t>(value_of(pads[0], pads[0].lt) * 257);
	inputs.jvs_analog[3] = static_cast<uint16_t>(value_of(pads[0], pads[0].rt) * 257);
	for (int i = 4; i < 8; i++)
		inputs.jvs_analog[i] = 0x8000;
}

} /* namespace odinc */
