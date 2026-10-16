// license:BSD-3-Clause
// copyright-holders:Olivier Galibert
/*
 * JVS host side and device base, from MAME src/devices/machine/jvshost
 * and jvsdev, rewritten for Odin. Odin is GPL-3.0; this file keeps its
 * BSD-3-Clause licence and copyright holder.
 * Odin changes: Copyright (C) 2026 Réda Chérif-Touil
 */

#ifndef ODIN_JVS_HPP
#define ODIN_JVS_HPP

#include <array>
#include <cstdint>
#include <span>

namespace odin {

/* The JVS: the link between the main board and its I/O boards, as the
 * JAMMA Video Standard (third edition) defines it. The paragraphs quoted
 * (§) are those of its translation by A. Marshall (2021).
 *
 * The wire is an RS-485 pair. The host (the MIE of the main board) sends a
 * frame, the node it names answers with one:
 *
 *   E0  node  length  data...  sum
 *
 * E0 (SYNC) starts the frame; node is 1, 2... or FF for every node, and 00
 * for a reply to the host; length counts the data and the sum; the sum is
 * that of every byte after E0. A D0 or E0 after the first byte goes on the
 * wire as D0 (MARK) then the byte minus one (§ 3.1.1 to 3.1.4).
 *
 * The data of a request is one or more commands, each followed by its
 * parameters. The data of a reply is a status for the frame, then for each
 * command a report and what it returns (§ 4.2). */
namespace jvs {

enum : uint8_t {
	SYNC = 0xE0,
	MARK = 0xD0,
	BROADCAST = 0xFF,
	HOST = 0x00,
};

/* Commands (§ 4.1, table 5) */
enum Command : uint8_t {
	IO_IDENTIFY = 0x10,		/* § 4.3.4: the name of the board */
	COMMAND_REVISION = 0x11,	/* § 4.3.5 */
	JVS_REVISION = 0x12,		/* § 4.3.6 */
	COMM_VERSION = 0x13,		/* § 4.3.7 */
	FEATURE_CHECK = 0x14,		/* § 4.3.8: what the board has (FUNC_ codes) */
	MAIN_ID = 0x15,			/* § 4.3.9 */
	SWITCH_INPUTS = 0x20,		/* § 4.3.10 */
	COIN_INPUTS = 0x21,		/* § 4.3.11 */
	ANALOG_INPUTS = 0x22,		/* § 4.3.12 */
	ROTARY_INPUTS = 0x23,		/* § 4.3.13 */
	KEYCODE_INPUTS = 0x24,		/* § 4.3.14 */
	SCREEN_POSITION_INPUTS = 0x25,	/* § 4.3.15 */
	MISC_SWITCH_INPUTS = 0x26,	/* § 4.3.16 */
	REMAINING_PAYOUT = 0x2E,	/* § 4.3.17 */
	DATA_RETRANSMIT = 0x2F,		/* § 4.3.18 */
	COIN_COUNTER_DECREASE = 0x30,	/* § 4.3.19 */
	PAYOUT_COUNTER_INCREASE = 0x31,	/* § 4.3.20 */
	GENERIC_OUTPUT_1 = 0x32,	/* § 4.3.21: all the outputs, first one bit 7 */
	ANALOG_OUTPUT = 0x33,		/* § 4.3.24 */
	CHARACTER_OUTPUT = 0x34,	/* § 4.3.25 */
	COIN_COUNTER_INCREASE = 0x35,	/* § 4.3.26 */
	PAYOUT_COUNTER_DECREASE = 0x36,	/* § 4.3.27 */
	GENERIC_OUTPUT_2 = 0x37,	/* § 4.3.22: eight outputs */
	GENERIC_OUTPUT_3 = 0x38,	/* § 4.3.23: one output */
	RESET = 0xF0,			/* § 4.3.1: F0 D9, twice, to every node */
	SET_ADDRESS = 0xF1,		/* § 4.3.2 */
	COMM_METHOD_CHANGE = 0xF2,	/* § 4.3.3 */
};

/* The second byte of RESET */
inline constexpr uint8_t RESET_CHECK = 0xD9;

/* Status of a reply (§ 4.2, table 6) */
enum Status : uint8_t {
	STATUS_NORMAL = 0x01,
	STATUS_UNKNOWN_COMMAND = 0x02,
	STATUS_CHECKSUM_ERROR = 0x03,
	STATUS_ACK_OVERFLOW = 0x04,
};

/* Report of a command (§ 4.2, table 7) */
enum Report : uint8_t {
	REPORT_NORMAL = 0x01,
	REPORT_PARAMETER_COUNT = 0x02,	/* too few or too many parameters */
	REPORT_PARAMETER_DATA = 0x03,	/* a parameter out of range */
	REPORT_BUSY = 0x04,
};

/* What FEATURE_CHECK lists, each with three parameters (§ 4.3.8, table 8) */
enum Function : uint8_t {
	FUNC_END = 0x00,
	FUNC_SWITCH_INPUT = 0x01,	/* players, switches per player */
	FUNC_COIN_INPUT = 0x02,		/* slots */
	FUNC_ANALOG_INPUT = 0x03,	/* channels, bits */
	FUNC_ROTARY_INPUT = 0x04,	/* channels */
	FUNC_KEYCODE_INPUT = 0x05,
	FUNC_SCREEN_POSITION_INPUT = 0x06, /* X bits, Y bits, channels */
	FUNC_MISC_SWITCH_INPUT = 0x07,	/* switches, on two bytes */
	FUNC_CARD_SYSTEM = 0x10,	/* slots */
	FUNC_MEDAL_HOPPER = 0x11,	/* channels */
	FUNC_GENERAL_OUTPUT = 0x12,	/* outputs */
	FUNC_ANALOG_OUTPUT = 0x13,	/* channels */
	FUNC_CHARACTER_OUTPUT = 0x14,	/* width, height, type */
	FUNC_BACKUP = 0x15,
};

} /* namespace jvs */

/* What an I/O board reads on its connectors. JVS bit order (§ 4.3.10):
 * system byte bit 7 TEST, 6 TILT1, 5 TILT2, 4 TILT3; player word high byte
 * bit 7 START, 6 SERVICE, 5 UP, 4 DOWN, 3 LEFT, 2 RIGHT, 1 PUSH1, 0 PUSH2,
 * low byte bits 7..2 PUSH3..PUSH8; analog channels 16-bit, 0x8000 centre. */
class JvsInputs {
public:
	virtual ~JvsInputs() = default;
	virtual uint8_t system() = 0;
	virtual uint16_t player(uint8_t index) = 0;
	virtual uint16_t analog(uint8_t channel) = 0;
};

class JvsDevice;

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

/* The cable, seen from the host end: the bytes the UART of the host sends
 * are an encoded frame; commit_encoded decodes it, hands it to the nodes in
 * their order on the cable and keeps their encoded reply for the UART to
 * receive. */
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

/* One node on the cable. The commands every board answers the same way
 * (reset, address, versions, feature list, the polls) are decoded here; a
 * board fills in what its hardware answers. */
class JvsDevice {
public:
	virtual ~JvsDevice() = default;

	void reset();
	void chain(JvsDevice *dev);
	/* Handles one frame for `dest` (BROADCAST for all). Returns the reply
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
	/* Whether a refused GENERIC_OUTPUT_1 skips its data bytes, or leaves
	 * them to be read as the next commands. */
	virtual bool refused_outputs_skip_data() const { return true; }
	virtual void device_reset() {}

private:
	/* Returns the request bytes consumed, the command refused or not, or
	 * -1 for an unknown command. `used` receives the reply bytes. */
	int32_t handle_message(std::span<const uint8_t> in, std::span<uint8_t> out, uint32_t &used);

	JvsDevice *m_next;
	uint8_t m_address;
	uint32_t m_reset_counter;
};

} /* namespace odin */

#endif /* ODIN_JVS_HPP */
