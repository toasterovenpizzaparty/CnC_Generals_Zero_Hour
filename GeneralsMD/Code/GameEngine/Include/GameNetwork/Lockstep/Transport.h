/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Transport.h - message transport used by the lockstep session.
//
// Delivers whole messages to and from peers.  Channel 0 carries the game (commands, turns, status),
// channel 1 bulk data (chat, file transfers) so a large file never delays a turn; both are reliable
// and ordered.  Channel 2 is unreliable and unordered: redundant copies of recent turns and
// commands, which arrive in time when a reliable packet is lost and waits to be resent.
//
// The real implementation is EnetTransport.  Tests use an in-memory transport with simulated
// latency.  Nothing in here depends on the game engine.

#ifndef ZH_LOCKSTEP_TRANSPORT_H
#define ZH_LOCKSTEP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace Lockstep {

typedef int PeerId;		// >= 0; -1 = none
enum { NO_PEER = -1 };

enum Channel
{
	CHANNEL_GAME = 0,
	CHANNEL_BULK = 1,
	CHANNEL_FAST = 2,		// unreliable, unordered
	NUM_CHANNELS = 3
};

struct TransportEvent
{
	enum Type { NONE, CONNECTED, DISCONNECTED, RECEIVED };
	Type type;
	PeerId peer;
	int channel;
	std::vector<uint8_t> data;

	TransportEvent() : type(NONE), peer(NO_PEER), channel(0) {}
};

struct PeerStats
{
	uint32_t roundTripMs;			// smoothed round trip time
	uint32_t roundTripVarianceMs;	// its mean deviation
	uint32_t packetsLost;
	uint32_t packetsSent;

	PeerStats() : roundTripMs(0), roundTripVarianceMs(0), packetsLost(0), packetsSent(0) {}
};

struct TransportStats
{
	uint64_t bytesSent;
	uint64_t bytesReceived;
	uint64_t packetsSent;
	uint64_t packetsReceived;

	TransportStats() : bytesSent(0), bytesReceived(0), packetsSent(0), packetsReceived(0) {}
};

class Transport
{
public:
	virtual ~Transport() {}

	/// Starts the transport on a local UDP port (0 = any).  Must be called first.
	virtual bool open(uint16_t port, int maxPeers) = 0;
	virtual void close() = 0;

	/// Starts connecting to a peer; CONNECTED or DISCONNECTED follows.  ipv4 is in host byte order.
	virtual PeerId connect(uint32_t ipv4, uint16_t port) = 0;
	/// Sends what is queued, then disconnects (DISCONNECTED follows).
	virtual void disconnect(PeerId peer) = 0;

	virtual bool send(PeerId peer, int channel, const void *data, size_t size) = 0;

	/// Sends queued messages and returns the next event (type NONE when there is none).
	virtual bool poll(TransportEvent &event) = 0;
	/// Sends everything queued right away.
	virtual void flush() = 0;

	virtual PeerStats peerStats(PeerId peer) const = 0;
	virtual TransportStats stats() const = 0;
};

} // namespace Lockstep

#endif // ZH_LOCKSTEP_TRANSPORT_H
