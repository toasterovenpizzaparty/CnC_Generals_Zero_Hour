/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// EnetTransport.h - Transport over ENet (reliable, ordered UDP; Libraries/Source/ENet).

#ifndef ZH_LOCKSTEP_ENETTRANSPORT_H
#define ZH_LOCKSTEP_ENETTRANSPORT_H

#include "GameNetwork/Lockstep/Transport.h"

struct _ENetHost;
struct _ENetPeer;

namespace Lockstep {

class EnetTransport : public Transport
{
public:
	EnetTransport();
	virtual ~EnetTransport();

	virtual bool open(uint16_t port, int maxPeers);
	virtual void close();
	virtual PeerId connect(uint32_t ipv4, uint16_t port);
	virtual void disconnect(PeerId peer);
	virtual bool send(PeerId peer, int channel, const void *data, size_t size);
	virtual bool poll(TransportEvent &event);
	virtual void flush();
	virtual PeerStats peerStats(PeerId peer) const;
	virtual TransportStats stats() const;

	/// How long an unresponsive peer is kept before DISCONNECTED (ENet's retransmission timeouts).
	void setTimeouts(uint32_t minimumMs, uint32_t maximumMs);

private:
	PeerId idOf(struct _ENetPeer *peer);

	struct _ENetHost *m_host;
	std::vector<struct _ENetPeer *> m_peers;	// index = PeerId
	uint32_t m_timeoutMinimum;
	uint32_t m_timeoutMaximum;
	static int s_instances;
};

} // namespace Lockstep

#endif // ZH_LOCKSTEP_ENETTRANSPORT_H
