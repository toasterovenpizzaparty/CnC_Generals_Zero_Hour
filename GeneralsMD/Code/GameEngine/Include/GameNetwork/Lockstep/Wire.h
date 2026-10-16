/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Wire.h - byte encoding of the lockstep protocol messages.
//
// Every transport message starts with a one byte MessageType.  Integers are little-endian.
// A reader that runs past the end marks itself failed instead of reading garbage; a failed
// message is dropped.

#ifndef ZH_LOCKSTEP_WIRE_H
#define ZH_LOCKSTEP_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace Lockstep {

enum
{
	PROTOCOL_MAGIC = 0x534c485a,	// "ZHLS"
	PROTOCOL_VERSION = 1
};

enum MessageType
{
	// connection
	MSG_HELLO = 1,			// client -> host: I am this slot (magic, version, session, slot, exe/ini CRC)
	MSG_WELCOME,			// host -> client: accepted
	MSG_REJECT,				// host -> client: refused (reason)
	// loading
	MSG_LOAD_PROGRESS,		// client -> host, host -> all: slot, percent
	MSG_LOAD_COMPLETE,		// client -> host, host -> all: slot
	MSG_START,				// host -> all: everyone loaded; first turns follow
	// game
	MSG_COMMANDS,			// client -> host: the client's new commands
	MSG_TURN,				// host -> all: everything that happens on one frame
	MSG_STATUS,				// client -> host: frame executed, state CRCs
	MSG_LEAVE,				// client -> host: leaving the game
	MSG_WAITING,			// host -> all: who the game is waiting for (disconnect screen)
	MSG_VOTE,				// client -> host: vote to drop a player
	MSG_TURN_BUNDLE,		// host -> all, unreliable: the latest turns again (covers a lost packet)
	MSG_COMMAND_BUNDLE,		// client -> host, unreliable: commands not yet seen in a turn
	// host change
	MSG_RESUME,				// client -> new host: like HELLO, plus the last turn received and executed
	MSG_TURN_REQUEST,		// new host -> client: send me your turns from this frame on
	// bulk channel
	MSG_CHAT,				// any -> host -> recipients
	MSG_FILE_ANNOUNCE,		// sender -> host -> recipients: a file transfer begins
	MSG_FILE_DATA,			// sender -> host -> recipients: a piece of the file
	MSG_FILE_PROGRESS		// recipient -> host -> sender: percent received
};

enum RejectReason
{
	REJECT_VERSION = 1,		// different protocol version
	REJECT_SESSION,			// a different game
	REJECT_SLOT,			// slot not expected or already taken
	REJECT_EXECUTABLE		// different exe or INI files: the game would desync
};

// Frame-synchronized events carried in a turn.
enum TurnEventType
{
	EVENT_PLAYER_LEFT = 1,	// slot left the game (reason: LeaveReason)
	EVENT_DESYNC = 2		// players' state CRCs differed on frame `frame`
};

enum LeaveReason
{
	LEAVE_QUIT = 1,			// the player left
	LEAVE_DISCONNECTED,		// the connection was lost
	LEAVE_DROPPED			// the other players voted to drop them
};

class ByteWriter
{
public:
	void u8(uint32_t v) { m_data.push_back((uint8_t)v); }
	void u16(uint32_t v) { u8(v); u8(v >> 8); }
	void u32(uint32_t v) { u16(v); u16(v >> 16); }
	void bytes(const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; m_data.insert(m_data.end(), b, b + n); }
	void blob(const std::vector<uint8_t> &v) { u32((uint32_t)v.size()); if (!v.empty()) bytes(&v[0], v.size()); }
	void str(const std::string &s) { u16((uint32_t)s.size()); bytes(s.data(), s.size()); }
	void wstr(const std::vector<uint16_t> &s) { u16((uint32_t)s.size()); for (size_t i = 0; i < s.size(); ++i) u16(s[i]); }

	const std::vector<uint8_t> &data() const { return m_data; }
	std::vector<uint8_t> &data() { return m_data; }
	size_t size() const { return m_data.size(); }

private:
	std::vector<uint8_t> m_data;
};

class ByteReader
{
public:
	ByteReader(const void *data, size_t size) : m_p((const uint8_t *)data), m_end((const uint8_t *)data + size), m_ok(true) {}
	explicit ByteReader(const std::vector<uint8_t> &v) : m_p(v.empty() ? NULL : &v[0]), m_end(v.empty() ? NULL : &v[0] + v.size()), m_ok(true) {}

	uint32_t u8() { if (!need(1)) return 0; return *m_p++; }
	uint32_t u16() { uint32_t a = u8(); return a | (u8() << 8); }
	uint32_t u32() { uint32_t a = u16(); return a | (u16() << 16); }
	bool bytes(void *out, size_t n) { if (!need(n)) return false; memcpy(out, m_p, n); m_p += n; return true; }
	std::vector<uint8_t> blob()
	{
		uint32_t n = u32();
		std::vector<uint8_t> v;
		if (need(n)) { v.assign(m_p, m_p + n); m_p += n; }
		return v;
	}
	std::string str()
	{
		uint32_t n = u16();
		std::string s;
		if (need(n)) { s.assign((const char *)m_p, n); m_p += n; }
		return s;
	}
	std::vector<uint16_t> wstr()
	{
		uint32_t n = u16();
		std::vector<uint16_t> s;
		if (need(n * 2)) { s.resize(n); for (uint32_t i = 0; i < n; ++i) s[i] = (uint16_t)u16(); }
		return s;
	}

	bool ok() const { return m_ok; }
	bool atEnd() const { return m_p == m_end; }
	size_t remaining() const { return (size_t)(m_end - m_p); }

private:
	bool need(size_t n)
	{
		if (!m_ok || (size_t)(m_end - m_p) < n) { m_ok = false; return false; }
		return true;
	}

	const uint8_t *m_p;
	const uint8_t *m_end;
	bool m_ok;
};

} // namespace Lockstep

#endif // ZH_LOCKSTEP_WIRE_H
