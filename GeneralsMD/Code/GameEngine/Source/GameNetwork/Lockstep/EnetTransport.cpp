/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// EnetTransport.cpp - Transport over ENet.

#include "GameNetwork/Lockstep/EnetTransport.h"

#include <enet/enet.h>
#include <string.h>

namespace Lockstep {

int EnetTransport::s_instances = 0;

EnetTransport::EnetTransport()
	: m_host(NULL), m_timeoutMinimum(3000), m_timeoutMaximum(10000)
{
	if (s_instances++ == 0) {
		enet_initialize();
	}
}

EnetTransport::~EnetTransport()
{
	close();
	if (--s_instances == 0) {
		enet_deinitialize();
	}
}

bool EnetTransport::open(uint16_t port, int maxPeers)
{
	close();
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;
	m_host = enet_host_create(&address, (size_t)maxPeers, NUM_CHANNELS, 0, 0);
	return m_host != NULL;
}

void EnetTransport::close()
{
	if (m_host) {
		// Tell connected peers we are gone instead of letting them time out.
		for (size_t i = 0; i < m_peers.size(); ++i) {
			if (m_peers[i]) {
				enet_peer_disconnect_now(m_peers[i], 0);
			}
		}
		enet_host_flush(m_host);
		enet_host_destroy(m_host);
		m_host = NULL;
	}
	m_peers.clear();
}

void EnetTransport::setTimeouts(uint32_t minimumMs, uint32_t maximumMs)
{
	m_timeoutMinimum = minimumMs;
	m_timeoutMaximum = maximumMs;
	for (size_t i = 0; i < m_peers.size(); ++i) {
		if (m_peers[i]) {
			enet_peer_timeout(m_peers[i], 0, m_timeoutMinimum, m_timeoutMaximum);
		}
	}
}

PeerId EnetTransport::idOf(ENetPeer *peer)
{
	for (size_t i = 0; i < m_peers.size(); ++i) {
		if (m_peers[i] == peer) {
			return (PeerId)i;
		}
	}
	for (size_t i = 0; i < m_peers.size(); ++i) {
		if (!m_peers[i]) {
			m_peers[i] = peer;
			return (PeerId)i;
		}
	}
	m_peers.push_back(peer);
	return (PeerId)(m_peers.size() - 1);
}

PeerId EnetTransport::connect(uint32_t ipv4, uint16_t port)
{
	if (!m_host) {
		return NO_PEER;
	}
	ENetAddress address;
	address.host = ENET_HOST_TO_NET_32(ipv4);
	address.port = port;
	ENetPeer *peer = enet_host_connect(m_host, &address, NUM_CHANNELS, 0);
	if (!peer) {
		return NO_PEER;
	}
	enet_peer_timeout(peer, 0, m_timeoutMinimum, m_timeoutMaximum);
	return idOf(peer);
}

void EnetTransport::disconnect(PeerId peer)
{
	if (m_host && peer >= 0 && (size_t)peer < m_peers.size() && m_peers[peer]) {
		enet_peer_disconnect_later(m_peers[peer], 0);
	}
}

bool EnetTransport::send(PeerId peer, int channel, const void *data, size_t size)
{
	if (!m_host || peer < 0 || (size_t)peer >= m_peers.size() || !m_peers[peer]) {
		return false;
	}
	enet_uint32 flags = channel == CHANNEL_FAST ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE;
	ENetPacket *packet = enet_packet_create(data, size, flags);
	if (!packet) {
		return false;
	}
	if (enet_peer_send(m_peers[peer], (enet_uint8)channel, packet) != 0) {
		enet_packet_destroy(packet);
		return false;
	}
	return true;
}

bool EnetTransport::poll(TransportEvent &out)
{
	out = TransportEvent();
	if (!m_host) {
		return false;
	}
	ENetEvent event;
	if (enet_host_service(m_host, &event, 0) <= 0) {
		return false;
	}
	switch (event.type) {
		case ENET_EVENT_TYPE_CONNECT:
			out.type = TransportEvent::CONNECTED;
			out.peer = idOf(event.peer);
			enet_peer_timeout(event.peer, 0, m_timeoutMinimum, m_timeoutMaximum);
			return true;
		case ENET_EVENT_TYPE_DISCONNECT: {
			out.type = TransportEvent::DISCONNECTED;
			out.peer = idOf(event.peer);
			m_peers[out.peer] = NULL;	// the slot can be reused by a new connection
			return true;
		}
		case ENET_EVENT_TYPE_RECEIVE:
			out.type = TransportEvent::RECEIVED;
			out.peer = idOf(event.peer);
			out.channel = event.channelID;
			out.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
			enet_packet_destroy(event.packet);
			return true;
		default:
			return false;
	}
}

void EnetTransport::flush()
{
	if (m_host) {
		enet_host_flush(m_host);
	}
}

PeerStats EnetTransport::peerStats(PeerId peer) const
{
	PeerStats s;
	if (m_host && peer >= 0 && (size_t)peer < m_peers.size() && m_peers[peer]) {
		const ENetPeer *p = m_peers[peer];
		s.roundTripMs = p->roundTripTime;
		s.roundTripVarianceMs = p->roundTripTimeVariance;
		s.packetsLost = p->packetsLost;
		s.packetsSent = p->packetsSent;
	}
	return s;
}

TransportStats EnetTransport::stats() const
{
	TransportStats s;
	if (m_host) {
		s.bytesSent = m_host->totalSentData;
		s.bytesReceived = m_host->totalReceivedData;
		s.packetsSent = m_host->totalSentPackets;
		s.packetsReceived = m_host->totalReceivedPackets;
	}
	return s;
}

} // namespace Lockstep
