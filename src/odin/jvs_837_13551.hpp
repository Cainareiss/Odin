// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * Sega 837-13551 JVS I/O board, from MAME src/mame/sega/jvs13551,
 * rewritten for Odin. Odin is GPL-3.0; this file keeps its BSD-3-Clause
 * licence and copyright holder.
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#ifndef ODIN_JVS_837_13551_HPP
#define ODIN_JVS_837_13551_HPP

#include "odin/jvs.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* The board's TMP90PH44 firmware (315-6215) is not run; the node answers
 * the JVS protocol as the firmware does: 2 players x 13 switch bits, 2
 * coin slots, 8 analog channels, 6 driver outputs. */
/* The node, its coin counters and its output latch. Plain data. */
struct Jvs837_13551State {
	JvsDeviceState node;
	std::array<uint16_t, 2> coin_counter;
	uint8_t outputs;
};

class Jvs837_13551 : public JvsDevice {
public:
	static constexpr uint8_t PLAYERS = 2;
	static constexpr uint8_t COIN_SLOTS = 2;
	static constexpr uint8_t ANALOG_CHANNELS = 8;

	explicit Jvs837_13551(JvsInputs &inputs);

	/* One coin through slot 0 or 1. */
	void inc_coin(uint8_t slot);
	uint8_t outputs() const { return m_outputs; }

	void save(Jvs837_13551State &out) const;
	void load(const Jvs837_13551State &in);

protected:
	const char *device_id() const override;
	/* What the 837-13551 answers (MAME jvs13551.cpp:68-78); the MIE
	 * program copies them into the node record (C101-C103). */
	uint8_t command_format_version() const override { return 0x11; }
	uint8_t jvs_standard_version() const override { return 0x20; }
	uint8_t comm_method_version() const override { return 0x10; }
	int32_t function_list(std::span<uint8_t> out) override;
	int32_t switches(std::span<uint8_t> out, uint8_t players, uint8_t bytes_per_player) override;
	int32_t coin_counters(std::span<uint8_t> out, uint8_t slots) override;
	bool coin_add(uint8_t slot, int32_t count) override;
	int32_t analogs(std::span<uint8_t> out, uint8_t channels) override;
	bool swoutputs(std::span<const uint8_t> values) override;
	bool swoutput(uint8_t id, uint8_t value) override;
	void device_reset() override;

private:
	JvsInputs &m_inputs;
	std::array<uint16_t, COIN_SLOTS> m_coin_counter;
	uint8_t m_outputs;
};

} /* namespace odin */

#endif /* ODIN_JVS_837_13551_HPP */
