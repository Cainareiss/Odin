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

#include "odin/card_reader.hpp"

#include <format>
#include <utility>

namespace odin {

namespace {

/* Layout of a master frame: STX LEN CMD 00 00 00 params ETX XOR. */
constexpr uint32_t FRAME_PARAMS = 6;
constexpr uint32_t FRAME_OVERHEAD = 8;
/* Response: STX LEN CMD st1 st2 st3 data ETX XOR. */
constexpr uint32_t RESPONSE_OVERHEAD = 8;

/* Read and write parameters (Flycast: mode, parity, track). */
constexpr uint32_t PARAM_MODE = 0;
constexpr uint32_t PARAM_TRACK = 2;
constexpr uint32_t PARAM_DATA = 3;
constexpr uint8_t MODE_CAPTURE = '2';

constexpr uint8_t ST2_OK = '0';
constexpr uint8_t ST3_OK = '0';
constexpr uint8_t ST3_CARD_WAIT = '4';
constexpr uint8_t ST3_NO_CARD_IN_BOX = '5';

constexpr uint32_t TRACE_BYTES = 32;

} /* namespace */

CardReader::CardReader(std::string card_file)
	: m_card_file(std::move(card_file)), m_card_present(false), m_door_open(false),
	  m_stocker_full(true), m_rx_count(0), m_response_size(0), m_tx_head(0), m_tx_count(0),
	  m_log(nullptr), m_trace(nullptr), m_frame_logged(false), m_idle_logged(false)
{
	m_card.fill(0);
	m_cmd_logged.fill(false);
	reset();
}

/* A card inside the mechanism stays there through a reset. */
void CardReader::reset()
{
	m_door_open = false;
	m_rx_count = 0;
	m_response_size = 0;
	m_tx_head = 0;
	m_tx_count = 0;
}

uint8_t CardReader::frame_xor(std::span<const uint8_t> bytes)
{
	uint8_t x = 0;
	for (uint8_t b : bytes) {
		x = static_cast<uint8_t>(x ^ b);
	}
	return x;
}

/* The line, byte by byte. Outside a frame the master sends ENQ for the
 * response, NAK for it again, ACK to close the exchange (firmware
 * FUN_000074dc state 2, FUN_00008418). */
void CardReader::receive(uint8_t byte)
{
	if (m_rx_count == 0) {
		if (byte == ENQ || byte == NAK) {
			if (m_trace) {
				std::fputs(byte == ENQ ? "card: rx ENQ\n" : "card: rx NAK\n", m_trace);
			}
			send_response();
			return;
		}
		if (byte == ACK) {
			return;
		}
		if (byte != STX) {
			if (m_log && !m_idle_logged) {
				m_idle_logged = true;
				std::fputs(std::format("card: unexpected byte {:02x} outside a frame\n", byte).c_str(), m_log);
			}
			return;
		}
	}
	m_rx[m_rx_count++] = byte;
	if (m_rx_count >= 2 && m_rx_count == static_cast<uint32_t>(m_rx[1]) + 2) {
		frame_complete();
		m_rx_count = 0;
	} else if (m_rx_count == FRAME_MAX) {
		if (m_log && !m_frame_logged) {
			m_frame_logged = true;
			std::fputs("card: frame longer than 256 bytes dropped\n", m_log);
		}
		m_rx_count = 0;
	}
}

bool CardReader::has_byte()
{
	return m_tx_count > m_tx_head;
}

uint8_t CardReader::take_byte()
{
	if (m_tx_count <= m_tx_head) {
		return 0;
	}
	const uint8_t v = m_tx[m_tx_head++];
	if (m_tx_head == m_tx_count) {
		m_tx_head = 0;
		m_tx_count = 0;
	}
	return v;
}

void CardReader::send(uint8_t byte)
{
	if (m_tx_count < m_tx.size()) {
		m_tx[m_tx_count++] = byte;
	}
}

/* ETX in place and XOR right: the command runs and ACK goes back
 * (firmware FUN_0000830c, FUN_000083f4). */
void CardReader::frame_complete()
{
	const std::span<const uint8_t> frame(m_rx.data(), m_rx_count);
	trace_frame("rx", frame);
	const uint8_t x = frame_xor(frame.subspan(1, m_rx_count - 2));
	if (frame[m_rx_count - 2] != ETX || x != frame[m_rx_count - 1]) {
		if (m_log && !m_frame_logged) {
			m_frame_logged = true;
			std::fputs(std::format("card: bad frame, xor {:02x} expected {:02x}\n",
			                       frame[m_rx_count - 1], x).c_str(), m_log);
		}
		send(NAK);
		if (m_trace) {
			std::fputs("card: tx NAK\n", m_trace);
		}
		return;
	}
	execute(frame);
	send(ACK);
	if (m_trace) {
		std::fputs("card: tx ACK\n", m_trace);
	}
}

/* Flycast track selector for read and write: '0'-'2' one track,
 * '3' 1+2, '4' 1+3, '5' 2+3, '6' all three. Bit n = track n+1. */
uint8_t CardReader::track_mask(uint8_t selector) const
{
	switch (selector) {
	case '0': return 0x1;
	case '1': return 0x2;
	case '2': return 0x4;
	case '3': return 0x3;
	case '4': return 0x5;
	case '5': return 0x6;
	case '6': return 0x7;
	default: return 0;
	}
}

void CardReader::execute(std::span<const uint8_t> frame)
{
	const uint8_t cmd = frame[2];
	const uint32_t nparams = frame.size() > FRAME_OVERHEAD ? static_cast<uint32_t>(frame.size()) - FRAME_OVERHEAD : 0;
	const std::span<const uint8_t> params = frame.subspan(FRAME_PARAMS, nparams);
	const std::span<const uint8_t> none;

	switch (cmd) {
	case CMD_INIT:
	case CMD_GET_STATE:
	case CMD_CANCEL:
	case CMD_PRINT_SETTINGS:
	case CMD_REGISTER_FONT:
	case CMD_PRINT:
	case CMD_ERASE_PRINT:
	case CMD_CLEAN:
		respond(cmd, ST3_OK, none);
		return;
	case CMD_DOOR:
		m_door_open = nparams >= 1 && params[0] == '1';
		respond(cmd, ST3_OK, none);
		return;
	case CMD_DISPENSE:
		/* A blank card from the stocker lands at the head. */
		if (!m_stocker_full) {
			respond(cmd, ST3_NO_CARD_IN_BOX, none);
			return;
		}
		m_card.fill(0);
		m_card_present = true;
		m_door_open = false;
		respond(cmd, ST3_OK, none);
		return;
	case CMD_EJECT:
		m_card_present = false;
		respond(cmd, ST3_OK, none);
		return;
	case CMD_READ: {
		if (!m_card_present || m_door_open) {
			respond(cmd, m_card_present ? ST3_OK : ST3_CARD_WAIT, none);
			return;
		}
		std::array<uint8_t, CARD_SIZE> out;
		uint32_t size = 0;
		const uint8_t mask = nparams > PARAM_TRACK ? track_mask(params[PARAM_TRACK]) : 0;
		if (nparams > PARAM_MODE && params[PARAM_MODE] != MODE_CAPTURE) {
			for (uint32_t t = 0; t < TRACKS; t++) {
				if (mask & (1u << t)) {
					for (uint32_t i = 0; i < TRACK_SIZE; i++) {
						out[size++] = m_card[t * TRACK_SIZE + i];
					}
				}
			}
		}
		respond(cmd, ST3_OK, std::span<const uint8_t>(out.data(), size));
		return;
	}
	case CMD_WRITE: {
		if (!m_card_present) {
			respond(cmd, ST3_CARD_WAIT, none);
			return;
		}
		const uint8_t mask = nparams > PARAM_TRACK ? track_mask(params[PARAM_TRACK]) : 0;
		uint32_t pos = PARAM_DATA;
		for (uint32_t t = 0; t < TRACKS; t++) {
			if ((mask & (1u << t)) && pos + TRACK_SIZE <= nparams) {
				for (uint32_t i = 0; i < TRACK_SIZE; i++) {
					m_card[t * TRACK_SIZE + i] = params[pos + i];
				}
				pos += TRACK_SIZE;
			}
		}
		save_card();
		respond(cmd, ST3_OK, none);
		return;
	}
	default:
		break;
	}
	if (m_log && !m_cmd_logged[cmd]) {
		m_cmd_logged[cmd] = true;
		std::fputs(std::format("card: unknown command {:02x}, {} parameter bytes, answered OK\n",
		                       cmd, nparams).c_str(), m_log);
	}
	respond(cmd, ST3_OK, none);
}

uint8_t CardReader::status1() const
{
	return static_cast<uint8_t>(((m_door_open ? 2u : 1u) << 6) | (m_stocker_full ? 0x20u : 0u) |
	                            (m_card_present ? 0x18u : 0u));
}

/* Builds the response the next ENQ fetches. */
void CardReader::respond(uint8_t cmd, uint8_t st3, std::span<const uint8_t> data)
{
	if (data.size() > FRAME_MAX - RESPONSE_OVERHEAD) {
		data = data.first(FRAME_MAX - RESPONSE_OVERHEAD);
	}
	uint32_t n = 0;
	m_response[n++] = STX;
	m_response[n++] = static_cast<uint8_t>(RESPONSE_OVERHEAD - 2 + data.size());
	m_response[n++] = cmd;
	m_response[n++] = status1();
	m_response[n++] = ST2_OK;
	m_response[n++] = st3;
	for (uint8_t b : data) {
		m_response[n++] = b;
	}
	m_response[n++] = ETX;
	m_response[n] = frame_xor(std::span<const uint8_t>(m_response.data() + 1, n - 1));
	m_response_size = n + 1;
}

void CardReader::send_response()
{
	if (m_response_size == 0) {
		if (m_log && !m_idle_logged) {
			m_idle_logged = true;
			std::fputs("card: ENQ before any command\n", m_log);
		}
		return;
	}
	for (uint32_t i = 0; i < m_response_size; i++) {
		send(m_response[i]);
	}
	trace_frame("tx", std::span<const uint8_t>(m_response.data(), m_response_size));
}

void CardReader::trace_frame(const char *dir, std::span<const uint8_t> bytes) const
{
	if (!m_trace) {
		return;
	}
	std::string line = std::format("card: {} ({} bytes)", dir, bytes.size());
	const size_t shown = bytes.size() < TRACE_BYTES ? bytes.size() : TRACE_BYTES;
	for (size_t i = 0; i < shown; i++) {
		line += std::format(" {:02x}", bytes[i]);
	}
	if (shown < bytes.size()) {
		line += " ...";
	}
	line += '\n';
	std::fputs(line.c_str(), m_trace);
}

bool CardReader::insert_card()
{
	if (m_card_file.empty()) {
		return false;
	}
	FILE *f = std::fopen(m_card_file.c_str(), "rb");
	if (!f) {
		return false;
	}
	const size_t n = std::fread(m_card.data(), 1, m_card.size(), f);
	std::fclose(f);
	if (n != m_card.size() && m_log) {
		std::fputs(std::format("card: file '{}' holds {} bytes, {} expected\n",
		                       m_card_file, n, m_card.size()).c_str(), m_log);
	}
	m_card_present = true;
	m_door_open = false;
	return true;
}

void CardReader::eject_card()
{
	m_card_present = false;
}

bool CardReader::save_card()
{
	if (m_card_file.empty()) {
		return false;
	}
	FILE *f = std::fopen(m_card_file.c_str(), "wb");
	if (!f) {
		if (m_log) {
			std::fputs(std::format("card: can't write '{}'\n", m_card_file).c_str(), m_log);
		}
		return false;
	}
	const size_t n = std::fwrite(m_card.data(), 1, m_card.size(), f);
	std::fclose(f);
	return n == m_card.size();
}

void CardReader::save(CardReaderState &out) const
{
	out.card = m_card;
	out.rx = m_rx;
	out.response = m_response;
	out.tx = m_tx;
	out.rx_count = m_rx_count;
	out.response_size = m_response_size;
	out.tx_head = m_tx_head;
	out.tx_count = m_tx_count;
	out.card_present = m_card_present;
	out.door_open = m_door_open;
	out.stocker_full = m_stocker_full;
}

void CardReader::load(const CardReaderState &in)
{
	m_card = in.card;
	m_rx = in.rx;
	m_response = in.response;
	m_tx = in.tx;
	m_rx_count = in.rx_count;
	m_response_size = in.response_size;
	m_tx_head = in.tx_head;
	m_tx_count = in.tx_count;
	m_card_present = in.card_present != 0;
	m_door_open = in.door_open != 0;
	m_stocker_full = in.stocker_full != 0;
}

} /* namespace odin */
