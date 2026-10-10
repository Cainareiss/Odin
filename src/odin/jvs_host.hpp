// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * JVS host side and device base, from MAME src/devices/machine/jvshost
 * and jvsdev, rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holder.
 */

#ifndef ODIN_JVS_HOST_HPP
#define ODIN_JVS_HOST_HPP

#include <array>
#include <cstdint>
#include <span>

namespace odin {

class JvsDevice;

/* The master end of a JVS bus: one frame at a time. Bytes pushed by the
 * UART are an encoded frame (E0 sync, D0 escapes, checksum); commit
 * decodes it, hands it to the device chain and keeps the encoded reply
 * for the UART to drain. */
/* The frame being sent and the last reply. Plain data. */
struct JvsHostState {
	std::array<uint8_t, 512> send;
	std::array<uint8_t, 512> recv;
	uint32_t send_size;
	uint32_t recv_size;
};

/* What the generic part of a node keeps. Plain data. */
struct JvsDeviceState {
	uint32_t reset_counter;
	uint8_t address;
};

class JvsHost {
public:
	static constexpr uint32_t BUFFER_SIZE = 512;

	JvsHost();

	void reset();
	void add_device(JvsDevice *dev);

	void push(uint8_t value);
	void commit_encoded();
	std::span<const uint8_t> reply() const { return std::span<const uint8_t>(m_recv.data(), m_recv_size); }

	/* Sense line: a device is wired. */
	bool presence_line() const { return m_first != nullptr; }
	/* Sense line: every device has taken an address. */
	bool address_set_line() const;

	void save(JvsHostState &out) const;
	void load(const JvsHostState &in);

private:
	void commit_raw(std::span<const uint8_t> frame, bool sum_ok);
	static uint32_t decode(std::span<uint8_t> buffer, uint32_t size);
	static uint32_t encode(std::span<uint8_t> buffer, uint32_t size);

	JvsDevice *m_first;
	std::array<uint8_t, BUFFER_SIZE> m_send;
	std::array<uint8_t, BUFFER_SIZE> m_recv;
	uint32_t m_send_size;
	uint32_t m_recv_size;
};

/* One node on the bus. The generic commands (reset, address, versions,
 * function list, polls) are decoded here; a board fills in what its
 * hardware answers. */
class JvsDevice {
public:
	virtual ~JvsDevice() = default;

	void reset();
	void chain(JvsDevice *dev);
	/* Handles one frame for `dest` (0xFF broadcast). Returns the reply
	 * size written to `out`, 0 when this node stays silent. */
	uint32_t message(uint8_t dest, std::span<const uint8_t> in, std::span<uint8_t> out);
	bool address_set_line() const;
	uint8_t address() const { return m_address; }

	void save(JvsDeviceState &out) const;
	void load(const JvsDeviceState &in);

protected:
	JvsDevice();

	virtual const char *device_id() const { return ""; }
	virtual uint8_t command_format_version() const { return 0x13; }
	virtual uint8_t jvs_standard_version() const { return 0x30; }
	virtual uint8_t comm_method_version() const { return 0x10; }
	/* Each hook appends to `out` and returns the bytes written, or
	 * -1 when the parameters are refused. */
	virtual int32_t function_list(std::span<uint8_t>) { return 0; }
	virtual int32_t switches(std::span<uint8_t>, uint8_t, uint8_t) { return -1; }
	virtual int32_t coin_counters(std::span<uint8_t>, uint8_t) { return -1; }
	virtual bool coin_add(uint8_t, int32_t) { return false; }
	virtual int32_t analogs(std::span<uint8_t>, uint8_t) { return -1; }
	virtual bool swoutputs(std::span<const uint8_t>) { return false; }
	virtual bool swoutput(uint8_t, uint8_t) { return false; }
	virtual void device_reset() {}

private:
	/* Returns the request bytes consumed, 0 for a bad parameter, -1
	 * for an unknown command. `used` receives the reply bytes. */
	int32_t handle_message(std::span<const uint8_t> in, std::span<uint8_t> out, uint32_t &used);

	JvsDevice *m_next;
	uint8_t m_address;
	uint32_t m_reset_counter;
};

} /* namespace odin */

#endif /* ODIN_JVS_HOST_HPP */
