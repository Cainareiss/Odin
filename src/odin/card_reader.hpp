/*
 * Odin
 * Copyright (C) 2026 Réda Chérif-Touil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef ODIN_CARD_READER_HPP
#define ODIN_CARD_READER_HPP

#include "odin/mie.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace odin {

/* Sanwa CRP-1231 family magnetic card reader/writer with a card
 * dispenser, on the RS-232 line of the MIE serial adapter block.
 *
 * HLE stand-in: the reader's firmware (an H8/3003; the LR-10NAB build
 * is dumped as crp1231lr10_ver0110.ic2 in MAME's wangmid set) is not
 * run. The protocol is answered as that firmware and Flycast's
 * card_reader.cpp describe it. The LLE path that replaces this file is
 * an H8/300H core running that firmware behind the same MieSerialDevice
 * interface.
 *
 * Master frame: 02 LEN CMD 00 00 00 [params] 03 XOR, LEN counting the
 * bytes after it, XOR over LEN..ETX. The reader answers ACK (06) or
 * NAK (15); on ENQ (05) it sends 02 LEN CMD st1 st2 st3 [data] 03 XOR.
 * Status bytes:
 *   st1  sensors, binary (Flycast SanwaCRP1231BR::getStatus1): bits
 *        7-6 shutter (1 closed, 2 open), bit 5 stocker full, bits 4-0
 *        card sensors (0x18 card loaded inside, 0 none). von4 shows
 *        "card left at the rear" for bit 4 alone, so 0x60 = no card.
 *   st2  result (Flycast RESULT2, firmware FUN_00009072): '0' OK.
 *   st3  command state (Flycast RESULT3): '0' OK, '4' waiting for a
 *        card (read or write without one), '5' stocker empty. */
struct CardReaderState;

class CardReader : public MieSerialDevice {
public:
	/* Flycast TRACK_SIZE: three 69-byte tracks. */
	static constexpr uint32_t TRACK_SIZE = 0x45;
	static constexpr uint32_t TRACKS = 3;
	static constexpr uint32_t CARD_SIZE = TRACK_SIZE * TRACKS;
	static constexpr uint32_t FRAME_MAX = 256;

	enum : uint8_t {
		STX = 0x02,
		ETX = 0x03,
		ENQ = 0x05,
		ACK = 0x06,
		NAK = 0x15
	};

	/* Commands (Flycast card_reader.cpp, firmware dispatch
	 * FUN_000023e2). */
	enum : uint8_t {
		CMD_INIT = 0x10,
		CMD_GET_STATE = 0x20,
		CMD_READ = 0x33,
		CMD_CANCEL = 0x40,
		CMD_WRITE = 0x53,
		CMD_PRINT_SETTINGS = 0x78,
		CMD_REGISTER_FONT = 0x7A,
		CMD_PRINT = 0x7C,
		CMD_ERASE_PRINT = 0x7D,
		CMD_EJECT = 0x80,
		CMD_CLEAN = 0xA0,
		CMD_DISPENSE = 0xB0,
		CMD_DOOR = 0xD0
	};

	/* `card_file` holds the three tracks; empty = no file, the card
	 * is never saved. */
	explicit CardReader(std::string card_file = std::string());

	/* Power-up: no card in the reader, stocker full, door closed. The
	 * card data survives. */
	void reset();

	/* MieSerialDevice: the line. */
	void receive(uint8_t byte) override;
	bool has_byte() override;
	uint8_t take_byte() override;

	void save(CardReaderState &out) const;
	void load(const CardReaderState &in);

	/* Puts the card of the file at the magnetic head; false when the
	 * file cannot be read. */
	bool insert_card();
	void eject_card();
	bool card_present() const { return m_card_present; }
	std::span<uint8_t> card() { return m_card; }

	/* XOR of a frame slice, as the firmware checks it. */
	static uint8_t frame_xor(std::span<const uint8_t> bytes);

	/* Unknown commands and bad frames are logged once each to `out`. */
	void set_log(FILE *out) { m_log = out; }
	/* One line per frame on the line, both ways. */
	void set_trace(FILE *out) { m_trace = out; }

private:
	void frame_complete();
	void execute(std::span<const uint8_t> frame);
	void respond(uint8_t cmd, uint8_t st3, std::span<const uint8_t> data);
	uint8_t status1() const;
	void send(uint8_t byte);
	void send_response();
	uint8_t track_mask(uint8_t selector) const;
	void trace_frame(const char *dir, std::span<const uint8_t> bytes) const;
	bool save_card();

	std::string m_card_file;
	std::array<uint8_t, CARD_SIZE> m_card;
	bool m_card_present;
	bool m_door_open;
	bool m_stocker_full;

	std::array<uint8_t, FRAME_MAX> m_rx;
	uint32_t m_rx_count;
	std::array<uint8_t, FRAME_MAX> m_response;
	uint32_t m_response_size;
	std::array<uint8_t, FRAME_MAX + 2> m_tx;
	uint32_t m_tx_head;
	uint32_t m_tx_count;

	FILE *m_log;
	FILE *m_trace;
	std::array<bool, 256> m_cmd_logged;
	bool m_frame_logged;
	bool m_idle_logged;
};

/* The card in the unit and the frames in flight. Plain data. */
struct CardReaderState {
	std::array<uint8_t, CardReader::CARD_SIZE> card;
	std::array<uint8_t, CardReader::FRAME_MAX> rx;
	std::array<uint8_t, CardReader::FRAME_MAX> response;
	std::array<uint8_t, CardReader::FRAME_MAX + 2> tx;
	uint32_t rx_count;
	uint32_t response_size;
	uint32_t tx_head;
	uint32_t tx_count;
	uint8_t card_present;
	uint8_t door_open;
	uint8_t stocker_full;
};

} /* namespace odin */

#endif /* ODIN_CARD_READER_HPP */
