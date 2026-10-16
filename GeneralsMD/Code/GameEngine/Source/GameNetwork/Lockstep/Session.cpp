/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Session.cpp - lockstep game session; see Session.h for the model.

#include "GameNetwork/Lockstep/Session.h"

#include <algorithm>
#include <math.h>

namespace Lockstep {

namespace {

// Message size limits, so a corrupt or hostile message cannot make us allocate without bound.
const uint32_t MAX_COMMANDS_PER_MESSAGE = 4096;
const uint32_t MAX_EVENTS_PER_TURN = 64;

} // namespace

// ================================================================================================
// Construction, start, shutdown
// ================================================================================================
Session::Session(Transport *transport, const SessionConfig &config)
	: m_transport(transport), m_config(config), m_hostPeer(NO_PEER), m_welcomed(false), m_running(false), m_started(false),
	m_left(false), m_localLoaded(false), m_nowMs(0), m_lastTaken(config.firstFrame - 1),
	m_lastReceived(config.firstFrame - 1), m_inputDelay(MIN_INPUT_DELAY), m_nextFrameMs(0),
	m_waitingShown(false), m_starvedSinceMs(0), m_nextSeq(1), m_desyncAnnounced(false),
	m_lastDelayUpdateMs(0), m_lastDelayDecreaseMs(0), m_lastWaitingBroadcastMs(0)
{
	m_statusFrame = 0;
	m_statusSentFrame = 0;
	m_hostWaitingMs = 0;
	m_lastBundleMs = 0;
	m_lastStarveMs = 0;
	m_cushionCurrent = 1000;
	m_cushionPrevious = 1000;
	m_cushionBucketMs = 0;
	m_lastStatusSentMs = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		m_nextSeqFrom[i] = 1;
	}
	if (m_config.frameRate <= 0) {
		m_config.frameRate = DEFAULT_FRAME_RATE;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		m_inGame[i] = m_config.players[i].human;
		m_hostWaiting[i] = 0;
	}
}

Session::~Session()
{
	shutdown();
}

bool Session::start(uint32_t nowMs)
{
	m_nowMs = nowMs;
	const PlayerAddress &self = m_config.players[m_config.localSlot];
	if (!m_transport->open(self.port, MAX_PLAYERS)) {
		return false;
	}
	m_running = true;
	if (!isHost()) {
		const PlayerAddress &host = m_config.players[m_config.hostSlot];
		m_hostPeer = m_transport->connect(host.ip, host.port);
		if (m_hostPeer == NO_PEER) {
			push(Event(Event::CONNECT_FAILED));
			return false;
		}
	}
	return true;
}

void Session::shutdown()
{
	if (!m_running) {
		return;
	}
	leave();
	m_transport->flush();
	m_transport->close();
	m_running = false;
}

bool Session::isPlaying(int slot) const
{
	if (slot < 0 || slot >= MAX_PLAYERS || !m_config.players[slot].human) {
		return false;
	}
	if (slot == m_config.localSlot) {
		return !m_left;
	}
	const Remote &r = m_remote[slot];
	if (isHost()) {
		return r.connected && !r.left;
	}
	return m_inGame[slot];
}

bool Session::inGame(int slot) const
{
	if (slot < 0 || slot >= MAX_PLAYERS) {
		return false;
	}
	return m_inGame[slot];
}

int Session::slotOfPeer(PeerId peer) const
{
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (m_remote[i].peer == peer && m_remote[i].connected) {
			return i;
		}
	}
	return -1;
}

// ================================================================================================
// Sending helpers
// ================================================================================================
void Session::sendTo(int slot, int channel, const ByteWriter &w)
{
	if (slot >= 0 && slot < MAX_PLAYERS && m_remote[slot].connected && !m_remote[slot].left) {
		m_transport->send(m_remote[slot].peer, channel, &w.data()[0], w.size());
	}
}

void Session::sendToHost(int channel, const ByteWriter &w)
{
	if (m_hostPeer != NO_PEER) {
		m_transport->send(m_hostPeer, channel, &w.data()[0], w.size());
	}
}

void Session::broadcast(int channel, const ByteWriter &w, int exceptSlot)
{
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (i != exceptSlot && i != m_config.localSlot) {
			sendTo(i, channel, w);
		}
	}
}

// ================================================================================================
// Update
// ================================================================================================
void Session::update(uint32_t nowMs)
{
	m_nowMs = nowMs;
	if (!m_running) {
		return;
	}

	TransportEvent e;
	while (m_transport->poll(e)) {
		switch (e.type) {
			case TransportEvent::CONNECTED:
				if (!isHost() && e.peer == m_hostPeer) {
					ByteWriter w;
					w.u8(MSG_HELLO);
					w.u32(PROTOCOL_MAGIC);
					w.u16(PROTOCOL_VERSION);
					w.u32(m_config.sessionId);
					w.u8(m_config.localSlot);
					w.u32(m_config.exeCRC);
					w.u32(m_config.iniCRC);
					sendToHost(CHANNEL_GAME, w);
				}
				break;

			case TransportEvent::DISCONNECTED:
				if (isHost()) {
					int slot = slotOfPeer(e.peer);
					if (slot >= 0) {
						m_remote[slot].connected = false;
						if (!m_remote[slot].left) {
							Event lost(Event::PLAYER_LOST);
							lost.slot = slot;
							push(lost);
							hostPlayerGone(slot, LEAVE_DISCONNECTED);
						}
					}
				} else if (e.peer == m_hostPeer) {
					m_hostPeer = NO_PEER;
					if (!m_left) {
						push(Event(m_welcomed ? Event::HOST_LOST : Event::CONNECT_FAILED));
					}
				}
				break;

			case TransportEvent::RECEIVED:
				handleMessage(e.peer, e.channel, e.data, nowMs);
				break;

			default:
				break;
		}
	}

	if (isHost()) {
		if (m_started && !m_left) {
			hostUpdateInputDelay(nowMs);
			hostUpdateWaiting(nowMs);
		}
	} else if (m_hostPeer != NO_PEER && !m_left) {
		clientSendCommands(nowMs);
		if (nowMs - m_cushionBucketMs >= CUSHION_BUCKET_MS) {
			m_cushionBucketMs = nowMs;
			m_cushionPrevious = m_cushionCurrent;
			m_cushionCurrent = 1000;
		}
		if (m_started && (m_statusFrame != m_statusSentFrame || !m_unsentCRCs.empty() ||
			nowMs - m_lastStatusSentMs >= STATUS_MIN_INTERVAL_MS)) {
			// Only the latest frame matters, so a status without CRCs may be lost.
			m_lastStatusSentMs = nowMs;
			int cushion = std::min(m_cushionCurrent, m_cushionPrevious);
			ByteWriter w;
			w.u8(MSG_STATUS);
			w.u32(m_statusFrame);
			w.u8((uint32_t)std::max(0, std::min(255, cushion + 1)));	// 0 = ran out of turns
			w.u8((uint32_t)m_unsentCRCs.size());
			for (size_t i = 0; i < m_unsentCRCs.size(); ++i) {
				w.u32(m_unsentCRCs[i].first);
				w.u32(m_unsentCRCs[i].second);
			}
			sendToHost(m_unsentCRCs.empty() ? CHANNEL_FAST : CHANNEL_GAME, w);
			m_statusSentFrame = m_statusFrame;
			m_unsentCRCs.clear();
		}
		clientUpdateWaiting(nowMs);
	}

	m_transport->flush();
}

// ================================================================================================
// Loading
// ================================================================================================
void Session::sendLoadProgress(int percent)
{
	ByteWriter w;
	w.u8(MSG_LOAD_PROGRESS);
	if (isHost()) {
		w.u8(m_config.localSlot);
		w.u8(percent);
		broadcast(CHANNEL_GAME, w);
	} else {
		w.u8(percent);
		sendToHost(CHANNEL_GAME, w);
	}
}

void Session::sendLoadComplete()
{
	m_localLoaded = true;
	ByteWriter w;
	w.u8(MSG_LOAD_COMPLETE);
	if (isHost()) {
		w.u8(m_config.localSlot);
		broadcast(CHANNEL_GAME, w);
		hostCheckStart();
	} else {
		sendToHost(CHANNEL_GAME, w);
	}
}

void Session::forceStart()
{
	if (!isHost() || m_started) {
		return;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (i != m_config.localSlot && m_config.players[i].human && !m_remote[i].left &&
			!(m_remote[i].connected && m_remote[i].loaded)) {
			hostPlayerGone(i, LEAVE_DISCONNECTED);
			if (m_remote[i].peer != NO_PEER) {
				m_transport->disconnect(m_remote[i].peer);
			}
		}
	}
	hostCheckStart();
}

// ================================================================================================
// Game
// ================================================================================================
void Session::submitCommand(const std::vector<uint8_t> &data)
{
	if (m_left) {
		return;
	}
	Command c;
	c.slot = (uint8_t)m_config.localSlot;
	c.seq = m_nextSeq++;
	c.data = data;
	if (isHost()) {
		m_pending.push_back(c);
	} else {
		m_outgoing.push_back(c);
	}
}

bool Session::readyForFrame(Frame next, uint32_t nowMs)
{
	m_nowMs = nowMs;
	if (!m_started) {
		return false;
	}
	if (m_turns.find(next) == m_turns.end()) {
		if (m_starvedSinceMs == 0) {
			m_starvedSinceMs = nowMs ? nowMs : 1;
		}
		// Waiting for a turn whose time has come: the input delay is too small for this link.
		if (!isHost() && m_nextFrameMs != 0.0 && (double)nowMs >= m_nextFrameMs) {
			noteCushion(-1);
		}
		return false;
	}

	double period = 1000.0 / m_config.frameRate;
	if (isHost()) {
		// Slow down, and finally wait, for a client that falls far behind.
		int lag = hostMaxClientLag(NULL);
		if (lag > m_inputDelay + LAG_STOP_FRAMES) {
			return false;
		} else if (lag > m_inputDelay + LAG_SLOWER_FRAMES) {
			period *= 2.0;
		} else if (lag > m_inputDelay + LAG_SLOW_FRAMES) {
			period *= 1.25;
		}
	} else {
		// Behind the host (turns piling up): catch up by running faster, up to as fast as possible.
		int buffered = (int)(m_lastReceived - next) + 1;
		int surplus = buffered - (m_inputDelay + 1);
		if (surplus > m_config.frameRate / 2) {
			period = 0.0;
		} else if (surplus > 0) {
			period /= 1.0 + 0.25 * std::min(surplus, 4);
		}
	}

	const double now = (double)nowMs;
	if (m_nextFrameMs == 0.0 || m_nextFrameMs < now - 2.0 * (1000.0 / m_config.frameRate)) {
		m_nextFrameMs = now;	// first frame, or after a stall: don't try to make up the lost time
	}
	if (now < m_nextFrameMs) {
		return false;
	}
	if (!isHost()) {
		noteCushion((int)(m_lastReceived - next));
	}
	m_nextFrameMs += period;
	m_starvedSinceMs = 0;
	return true;
}

bool Session::takeTurn(Frame f, Turn &turn)
{
	std::map<Frame, Turn>::iterator it = m_turns.find(f);
	if (it == m_turns.end()) {
		return false;
	}
	turn = it->second;
	m_turns.erase(it);
	m_lastTaken = f;

	// Frame-synchronized membership.
	for (size_t i = 0; i < turn.events.size(); ++i) {
		if (turn.events[i].type == EVENT_PLAYER_LEFT && turn.events[i].slot < MAX_PLAYERS) {
			m_inGame[turn.events[i].slot] = false;
		}
	}

	m_history.push_back(turn);
	while (m_history.size() > TURN_HISTORY) {
		m_history.pop_front();
	}

	if (isHost()) {
		// Executing frame f releases the turn D frames ahead (several if D just grew).
		for (Frame r = m_lastReceived + 1; r <= f + (Frame)m_inputDelay; ++r) {
			hostRelease(r);
		}
	} else {
		m_statusFrame = f;
	}
	return true;
}

void Session::reportCRC(Frame f, uint32_t crc)
{
	if (isHost()) {
		CRCSet &set = m_crcs[f];
		if (set.have[m_config.localSlot] == false) {
			set.crc[m_config.localSlot] = crc;
			set.have[m_config.localSlot] = true;
		}
		hostCheckCRCs();
	} else {
		m_unsentCRCs.push_back(std::make_pair(f, crc));
	}
}

void Session::voteToDrop(int slot)
{
	if (slot < 0 || slot >= MAX_PLAYERS) {
		return;
	}
	if (isHost()) {
		hostVote(m_config.localSlot, slot);
	} else {
		ByteWriter w;
		w.u8(MSG_VOTE);
		w.u8(slot);
		sendToHost(CHANNEL_GAME, w);
	}
}

void Session::leave()
{
	if (m_left || !m_running) {
		return;
	}
	m_left = true;
	if (isHost()) {
		// Tell the clients; they lose the game's host.
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			if (m_remote[i].connected) {
				m_transport->disconnect(m_remote[i].peer);
			}
		}
	} else if (m_hostPeer != NO_PEER) {
		ByteWriter w;
		w.u8(MSG_LEAVE);
		sendToHost(CHANNEL_GAME, w);
		m_transport->disconnect(m_hostPeer);	// after the queued messages
	}
	m_transport->flush();
}

// ================================================================================================
// Chat and files
// ================================================================================================
void Session::deliverBulk(int fromSlot, uint32_t mask, const ByteWriter &w, const Event &localEvent)
{
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (!(mask & (1u << i)) || i == fromSlot) {
			continue;
		}
		if (i == m_config.localSlot) {
			push(localEvent);
		} else {
			sendTo(i, CHANNEL_BULK, w);
		}
	}
}

void Session::sendChat(uint32_t mask, const std::vector<uint16_t> &text)
{
	if (isHost()) {
		ByteWriter w;
		w.u8(MSG_CHAT);
		w.u8(m_config.localSlot);
		w.u32(mask);
		w.wstr(text);
		Event e(Event::CHAT);
		deliverBulk(m_config.localSlot, mask, w, e);
	} else {
		ByteWriter w;
		w.u8(MSG_CHAT);
		w.u32(mask);
		w.wstr(text);
		sendToHost(CHANNEL_BULK, w);
	}
}

void Session::announceFile(uint32_t id, const std::string &name, uint32_t totalSize, uint32_t mask)
{
	if (isHost()) {
		m_fileMasks[std::make_pair(m_config.localSlot, id)] = mask;
		ByteWriter w;
		w.u8(MSG_FILE_ANNOUNCE);
		w.u8(m_config.localSlot);
		w.u32(id);
		w.str(name);
		w.u32(totalSize);
		w.u32(mask);
		deliverBulk(m_config.localSlot, mask, w, Event(Event::FILE_ANNOUNCE));
	} else {
		ByteWriter w;
		w.u8(MSG_FILE_ANNOUNCE);
		w.u32(id);
		w.str(name);
		w.u32(totalSize);
		w.u32(mask);
		sendToHost(CHANNEL_BULK, w);
	}
}

void Session::sendFileData(uint32_t id, uint32_t offset, const std::vector<uint8_t> &data)
{
	if (isHost()) {
		std::map<std::pair<int, uint32_t>, uint32_t>::iterator it = m_fileMasks.find(std::make_pair(m_config.localSlot, id));
		if (it == m_fileMasks.end()) {
			return;
		}
		ByteWriter w;
		w.u8(MSG_FILE_DATA);
		w.u8(m_config.localSlot);
		w.u32(id);
		w.u32(offset);
		w.blob(data);
		deliverBulk(m_config.localSlot, it->second, w, Event(Event::FILE_DATA));
	} else {
		ByteWriter w;
		w.u8(MSG_FILE_DATA);
		w.u32(id);
		w.u32(offset);
		w.blob(data);
		sendToHost(CHANNEL_BULK, w);
	}
}

void Session::sendFileProgress(int senderSlot, uint32_t id, int percent)
{
	ByteWriter w;
	w.u8(MSG_FILE_PROGRESS);
	if (isHost()) {
		w.u8(m_config.localSlot);	// who reports
		w.u32(id);
		w.u8(percent);
		sendTo(senderSlot, CHANNEL_BULK, w);
	} else {
		w.u8(senderSlot);			// whom to tell
		w.u32(id);
		w.u8(percent);
		sendToHost(CHANNEL_BULK, w);
	}
}

// ================================================================================================
// Events and stats
// ================================================================================================
bool Session::pollEvent(Event &event)
{
	if (m_events.empty()) {
		return false;
	}
	event = m_events.front();
	m_events.pop_front();
	return true;
}

SessionStats Session::stats() const
{
	SessionStats s;
	s.inputDelay = m_inputDelay;
	s.bufferedTurns = (int)(m_lastReceived - m_lastTaken);
	s.maxClientLag = isHost() ? hostMaxClientLag(NULL) : 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		PeerId peer = isHost() ? (m_remote[i].connected ? m_remote[i].peer : NO_PEER)
			: (i == m_config.hostSlot ? m_hostPeer : NO_PEER);
		s.roundTripMs[i] = peer != NO_PEER ? m_transport->peerStats(peer).roundTripMs : 0;
	}
	return s;
}

// ================================================================================================
// Message dispatch
// ================================================================================================
void Session::handleMessage(PeerId peer, int, const std::vector<uint8_t> &data, uint32_t nowMs)
{
	ByteReader r(data);
	uint32_t type = r.u8();
	if (!r.ok()) {
		return;
	}
	if (isHost()) {
		int slot = slotOfPeer(peer);
		if (type == MSG_HELLO) {
			hostHello(peer, r);
		} else if (slot >= 0) {
			hostHandle(slot, type, r, nowMs);
		}
	} else if (peer == m_hostPeer) {
		clientHandle(type, r, nowMs);
	}
}

// ================================================================================================
// Host
// ================================================================================================
void Session::hostHello(PeerId peer, ByteReader &r)
{
	uint32_t magic = r.u32();
	uint32_t version = r.u16();
	uint32_t session = r.u32();
	uint32_t slot = r.u8();
	uint32_t exeCRC = r.u32();
	uint32_t iniCRC = r.u32();

	int reason = 0;
	if (!r.ok() || magic != PROTOCOL_MAGIC || version != PROTOCOL_VERSION) {
		reason = REJECT_VERSION;
	} else if (session != m_config.sessionId) {
		reason = REJECT_SESSION;
	} else if (slot >= MAX_PLAYERS || (int)slot == m_config.localSlot || !m_config.players[slot].human ||
		m_remote[slot].connected || m_remote[slot].left || m_started) {
		reason = REJECT_SLOT;
	} else if (exeCRC != m_config.exeCRC || iniCRC != m_config.iniCRC) {
		reason = REJECT_EXECUTABLE;
	}

	ByteWriter w;
	if (reason) {
		w.u8(MSG_REJECT);
		w.u8(reason);
		m_transport->send(peer, CHANNEL_GAME, &w.data()[0], w.size());
		m_transport->disconnect(peer);
		return;
	}

	Remote &rem = m_remote[slot];
	rem.peer = peer;
	rem.connected = true;
	rem.helloReceived = true;
	rem.lastFrame = m_config.firstFrame - 1;
	w.u8(MSG_WELCOME);
	w.u8(slot);
	m_transport->send(peer, CHANNEL_GAME, &w.data()[0], w.size());

	Event e(Event::PLAYER_JOINED);
	e.slot = (int)slot;
	push(e);
}

void Session::hostHandle(int slot, uint32_t type, ByteReader &r, uint32_t nowMs)
{
	Remote &rem = m_remote[slot];
	if (rem.left) {
		return;
	}
	switch (type) {
		case MSG_LOAD_PROGRESS: {
			uint32_t percent = r.u8();
			if (!r.ok()) return;
			ByteWriter w;
			w.u8(MSG_LOAD_PROGRESS);
			w.u8(slot);
			w.u8(percent);
			broadcast(CHANNEL_GAME, w, slot);
			Event e(Event::LOAD_PROGRESS);
			e.slot = slot;
			e.value = (int)percent;
			push(e);
			break;
		}

		case MSG_LOAD_COMPLETE: {
			rem.loaded = true;
			ByteWriter w;
			w.u8(MSG_LOAD_COMPLETE);
			w.u8(slot);
			broadcast(CHANNEL_GAME, w, slot);
			Event e(Event::LOAD_COMPLETE);
			e.slot = slot;
			push(e);
			hostCheckStart();
			break;
		}

		case MSG_COMMANDS:
		case MSG_COMMAND_BUNDLE: {
			uint32_t n = r.u16();
			if (n > MAX_COMMANDS_PER_MESSAGE) return;
			for (uint32_t i = 0; i < n && r.ok(); ++i) {
				uint32_t seq = r.u32();
				std::vector<uint8_t> data = r.blob();
				if (r.ok()) {
					hostAcceptCommand(slot, seq, data);
				}
			}
			break;
		}

		case MSG_STATUS: {
			Frame frame = r.u32();
			int cushion = (int)r.u8() - 1;
			uint32_t n = r.u8();
			if (!r.ok()) return;
			rem.cushion = cushion;
			rem.cushionMs = nowMs;
			if (frame > rem.lastFrame) {
				rem.lastFrame = frame;
			}
			rem.lastStatusMs = nowMs;
			for (uint32_t i = 0; i < n; ++i) {
				Frame f = r.u32();
				uint32_t crc = r.u32();
				if (!r.ok()) break;
				CRCSet &set = m_crcs[f];
				set.crc[slot] = crc;
				set.have[slot] = true;
			}
			hostCheckCRCs();
			break;
		}

		case MSG_LEAVE:
			hostPlayerGone(slot, LEAVE_QUIT);
			break;

		case MSG_VOTE: {
			uint32_t target = r.u8();
			if (r.ok()) hostVote(slot, (int)target);
			break;
		}

		case MSG_CHAT: {
			uint32_t mask = r.u32();
			std::vector<uint16_t> text = r.wstr();
			if (!r.ok()) return;
			ByteWriter w;
			w.u8(MSG_CHAT);
			w.u8(slot);
			w.u32(mask);
			w.wstr(text);
			Event e(Event::CHAT);
			e.slot = slot;
			e.mask = mask;
			e.text = text;
			deliverBulk(slot, mask, w, e);
			break;
		}

		case MSG_FILE_ANNOUNCE: {
			uint32_t id = r.u32();
			std::string name = r.str();
			uint32_t size = r.u32();
			uint32_t mask = r.u32();
			if (!r.ok()) return;
			m_fileMasks[std::make_pair(slot, id)] = mask;
			ByteWriter w;
			w.u8(MSG_FILE_ANNOUNCE);
			w.u8(slot);
			w.u32(id);
			w.str(name);
			w.u32(size);
			w.u32(mask);
			Event e(Event::FILE_ANNOUNCE);
			e.slot = slot;
			e.id = id;
			e.name = name;
			e.value = (int)size;
			e.mask = mask;
			deliverBulk(slot, mask, w, e);
			break;
		}

		case MSG_FILE_DATA: {
			uint32_t id = r.u32();
			uint32_t offset = r.u32();
			std::vector<uint8_t> data = r.blob();
			if (!r.ok()) return;
			std::map<std::pair<int, uint32_t>, uint32_t>::iterator it = m_fileMasks.find(std::make_pair(slot, id));
			if (it == m_fileMasks.end()) return;
			ByteWriter w;
			w.u8(MSG_FILE_DATA);
			w.u8(slot);
			w.u32(id);
			w.u32(offset);
			w.blob(data);
			Event e(Event::FILE_DATA);
			e.slot = slot;
			e.id = id;
			e.value = (int)offset;
			e.data = data;
			deliverBulk(slot, it->second, w, e);
			break;
		}

		case MSG_FILE_PROGRESS: {
			uint32_t sender = r.u8();
			uint32_t id = r.u32();
			uint32_t percent = r.u8();
			if (!r.ok() || sender >= MAX_PLAYERS) return;
			if ((int)sender == m_config.localSlot) {
				Event e(Event::FILE_PROGRESS);
				e.slot = slot;
				e.id = id;
				e.value = (int)percent;
				push(e);
			} else {
				ByteWriter w;
				w.u8(MSG_FILE_PROGRESS);
				w.u8(slot);
				w.u32(id);
				w.u8(percent);
				sendTo((int)sender, CHANNEL_BULK, w);
			}
			break;
		}

		default:
			break;
	}
}

void Session::hostCheckStart()
{
	if (m_started || !m_localLoaded) {
		return;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (i == m_config.localSlot || !m_config.players[i].human || m_remote[i].left) {
			continue;
		}
		if (!m_remote[i].connected || !m_remote[i].loaded) {
			return;
		}
	}
	m_started = true;
	ByteWriter w;
	w.u8(MSG_START);
	broadcast(CHANNEL_GAME, w);
	push(Event(Event::GAME_START));
	// The first D turns are released now; afterwards executing frame n releases n + D.
	for (Frame f = m_config.firstFrame; f < m_config.firstFrame + (Frame)m_inputDelay; ++f) {
		hostRelease(f);
	}
}

void Session::hostRelease(Frame f)
{
	Turn t;
	t.frame = f;
	t.inputDelay = (uint8_t)m_inputDelay;
	t.events.swap(m_pendingEvents);
	t.commands.swap(m_pending);

	ByteWriter w;
	encodeTurn(w, t);
	broadcast(CHANNEL_GAME, w);

	// The same turn plus the previous ones, unreliably: if a reliable packet is lost, ENet holds back
	// every later turn until it is resent; the next bundle delivers them on time instead.
	ByteWriter body;
	encodeTurnBody(body, t);
	m_recentTurns.push_back(body.data());
	while (m_recentTurns.size() > REDUNDANT_TURNS) {
		m_recentTurns.pop_front();
	}
	size_t first = 0, bytes = 0;
	for (size_t i = m_recentTurns.size(); i-- > 0; ) {
		if (bytes + m_recentTurns[i].size() > REDUNDANT_BUNDLE_BYTES && i + 1 < m_recentTurns.size()) {
			first = i + 1;
			break;
		}
		bytes += m_recentTurns[i].size();
	}
	ByteWriter bundle;
	bundle.u8(MSG_TURN_BUNDLE);
	bundle.u8((uint32_t)(m_recentTurns.size() - first));
	for (size_t i = first; i < m_recentTurns.size(); ++i) {
		bundle.bytes(&m_recentTurns[i][0], m_recentTurns[i].size());
	}
	broadcast(CHANNEL_FAST, bundle);

	storeTurn(t);
}

void Session::hostAcceptCommand(int slot, uint32_t seq, const std::vector<uint8_t> &data)
{
	// Each player's commands are used once and in the order they were issued, whichever copy
	// (reliable or redundant) arrives first.
	if (seq < m_nextSeqFrom[slot]) {
		return;		// already have it
	}
	if (seq > m_nextSeqFrom[slot]) {
		m_early[slot][seq] = data;
		return;
	}
	Command c;
	c.slot = (uint8_t)slot;
	c.seq = seq;
	c.data = data;
	m_pending.push_back(c);
	++m_nextSeqFrom[slot];
	std::map<uint32_t, std::vector<uint8_t> >::iterator it;
	while ((it = m_early[slot].find(m_nextSeqFrom[slot])) != m_early[slot].end()) {
		c.seq = it->first;
		c.data = it->second;
		m_pending.push_back(c);
		m_early[slot].erase(it);
		++m_nextSeqFrom[slot];
	}
}

void Session::clientSendCommands(uint32_t nowMs)
{
	// New commands: reliably, at once.
	if (!m_outgoing.empty()) {
		ByteWriter w;
		w.u8(MSG_COMMANDS);
		w.u16((uint32_t)m_outgoing.size());
		for (size_t i = 0; i < m_outgoing.size(); ++i) {
			w.u32(m_outgoing[i].seq);
			w.blob(m_outgoing[i].data);
		}
		sendToHost(CHANNEL_GAME, w);
		m_unacked.insert(m_unacked.end(), m_outgoing.begin(), m_outgoing.end());
		m_outgoing.clear();
		m_lastBundleMs = nowMs;
		return;
	}
	// Commands the host has not put in a turn yet: again, unreliably, in case the reliable packet
	// was lost and waits for its retransmission.
	if (!m_unacked.empty() && nowMs - m_lastBundleMs >= COMMAND_RESEND_MS) {
		m_lastBundleMs = nowMs;
		ByteWriter w;
		w.u8(MSG_COMMAND_BUNDLE);
		size_t countAt = w.size();
		w.u16(0);
		uint32_t n = 0;
		for (size_t i = 0; i < m_unacked.size() && w.size() + m_unacked[i].data.size() + 8 <= REDUNDANT_BUNDLE_BYTES; ++i, ++n) {
			w.u32(m_unacked[i].seq);
			w.blob(m_unacked[i].data);
		}
		w.data()[countAt] = (uint8_t)n;
		w.data()[countAt + 1] = (uint8_t)(n >> 8);
		if (n > 0) {
			sendToHost(CHANNEL_FAST, w);
		}
	}
}

void Session::storeTurn(const Turn &t)
{
	if (!isHost() && t.frame >= m_lastReceived && t.inputDelay >= MIN_INPUT_DELAY && t.inputDelay <= MAX_INPUT_DELAY) {
		// Clients keep the host's input delay worth of turns in hand: that is the slack that rides
		// out a late packet, so they only run faster when they hold more than that.
		m_inputDelay = t.inputDelay;
	}
	if (!isHost() && !m_unacked.empty()) {
		// Our commands in this turn reached the host: stop repeating them.
		uint32_t acked = 0;
		for (size_t i = 0; i < t.commands.size(); ++i) {
			if (t.commands[i].slot == m_config.localSlot && t.commands[i].seq > acked) {
				acked = t.commands[i].seq;
			}
		}
		if (acked) {
			size_t keep = 0;
			while (keep < m_unacked.size() && m_unacked[keep].seq <= acked) {
				++keep;
			}
			m_unacked.erase(m_unacked.begin(), m_unacked.begin() + keep);
		}
	}
	if (t.frame > m_lastTaken && m_turns.find(t.frame) == m_turns.end()) {
		m_turns[t.frame] = t;
		if (t.frame > m_lastReceived) {
			m_lastReceived = t.frame;
		}
	}
}

void Session::hostPlayerGone(int slot, int reason)
{
	Remote &rem = m_remote[slot];
	if (rem.left) {
		return;
	}
	rem.left = true;
	rem.waitingSinceMs = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		m_remote[i].dropVotes[slot] = false;
		rem.dropVotes[i] = false;
	}

	TurnEvent ev;
	ev.type = EVENT_PLAYER_LEFT;
	ev.slot = (uint8_t)slot;
	ev.reason = (uint8_t)reason;
	ev.frame = 0;
	m_pendingEvents.push_back(ev);

	// Commands it sent before leaving still count (e.g. its self-destruct); they are already in
	// m_pending in arrival order, before this event's turn is released.
	hostCheckCRCs();
	hostCheckStart();
}

void Session::hostVote(int voter, int target)
{
	if (target < 0 || target >= MAX_PLAYERS || target == m_config.localSlot || !isPlaying(target) ||
		!isPlaying(voter) || voter == target) {
		return;
	}
	m_remote[target].dropVotes[voter] = true;
	int voters = 0, votes = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (i != target && isPlaying(i)) {
			++voters;
			if (m_remote[target].dropVotes[i]) {
				++votes;
			}
		}
	}
	if (votes * 2 > voters) {
		hostPlayerGone(target, LEAVE_DROPPED);
		if (m_remote[target].peer != NO_PEER) {
			m_transport->disconnect(m_remote[target].peer);
		}
	}
}

void Session::hostCheckCRCs()
{
	std::map<Frame, CRCSet>::iterator it = m_crcs.begin();
	while (it != m_crcs.end()) {
		const CRCSet &set = it->second;
		bool complete = true;
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			if (isPlaying(i) && !set.have[i]) {
				complete = false;
				break;
			}
		}
		if (!complete) {
			// Keep waiting, unless this frame is long past (a player stopped reporting).
			if (m_lastTaken > it->first + 3000) {
				m_crcs.erase(it++);
			} else {
				++it;
			}
			continue;
		}
		bool mismatch = false;
		int first = -1;
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			if (!set.have[i]) {
				continue;
			}
			if (first < 0) {
				first = i;
			} else if (set.crc[i] != set.crc[first]) {
				mismatch = true;
			}
		}
		if (mismatch && !m_desyncAnnounced) {
			m_desyncAnnounced = true;
			TurnEvent ev;
			ev.type = EVENT_DESYNC;
			ev.slot = 0;
			ev.reason = 0;
			ev.frame = it->first;
			m_pendingEvents.push_back(ev);
		}
		m_crcs.erase(it++);
	}
}

int Session::hostClientLag(int slot) const
{
	const Remote &rem = m_remote[slot];
	if (!rem.connected || rem.left) {
		return 0;
	}
	// Where the client is now: its last report plus the frames played while the report travelled.
	PeerStats ps = m_transport->peerStats(rem.peer);
	double travelFrames = (ps.roundTripMs / 2.0) * m_config.frameRate / 1000.0;
	double estimate = (double)rem.lastFrame + travelFrames;
	double lag = (double)m_lastTaken - estimate;
	return lag > 0.0 ? (int)lag : 0;
}

int Session::hostMaxClientLag(int *slowest) const
{
	int worst = 0;
	if (slowest) {
		*slowest = -1;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		if (i == m_config.localSlot) {
			continue;
		}
		int lag = hostClientLag(i);
		if (lag > worst) {
			worst = lag;
			if (slowest) {
				*slowest = i;
			}
		}
	}
	return worst;
}

void Session::noteCushion(int cushion)
{
	if (cushion < m_cushionCurrent) {
		m_cushionCurrent = cushion;
	}
}

void Session::hostUpdateInputDelay(uint32_t nowMs)
{
	if (nowMs - m_lastDelayUpdateMs < INPUT_DELAY_UPDATE_MS) {
		return;
	}
	m_lastDelayUpdateMs = nowMs;

	// The input delay D is the slack between the host releasing a turn and the clients needing it.
	// Clients report their cushion: how many turns beyond the current one they held, at worst, over
	// the last second.  It is driven by what actually happened rather than by a jitter estimate:
	// isolated late or lost packets are covered by the redundant turn bundles and cost nothing.
	//   - a client ran out of turns: one more frame of delay, at once;
	//   - every client had two or more spare turns and nobody ran out for a while: one frame less.
	int worst = 1000;
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		const Remote &rem = m_remote[i];
		if (i == m_config.localSlot || !rem.connected || rem.left || rem.cushionMs == 0 ||
			nowMs - rem.cushionMs > 3 * CUSHION_BUCKET_MS) {
			continue;
		}
		worst = std::min(worst, rem.cushion);
	}
	if (worst < 0) {
		m_lastStarveMs = nowMs;
		if (m_inputDelay < MAX_INPUT_DELAY) {
			++m_inputDelay;
			m_lastDelayDecreaseMs = nowMs;
		}
	} else if (worst >= 2 && worst < 1000 && m_inputDelay > MIN_INPUT_DELAY &&
		nowMs - m_lastStarveMs >= INPUT_DELAY_CALM_MS && nowMs - m_lastDelayDecreaseMs >= INPUT_DELAY_DECREASE_MS) {
		--m_inputDelay;
		m_lastDelayDecreaseMs = nowMs;
	}
}

void Session::hostUpdateWaiting(uint32_t nowMs)
{
	bool any = false;
	Event e(Event::WAITING);
	for (int i = 0; i < MAX_PLAYERS; ++i) {
		Remote &rem = m_remote[i];
		if (i == m_config.localSlot || !rem.connected || rem.left) {
			continue;
		}
		if (hostClientLag(i) > m_inputDelay + LAG_STOP_FRAMES) {
			if (rem.waitingSinceMs == 0) {
				rem.waitingSinceMs = nowMs ? nowMs : 1;
			}
			uint32_t ms = nowMs - rem.waitingSinceMs;
			if (ms >= WAITING_REPORT_MS) {
				e.waitingMs[i] = ms;
				any = true;
			}
		} else {
			rem.waitingSinceMs = 0;
		}
	}

	if (any && nowMs - m_lastWaitingBroadcastMs >= WAITING_BROADCAST_MS) {
		m_lastWaitingBroadcastMs = nowMs;
		m_waitingShown = true;
		ByteWriter w;
		w.u8(MSG_WAITING);
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			w.u32(e.waitingMs[i]);
		}
		broadcast(CHANNEL_GAME, w);
		push(e);
	} else if (!any && m_waitingShown) {
		m_waitingShown = false;
		ByteWriter w;
		w.u8(MSG_WAITING);
		for (int i = 0; i < MAX_PLAYERS; ++i) {
			w.u32(0);
		}
		broadcast(CHANNEL_GAME, w);
		push(Event(Event::WAITING));
	}
}

// ================================================================================================
// Client
// ================================================================================================
void Session::clientHandle(uint32_t type, ByteReader &r, uint32_t nowMs)
{
	switch (type) {
		case MSG_WELCOME:
			m_welcomed = true;
			push(Event(Event::CONNECTED));
			break;

		case MSG_REJECT: {
			Event e(Event::CONNECT_FAILED);
			e.value = (int)r.u8();
			push(e);
			m_left = true;	// do not report a lost host on the disconnect that follows
			break;
		}

		case MSG_LOAD_PROGRESS: {
			Event e(Event::LOAD_PROGRESS);
			e.slot = (int)r.u8();
			e.value = (int)r.u8();
			if (r.ok() && e.slot < MAX_PLAYERS) push(e);
			break;
		}

		case MSG_LOAD_COMPLETE: {
			Event e(Event::LOAD_COMPLETE);
			e.slot = (int)r.u8();
			if (r.ok() && e.slot < MAX_PLAYERS) push(e);
			break;
		}

		case MSG_START:
			if (!m_started) {
				m_started = true;
				push(Event(Event::GAME_START));
			}
			break;

		case MSG_TURN: {
			Turn t;
			if (decodeTurn(r, t)) {
				storeTurn(t);
			}
			break;
		}

		case MSG_TURN_BUNDLE: {
			uint32_t n = r.u8();
			for (uint32_t i = 0; i < n; ++i) {
				Turn t;
				if (!decodeTurn(r, t)) {
					break;
				}
				storeTurn(t);
			}
			break;
		}

		case MSG_WAITING: {
			bool any = false;
			for (int i = 0; i < MAX_PLAYERS; ++i) {
				m_hostWaiting[i] = r.u32();
				any = any || m_hostWaiting[i] != 0;
			}
			if (!r.ok()) return;
			m_hostWaitingMs = any ? nowMs : 0;
			Event e(Event::WAITING);
			memcpy(e.waitingMs, m_hostWaiting, sizeof(e.waitingMs));
			push(e);
			m_waitingShown = any;
			break;
		}

		case MSG_CHAT: {
			Event e(Event::CHAT);
			e.slot = (int)r.u8();
			e.mask = r.u32();
			e.text = r.wstr();
			if (r.ok()) push(e);
			break;
		}

		case MSG_FILE_ANNOUNCE: {
			Event e(Event::FILE_ANNOUNCE);
			e.slot = (int)r.u8();
			e.id = r.u32();
			e.name = r.str();
			e.value = (int)r.u32();
			e.mask = r.u32();
			if (r.ok()) push(e);
			break;
		}

		case MSG_FILE_DATA: {
			Event e(Event::FILE_DATA);
			e.slot = (int)r.u8();
			e.id = r.u32();
			e.value = (int)r.u32();
			e.data = r.blob();
			if (r.ok()) push(e);
			break;
		}

		case MSG_FILE_PROGRESS: {
			Event e(Event::FILE_PROGRESS);
			e.slot = (int)r.u8();
			e.id = r.u32();
			e.value = (int)r.u8();
			if (r.ok()) push(e);
			break;
		}

		default:
			break;
	}
}

void Session::clientUpdateWaiting(uint32_t nowMs)
{
	// Starved of turns for a while without the host saying whom it waits for: we wait for the host.
	if (!m_started) {
		return;
	}
	bool starved = m_turns.find(m_lastTaken + 1) == m_turns.end();
	if (!starved) {
		m_starvedSinceMs = 0;
		if (m_waitingShown && m_hostWaitingMs == 0) {
			m_waitingShown = false;
			push(Event(Event::WAITING));
		}
		return;
	}
	if (m_starvedSinceMs == 0) {
		m_starvedSinceMs = nowMs ? nowMs : 1;
		return;
	}
	uint32_t ms = nowMs - m_starvedSinceMs;
	bool hostExplained = m_hostWaitingMs != 0 && nowMs - m_hostWaitingMs < 2 * WAITING_BROADCAST_MS;
	if (ms >= WAITING_REPORT_MS && !hostExplained && nowMs - m_lastWaitingBroadcastMs >= WAITING_BROADCAST_MS) {
		m_lastWaitingBroadcastMs = nowMs;
		m_waitingShown = true;
		Event e(Event::WAITING);
		e.waitingMs[m_config.hostSlot] = ms;
		push(e);
	}
}

// ================================================================================================
// Turn encoding
// ================================================================================================
void Session::encodeTurn(ByteWriter &w, const Turn &t)
{
	w.u8(MSG_TURN);
	encodeTurnBody(w, t);
}

void Session::encodeTurnBody(ByteWriter &w, const Turn &t)
{
	w.u32(t.frame);
	w.u8(t.inputDelay);
	w.u8((uint32_t)t.events.size());
	for (size_t i = 0; i < t.events.size(); ++i) {
		w.u8(t.events[i].type);
		w.u8(t.events[i].slot);
		w.u8(t.events[i].reason);
		w.u32(t.events[i].frame);
	}
	w.u16((uint32_t)t.commands.size());
	for (size_t i = 0; i < t.commands.size(); ++i) {
		w.u8(t.commands[i].slot);
		w.u32(t.commands[i].seq);
		w.blob(t.commands[i].data);
	}
}

bool Session::decodeTurn(ByteReader &r, Turn &t)
{
	t.frame = r.u32();
	t.inputDelay = (uint8_t)r.u8();
	uint32_t ne = r.u8();
	if (ne > MAX_EVENTS_PER_TURN) return false;
	t.events.resize(ne);
	for (uint32_t i = 0; i < ne; ++i) {
		t.events[i].type = (uint8_t)r.u8();
		t.events[i].slot = (uint8_t)r.u8();
		t.events[i].reason = (uint8_t)r.u8();
		t.events[i].frame = r.u32();
	}
	uint32_t nc = r.u16();
	if (nc > MAX_COMMANDS_PER_MESSAGE) return false;
	t.commands.resize(nc);
	for (uint32_t i = 0; i < nc && r.ok(); ++i) {
		t.commands[i].slot = (uint8_t)r.u8();
		t.commands[i].seq = r.u32();
		t.commands[i].data = r.blob();
	}
	return r.ok();
}

} // namespace Lockstep
