// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * JVS host side and device base, from MAME src/devices/machine/jvshost
 * and jvsdev, rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holder.
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#include "odin/jvs.hpp"

#include <cstring>

namespace odin {

using namespace jvs;

/* Host */

JvsHost::JvsHost()
	: m_first(nullptr)
{
	reset();
}

void JvsHost::reset()
{
	m_send_size = 0;
	m_recv_size = 0;
	m_send.fill(0);
	m_recv.fill(0);
}

void JvsHost::add_device(JvsDevice *dev)
{
	if (m_first) {
		m_first->chain(dev);
	} else {
		m_first = dev;
	}
}

bool JvsHost::address_set_line() const
{
	return m_first && m_first->address_set_line();
}

void JvsHost::push(uint8_t value)
{
	if (m_send_size < BUFFER_SIZE) {
		m_send[m_send_size++] = value;
	}
}

/* Decoded frame: dest, length (data bytes plus the checksum), data. */
void JvsHost::commit_encoded()
{
	m_recv_size = 0;
	if (m_send_size == 0) {
		return;
	}
	const uint32_t size = decode(m_send, m_send_size);
	m_send_size = 0;
	/* The checksum is the sum of every byte after the sync; a node
	 * answers status 3 when it does not match (JVS sum error). */
	uint8_t sum = 0;
	for (uint32_t i = 0; i < size; i++) {
		sum = static_cast<uint8_t>(sum + m_send[i]);
	}
	commit_raw(std::span<const uint8_t>(m_send.data(), size), sum == m_send[size]);
}

void JvsHost::commit_raw(std::span<const uint8_t> frame, bool sum_ok)
{
	const uint32_t size = static_cast<uint32_t>(frame.size());
	m_recv_size = 0;
	if (size < 3 || frame[0] == HOST || frame[1] != size - 1 || !sum_ok) {
		/* Malformed or bad checksum: sum error. */
		m_recv[0] = HOST;
		m_recv[1] = 0x02;	/* the status and the sum */
		m_recv[2] = STATUS_CHECKSUM_ERROR;
		m_recv_size = 3;
	} else if (m_first) {
		const uint32_t n = m_first->message(frame[0], frame.subspan(2),
		                                    std::span<uint8_t>(m_recv.data() + 2, BUFFER_SIZE - 3));
		if (n) {
			/* Reply header: the host is always destination 0. */
			m_recv[0] = HOST;
			m_recv[1] = static_cast<uint8_t>(n + 1);
			m_recv_size = n + 2;
		}
	}
	if (m_recv_size) {
		m_recv_size = encode(m_recv, m_recv_size);
	}
}

/* Strips the sync byte and unescapes D0 xx into xx+1. Returns the
 * decoded size without the checksum, which stays in the buffer right
 * after the data. */
uint32_t JvsHost::decode(std::span<uint8_t> buffer, uint32_t size)
{
	uint32_t pos = 0;
	for (uint32_t i = 0; i < size; i++) {
		uint8_t t = buffer[i];
		if (i == 0 && t == SYNC) {
			continue;
		}
		if (t == MARK) {
			i++;
			if (i >= size) {
				break;
			}
			t = static_cast<uint8_t>(buffer[i] + 1);
		}
		buffer[pos++] = t;
	}
	return pos ? pos - 1 : 0;
}

/* Appends the checksum, escapes D0 and E0 as D0 xx-1 and prepends the
 * sync byte. The buffer holds at most BUFFER_SIZE bytes; a reply that
 * would not fit after escaping is truncated, which the host sees as a
 * checksum error. */
uint32_t JvsHost::encode(std::span<uint8_t> buffer, uint32_t size)
{
	if (size == 0 || size + 1 > BUFFER_SIZE) {
		return 0;
	}
	uint8_t sum = 0;
	for (uint32_t i = 0; i < size; i++) {
		sum = static_cast<uint8_t>(sum + buffer[i]);
	}
	buffer[size++] = sum;
	uint32_t add = 1;
	for (uint32_t i = 0; i < size; i++) {
		if (buffer[i] == MARK || buffer[i] == SYNC) {
			add++;
		}
	}
	if (size + add > BUFFER_SIZE) {
		return 0;
	}
	for (uint32_t i = size, j = add; i; i--) {
		const uint8_t t = buffer[i - 1];
		if (t == MARK || t == SYNC) {
			buffer[i + j - 1] = static_cast<uint8_t>(t - 1);
			buffer[i + j - 2] = MARK;
			j--;
		} else {
			buffer[i + j - 1] = t;
		}
	}
	buffer[0] = SYNC;
	return size + add;
}

/* Device */

JvsDevice::JvsDevice()
	: m_next(nullptr), m_address(BROADCAST), m_reset_counter(0)
{
}

void JvsDevice::reset()
{
	m_address = BROADCAST;
	m_reset_counter = 0;
	device_reset();
	if (m_next) {
		m_next->reset();
	}
}

void JvsDevice::chain(JvsDevice *dev)
{
	if (m_next) {
		m_next->chain(dev);
	} else {
		m_next = dev;
	}
}

bool JvsDevice::address_set_line() const
{
	return m_address != BROADCAST && (!m_next || m_next->address_set_line());
}

uint32_t JvsDevice::message(uint8_t dest, std::span<const uint8_t> in, std::span<uint8_t> out)
{
	uint32_t size = 0;

	/* Set address: taken by the farthest node that has none yet. */
	if (in.size() == 2 && in[0] == SET_ADDRESS) {
		if (m_next && !m_next->address_set_line()) {
			return m_next->message(dest, in, out);
		}
		m_address = in[1];
		out[0] = STATUS_NORMAL;
		out[1] = REPORT_NORMAL;
		return 2;
	}

	if (dest == BROADCAST || dest == m_address) {
		uint32_t pos = 0;
		uint32_t used = 0;
		out[pos++] = STATUS_NORMAL;
		size_t s = 0;
		while (s < in.size()) {
			const int32_t len = handle_message(in.subspan(s), out.subspan(pos), used);
			if (len < 0) {
				out[0] = STATUS_UNKNOWN_COMMAND;
				return 1;
			}
			if (len == 0) {
				out[pos++] = REPORT_PARAMETER_COUNT;
				break;
			}
			pos += used;
			s += static_cast<size_t>(len);
		}
		/* The status byte alone is not a reply when a node behind us
		 * may still answer a broadcast (a reset, for instance). */
		if (!m_next || pos > 1) {
			size = pos;
		}
	}

	if (m_next && (dest == BROADCAST || size == 0)) {
		return m_next->message(dest, in, out);
	}
	return size;
}

int32_t JvsDevice::handle_message(std::span<const uint8_t> in, std::span<uint8_t> out, uint32_t &used)
{
	const uint32_t old_reset_counter = m_reset_counter;
	m_reset_counter = 0;
	used = 0;

	switch (in[0]) {
	case RESET:
		/* Sent twice. */
		if (in.size() < 2 || in[1] != RESET_CHECK) {
			return 0;
		}
		m_reset_counter = old_reset_counter + 1;
		if (m_reset_counter == 2) {
			m_address = BROADCAST;
			m_reset_counter = 0;
			device_reset();
		}
		return 2;

	case IO_IDENTIFY: {
		const char *id = device_id();
		const size_t len = std::strlen(id) + 1;
		out[0] = REPORT_NORMAL;
		std::memcpy(out.data() + 1, id, len);
		used = static_cast<uint32_t>(1 + len);
		return 1;
	}

	case COMMAND_REVISION:
		out[0] = REPORT_NORMAL;
		out[1] = command_format_version();
		used = 2;
		return 1;

	case JVS_REVISION:
		out[0] = REPORT_NORMAL;
		out[1] = jvs_standard_version();
		used = 2;
		return 1;

	case COMM_VERSION:
		out[0] = REPORT_NORMAL;
		out[1] = comm_method_version();
		used = 2;
		return 1;

	case FEATURE_CHECK: {
		out[0] = REPORT_NORMAL;
		const int32_t n = function_list(out.subspan(1));
		out[1 + static_cast<uint32_t>(n)] = FUNC_END;
		used = static_cast<uint32_t>(2 + n);
		return 1;
	}

	case SWITCH_INPUTS: {
		if (in.size() < 3) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		const int32_t n = switches(out.subspan(1), in[1], in[2]);
		if (n < 0) {
			return 0;
		}
		used = static_cast<uint32_t>(1 + n);
		return 3;
	}

	case COIN_INPUTS: {
		if (in.size() < 2) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		const int32_t n = coin_counters(out.subspan(1), in[1]);
		if (n < 0) {
			return 0;
		}
		used = static_cast<uint32_t>(1 + n);
		return 2;
	}

	case ANALOG_INPUTS: {
		if (in.size() < 2) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		const int32_t n = analogs(out.subspan(1), in[1]);
		if (n < 0) {
			return 0;
		}
		used = static_cast<uint32_t>(1 + n);
		return 2;
	}

	case COIN_COUNTER_DECREASE:
		if (in.size() < 4) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		used = 1;
		return coin_add(in[1], -static_cast<int32_t>((in[2] << 8) | in[3])) ? 4 : 0;

	case COIN_COUNTER_INCREASE:
		if (in.size() < 4) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		used = 1;
		return coin_add(in[1], static_cast<int32_t>((in[2] << 8) | in[3])) ? 4 : 0;

	case GENERIC_OUTPUT_1:
		if (in.size() < 2 || in.size() < 2u + in[1]) {
			return 0;
		}
		out[0] = REPORT_NORMAL;
		used = 1;
		return swoutputs(in.subspan(2, in[1])) ? static_cast<int32_t>(2 + in[1]) : 0;

	/* The programs of the 837-13551 (315-6215, jump table at 3000) and of
	 * the 837-13741 (epr-21868, 058B) take no other command: 31, 33, 34,
	 * 36, 37 and 38 are unknown to both. */
	default:
		return -1;
	}
}

void JvsHost::save(JvsHostState &out) const
{
	out.send = m_send;
	out.recv = m_recv;
	out.send_size = m_send_size;
	out.recv_size = m_recv_size;
}

void JvsHost::load(const JvsHostState &in)
{
	m_send = in.send;
	m_recv = in.recv;
	m_send_size = in.send_size;
	m_recv_size = in.recv_size;
}

void JvsDevice::save(JvsDeviceState &out) const
{
	out.reset_counter = m_reset_counter;
	out.address = m_address;
}

void JvsDevice::load(const JvsDeviceState &in)
{
	m_reset_counter = in.reset_counter;
	m_address = in.address;
}

} /* namespace odin */
