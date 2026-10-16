/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Tests for the lockstep session (GameNetwork/Lockstep).
//
// Part 1 runs complete games between several players over a simulated network (latency, jitter,
// spikes, packet loss, leaving, crashing, slow computers) in simulated time.  Every player runs a
// stand-in for the game: a state hash that absorbs each executed command in order.  The tests check
// that every player executed exactly the same commands on the same frames, that every command was
// executed exactly once, and measure input delay and how long players were frozen.
//
// Part 2 runs a short game over real ENet on 127.0.0.1.
//
//   g++ -O2 -std=c++14 -I../../GameEngine/Include -I../../Libraries/Source/ENet/include
//       lockstep_test.cpp ../../GameEngine/Source/GameNetwork/Lockstep/Session.cpp
//       ../../GameEngine/Source/GameNetwork/Lockstep/EnetTransport.cpp ../../Libraries/Source/ENet/*.c
//       -o lockstep_test && ./lockstep_test

#include "GameNetwork/Lockstep/Session.h"
#include "GameNetwork/Lockstep/EnetTransport.h"

#include <algorithm>
#include <math.h>
#include <map>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
static uint32_t RealMs() { return GetTickCount(); }
static void SleepMs(int ms) { Sleep(ms); }
#else
#include <time.h>
#include <unistd.h>
static uint32_t RealMs() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
static void SleepMs(int ms) { usleep(ms * 1000); }
#endif

using namespace Lockstep;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (cond) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
	else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); ++g_failures; } } while (0)

// Deterministic random numbers (the same run every time).
struct Rng
{
	uint64_t s;
	explicit Rng(uint64_t seed) : s(seed * 2862933555777941757ull + 3037000493ull) {}
	uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); }
	double uniform() { return next() / 2147483648.0; }
};

// =================================================================================================
// Simulated network
// =================================================================================================
struct LinkProfile
{
	const char *name;
	double baseMs;			// one-way latency
	double jitterMs;		// + uniform 0..jitter
	double spikeChance;		// per message: delayed by spikeMs (Wi-Fi retries, scans)
	double spikeMs;
	double lossChance;		// per message: lost once, resent after the retransmission timeout
	double dropoutEveryMs;	// the link stops completely every this many ms (Wi-Fi scan, interference)...
	double dropoutMs;		// ...for this long; everything sent meanwhile arrives when it ends
};

static const LinkProfile WIRED = { "wired LAN", 0.3, 0.3, 0.0, 0.0, 0.0, 0, 0 };
static const LinkProfile GOOD_WIFI = { "good Wi-Fi (0.2% loss)", 2.0, 4.0, 0.002, 60.0, 0.002, 0, 0 };
static const LinkProfile BUSY_WIFI = { "busy Wi-Fi (1% loss, spikes)", 3.0, 10.0, 0.01, 150.0, 0.01, 0, 0 };
static const LinkProfile BAD_WIFI = { "bad Wi-Fi (3% loss, big spikes)", 5.0, 20.0, 0.02, 300.0, 0.03, 0, 0 };
static const LinkProfile DROPOUT_WIFI = { "Wi-Fi, 250 ms dropout every 20 s", 2.0, 4.0, 0.002, 60.0, 0.002, 20000, 250 };

class SimNetwork;

class SimTransport : public Transport
{
public:
	SimTransport(SimNetwork *net, uint32_t ip) : m_net(net), m_ip(ip), m_port(0), m_open(false) {}
	virtual bool open(uint16_t port, int);
	virtual void close();
	virtual PeerId connect(uint32_t ipv4, uint16_t port);
	virtual void disconnect(PeerId peer);
	virtual bool send(PeerId peer, int channel, const void *data, size_t size);
	virtual bool poll(TransportEvent &event);
	virtual void flush() {}
	virtual PeerStats peerStats(PeerId peer) const;
	virtual TransportStats stats() const { return m_stats; }

	SimNetwork *m_net;
	uint32_t m_ip;
	uint16_t m_port;
	bool m_open;
	std::vector<int> m_conns;		// PeerId -> connection index in the network (-1 = closed)
	TransportStats m_stats;
};

class SimNetwork
{
public:
	struct Message
	{
		double at;
		TransportEvent event;
		int toEndpoint;			// connection end: 0 or 1
	};

	struct Connection
	{
		SimTransport *end[2];
		PeerId peerAt[2];		// PeerId of this connection at each end
		LinkProfile link;
		double lastAt[2][NUM_CHANNELS];	// ordered delivery: per direction and channel
		bool open;
		// round trip estimate like ENet's
		double srtt, rttvar;
		uint32_t lost;
	};

	double now;
	Rng rng;
	std::vector<Connection> conns;
	std::vector<Message> inflight;
	std::map<uint32_t, LinkProfile> linkOf;		// ip -> link to the host
	std::vector<SimTransport *> transports;
	std::map<uint32_t, bool> crashed;			// ip -> machine is dead (sends and receives nothing)

	SimNetwork(uint64_t seed) : now(0), rng(seed) {}

	SimTransport *find(uint32_t ip, uint16_t port)
	{
		for (size_t i = 0; i < transports.size(); ++i)
			if (transports[i]->m_open && transports[i]->m_ip == ip && transports[i]->m_port == port) return transports[i];
		return NULL;
	}

	LinkProfile linkBetween(uint32_t a, uint32_t b)
	{
		// The worse of the two machines' links (each machine has one link profile).
		LinkProfile la = linkOf.count(a) ? linkOf[a] : WIRED, lb = linkOf.count(b) ? linkOf[b] : WIRED;
		return la.baseMs + la.jitterMs >= lb.baseMs + lb.jitterMs ? la : lb;
	}

	// One-way delay of a message.  Like ENet, the round trip estimate is sampled per transmission
	// (each packet's own acknowledgement), so it is not inflated by retransmissions or by messages
	// waiting behind a delayed one.
	// Returns -1 when an unreliable message is lost.
	double delay(Connection &c, bool reliable)
	{
		const LinkProfile &l = c.link;
		double d = l.baseMs + rng.uniform() * l.jitterMs;
		if (rng.uniform() < l.spikeChance) d += l.spikeMs;
		if (l.dropoutEveryMs > 0) {
			double into = fmod(now + 7000.0, l.dropoutEveryMs);	// dropouts at 13 s, 33 s, ...
			if (into < l.dropoutMs) d += l.dropoutMs - into;
		}
		double sample = 2 * d;
		c.rttvar = 0.75 * c.rttvar + 0.25 * fabs(sample - c.srtt);
		c.srtt = 0.875 * c.srtt + 0.125 * sample;
		if (rng.uniform() < l.lossChance) {
			++c.lost;
			if (!reliable) return -1;
			// lost once: resent after ENet's retransmission timeout (rtt + 4 * variance), then delivered
			d += c.srtt + 4 * c.rttvar + l.baseMs + rng.uniform() * l.jitterMs;
		}
		return d;
	}

	void deliver(int conn, int toEnd, TransportEvent::Type type, int channel, const std::vector<uint8_t> &data)
	{
		Connection &c = conns[conn];
		const bool reliable = !(type == TransportEvent::RECEIVED && channel == CHANNEL_FAST);
		double d = delay(c, reliable);
		if (d < 0) return;	// lost
		double at = now + d;
		if (type == TransportEvent::RECEIVED && reliable) {
			at = std::max(at, c.lastAt[toEnd][channel]);	// reliable and ordered per channel
			c.lastAt[toEnd][channel] = at;
		} else if (type == TransportEvent::RECEIVED) {
			// unreliable: independent of everything else
		} else {
			// after everything already sent
			for (int ch = 0; ch < NUM_CHANNELS; ++ch) if (ch != CHANNEL_FAST) at = std::max(at, c.lastAt[toEnd][ch]);
		}
		Message m;
		m.at = at;
		m.event.type = type;
		m.event.peer = c.peerAt[toEnd];
		m.event.channel = channel;
		m.event.data = data;
		m.toEndpoint = conn * 2 + toEnd;
		inflight.push_back(m);
	}

	bool next(SimTransport *t, TransportEvent &out)
	{
		// earliest due message for this transport
		int best = -1;
		for (size_t i = 0; i < inflight.size(); ++i) {
			const Message &m = inflight[i];
			Connection &c = conns[m.toEndpoint / 2];
			if (c.end[m.toEndpoint % 2] == t && m.at <= now && (best < 0 || m.at < inflight[best].at)) best = (int)i;
		}
		if (best < 0) return false;
		out = inflight[best].event;
		inflight.erase(inflight.begin() + best);
		return true;
	}

	// A crashed machine's peers notice after the connection timeout.
	void crash(uint32_t ip, double timeoutMs)
	{
		crashed[ip] = true;
		for (size_t i = 0; i < conns.size(); ++i) {
			Connection &c = conns[i];
			if (!c.open) continue;
			for (int e = 0; e < 2; ++e) {
				if (c.end[e]->m_ip == ip) {
					c.open = false;
					int other = 1 - e;
					Message m;
					m.at = now + timeoutMs;
					m.event.type = TransportEvent::DISCONNECTED;
					m.event.peer = c.peerAt[other];
					m.toEndpoint = (int)i * 2 + other;
					inflight.push_back(m);
				}
			}
		}
		// nothing it had in flight arrives any more, and it receives nothing
		for (size_t i = 0; i < inflight.size(); ) {
			Connection &c = conns[inflight[i].toEndpoint / 2];
			if (c.end[inflight[i].toEndpoint % 2]->m_ip == ip ||
				(inflight[i].event.type == TransportEvent::RECEIVED && c.end[1 - inflight[i].toEndpoint % 2]->m_ip == ip))
				inflight.erase(inflight.begin() + i);
			else ++i;
		}
	}
};

bool SimTransport::open(uint16_t port, int) { m_port = port; m_open = true; m_net->transports.push_back(this); return true; }

void SimTransport::close()
{
	for (size_t i = 0; i < m_conns.size(); ++i) disconnect((PeerId)i);
	m_open = false;
}

PeerId SimTransport::connect(uint32_t ipv4, uint16_t port)
{
	SimTransport *other = m_net->find(ipv4, port);
	if (!other) return NO_PEER;
	SimNetwork::Connection c;
	c.end[0] = this;
	c.end[1] = other;
	c.link = m_net->linkBetween(m_ip, ipv4);
	for (int e = 0; e < 2; ++e) for (int ch = 0; ch < NUM_CHANNELS; ++ch) c.lastAt[e][ch] = 0;
	c.open = true;
	c.srtt = 2 * (c.link.baseMs + c.link.jitterMs / 2);
	c.rttvar = c.link.jitterMs / 2;
	c.lost = 0;
	int idx = (int)m_net->conns.size();
	c.peerAt[0] = (PeerId)m_conns.size();
	m_conns.push_back(idx);
	c.peerAt[1] = (PeerId)other->m_conns.size();
	other->m_conns.push_back(idx);
	m_net->conns.push_back(c);
	m_net->deliver(idx, 1, TransportEvent::CONNECTED, 0, std::vector<uint8_t>());
	m_net->deliver(idx, 0, TransportEvent::CONNECTED, 0, std::vector<uint8_t>());
	return c.peerAt[0];
}

void SimTransport::disconnect(PeerId peer)
{
	if (peer < 0 || (size_t)peer >= m_conns.size() || m_conns[peer] < 0) return;
	int idx = m_conns[peer];
	SimNetwork::Connection &c = m_net->conns[idx];
	m_conns[peer] = -1;
	if (!c.open) return;
	c.open = false;
	int me = c.end[0] == this ? 0 : 1;
	m_net->deliver(idx, 1 - me, TransportEvent::DISCONNECTED, 0, std::vector<uint8_t>());
}

bool SimTransport::send(PeerId peer, int channel, const void *data, size_t size)
{
	if (m_net->crashed[m_ip]) return false;
	if (peer < 0 || (size_t)peer >= m_conns.size() || m_conns[peer] < 0) return false;
	int idx = m_conns[peer];
	SimNetwork::Connection &c = m_net->conns[idx];
	if (!c.open) return false;
	int me = c.end[0] == this ? 0 : 1;
	std::vector<uint8_t> v((const uint8_t *)data, (const uint8_t *)data + size);
	m_net->deliver(idx, 1 - me, TransportEvent::RECEIVED, channel, v);
	m_stats.bytesSent += size;
	m_stats.packetsSent++;
	return true;
}

bool SimTransport::poll(TransportEvent &event)
{
	if (m_net->crashed[m_ip]) return false;
	if (!m_net->next(this, event)) return false;
	if (event.type == TransportEvent::RECEIVED) {
		m_stats.bytesReceived += event.data.size();
		m_stats.packetsReceived++;
	}
	if (event.type == TransportEvent::DISCONNECTED && event.peer >= 0 && (size_t)event.peer < m_conns.size())
		m_conns[event.peer] = -1;
	return true;
}

PeerStats SimTransport::peerStats(PeerId peer) const
{
	PeerStats s;
	if (peer >= 0 && (size_t)peer < m_conns.size() && m_conns[peer] >= 0) {
		const SimNetwork::Connection &c = m_net->conns[m_conns[peer]];
		s.roundTripMs = (uint32_t)c.srtt;
		s.roundTripVarianceMs = (uint32_t)c.rttvar;
		s.packetsLost = c.lost;
	}
	return s;
}

// =================================================================================================
// A player: the session plus a stand-in for the game
// =================================================================================================
struct Player
{
	int slot;
	std::unique_ptr<Transport> transport;
	std::unique_ptr<Session> session;
	Frame nextFrame;
	uint64_t hash;
	std::vector<uint64_t> hashAt;			// hash after each frame
	std::map<uint32_t, Frame> issuedAtFrame;	// seq -> frame at which the command was issued
	std::vector<double> inputDelayMs;		// command issue -> execution
	std::map<uint32_t, double> issuedAtMs;
	std::map<std::pair<int, uint32_t>, int> executed;	// (slot, seq) -> times executed
	double frozenMs;
	double starvedMs;		// frozen without the turn (vs. turn there but pacing held back)
	double longestFreezeMs;
	double freezeStart;
	bool left;
	bool crashed;
	bool desyncSeen;
	Frame desyncFrame;
	double updateIntervalMs;	// how often the game loop runs (a slow computer runs it less often)
	double nextUpdate;
	bool corrupt;				// simulate a desync: this player's simulation differs
	std::vector<std::pair<Frame, int> > leftEvents;	// (frame, slot)
	uint32_t seqCounter;
	int newHost;			// from HOST_CHANGED (-1 = none)
	bool hostLost;

	Player() : newHost(-1), hostLost(false), slot(0), nextFrame(1), hash(1469598103934665603ull), frozenMs(0), starvedMs(0), longestFreezeMs(0), freezeStart(-1),
		left(false), crashed(false), desyncSeen(false), desyncFrame(0), updateIntervalMs(5), nextUpdate(0),
		corrupt(false), seqCounter(0) {}
};

struct GameResult
{
	double avgInputDelayMs;
	double maxInputDelayMs;
	double frozenPercentWorst;
	double longestFreezeMs;
	bool identical;
	bool exactlyOnce;
	int framesPlayed;
};

static uint64_t Mix(uint64_t h, uint64_t v) { h ^= v; h *= 1099511628211ull; return h; }

struct GameSetup
{
	int players;
	LinkProfile links[MAX_PLAYERS];		// per player (slot 0 = host)
	double seconds;
	double commandsPerSecond;			// per player
	int leaveSlot; double leaveAtMs;	// a player quits (-1 = nobody)
	int crashSlot; double crashAtMs;	// a player's computer dies
	int slowSlot; double slowUpdateMs;	// a slow computer: game loop every slowUpdateMs
	int corruptSlot; Frame corruptAtFrame;	// a desync
	int hangSlot; double hangAtMs;		// a player's game hangs; its connection stays up
	uint32_t dropAfterMs;				// SessionConfig::dropAfterMs
	uint64_t seed;

	GameSetup() : players(3), seconds(60), commandsPerSecond(2), leaveSlot(-1), leaveAtMs(0), crashSlot(-1), crashAtMs(0),
		slowSlot(-1), slowUpdateMs(0), corruptSlot(-1), corruptAtFrame(0), hangSlot(-1), hangAtMs(0), dropAfterMs(0), seed(1)
	{
		for (int i = 0; i < MAX_PLAYERS; ++i) links[i] = WIRED;
	}
};

static GameResult RunSimulatedGame(const GameSetup &setup, std::vector<Player> *outPlayers = NULL)
{
	SimNetwork net(setup.seed);
	Rng rng(setup.seed + 99);
	SessionConfig base;
	base.hostSlot = 0;
	base.sessionId = 0x1234;
	base.exeCRC = 0xabcd;
	base.iniCRC = 0xef01;
	base.firstFrame = 1;
	base.dropAfterMs = setup.dropAfterMs;
	for (int i = 0; i < setup.players; ++i) {
		base.players[i].human = true;
		base.players[i].ip = 0x0a000001 + i;
		base.players[i].port = 8088;
		net.linkOf[base.players[i].ip] = setup.links[i];
	}

	std::vector<Player> players(setup.players);
	for (int i = 0; i < setup.players; ++i) {
		Player &p = players[i];
		p.slot = i;
		p.transport.reset(new SimTransport(&net, base.players[i].ip));
		SessionConfig cfg = base;
		cfg.localSlot = i;
		p.session.reset(new Session(p.transport.get(), cfg));
		if (i == setup.slowSlot) p.updateIntervalMs = setup.slowUpdateMs;
		if (i == setup.corruptSlot) p.corrupt = true;
	}
	players[0].session->start(0);	// the host first, so clients can connect
	for (int i = 1; i < setup.players; ++i) players[i].session->start(0);

	const double end = setup.seconds * 1000.0;
	const double step = 1.0;
	bool loadedSent = false;
	for (net.now = 0; net.now < end; net.now += step) {
		uint32_t ms = (uint32_t)net.now;
		for (int i = 0; i < setup.players; ++i) {
			Player &p = players[i];
			if (p.left || p.crashed) continue;
			p.session->update(ms);

			Event e;
			while (p.session->pollEvent(e)) {
				if (e.type == Event::HOST_CHANGED) p.newHost = e.slot;
				if (e.type == Event::HOST_LOST) p.hostLost = true;
			}

			// load instantly once connected (after 200 ms)
			if (!loadedSent && net.now >= 200) {
				for (int j = 0; j < setup.players; ++j) players[j].session->sendLoadComplete();
				loadedSent = true;
			}

			if (net.now < p.nextUpdate) continue;
			if (i == setup.hangSlot && net.now >= setup.hangAtMs) continue;	// hung: only the network runs
			p.nextUpdate = net.now + p.updateIntervalMs;

			// player input
			if (p.session->gameStarted() && rng.uniform() < setup.commandsPerSecond * p.updateIntervalMs / 1000.0) {
				uint32_t seq = ++p.seqCounter;
				std::vector<uint8_t> cmd(8);
				memcpy(&cmd[0], &seq, 4);
				uint32_t salt = rng.next();
				memcpy(&cmd[4], &salt, 4);
				p.issuedAtMs[seq] = net.now;
				p.session->submitCommand(cmd);
			}

			// one logic frame per game update at most, like the engine
			bool ran = false;
			if (p.session->readyForFrame(p.nextFrame, ms)) {
				Turn t;
				if (p.session->takeTurn(p.nextFrame, t)) {
					p.hash = Mix(p.hash, t.frame);
					for (size_t c = 0; c < t.events.size(); ++c) {
						p.hash = Mix(p.hash, t.events[c].type * 256 + t.events[c].slot);
						if (t.events[c].type == EVENT_DESYNC && !p.desyncSeen) { p.desyncSeen = true; p.desyncFrame = t.frame; }
						if (t.events[c].type == EVENT_PLAYER_LEFT) p.leftEvents.push_back(std::make_pair(t.frame, (int)t.events[c].slot));
					}
					for (size_t c = 0; c < t.commands.size(); ++c) {
						const Command &cmd = t.commands[c];
						p.hash = Mix(p.hash, cmd.slot);
						for (size_t b = 0; b < cmd.data.size(); ++b) p.hash = Mix(p.hash, cmd.data[b]);
						p.executed[std::make_pair((int)cmd.slot, cmd.seq)]++;
						if (cmd.slot == p.slot && p.issuedAtMs.count(cmd.seq))
							p.inputDelayMs.push_back(net.now - p.issuedAtMs[cmd.seq]);
					}
					uint64_t state = p.hash;
					if (p.corrupt && t.frame >= setup.corruptAtFrame) state ^= 0x5555;
					p.hashAt.push_back(p.hash);
					if (t.frame % 100 == 0) p.session->reportCRC(t.frame, (uint32_t)(state ^ (state >> 32)));
					++p.nextFrame;
					ran = true;
				}
			}

			// frozen = the game had started and this player could not advance for longer than a frame
			if (p.session->gameStarted()) {
				if (ran) {
					if (p.freezeStart >= 0) {
						double f = net.now - p.freezeStart;
						if (f > 50) { p.frozenMs += f; p.longestFreezeMs = std::max(p.longestFreezeMs, f); }
						p.freezeStart = -1;
					}
				} else if (p.freezeStart < 0) {
					p.freezeStart = net.now;
				}
				if (!ran && !p.session->hasTurn(p.nextFrame)) p.starvedMs += p.updateIntervalMs;
				if (getenv("LOCKSTEP_TRACE") && !ran && p.freezeStart >= 0 && net.now - p.freezeStart == 500 && p.slot == 1)
					printf("      t=%.0f player 1 starved at frame %u: %d later turns already here, host at frame %u\n", net.now,
						p.nextFrame, p.session->stats().bufferedTurns, players[0].nextFrame);
			}
		}

		if (setup.leaveSlot >= 0 && !players[setup.leaveSlot].left && net.now >= setup.leaveAtMs) {
			players[setup.leaveSlot].session->leave();
			players[setup.leaveSlot].left = true;
		}
		if (setup.crashSlot >= 0 && !players[setup.crashSlot].crashed && net.now >= setup.crashAtMs) {
			players[setup.crashSlot].crashed = true;
			net.crash(base.players[setup.crashSlot].ip, 3000);
		}
	}

	GameResult r;
	// Compare every pair over the frames both executed.
	r.identical = true;
	size_t common = (size_t)-1;
	for (int i = 0; i < setup.players; ++i) {
		if (players[i].left || players[i].crashed) continue;
		common = std::min(common, players[i].hashAt.size());
	}
	for (int i = 0; i < setup.players; ++i)
		for (int j = 0; j < setup.players; ++j) {
			size_t n = std::min(players[i].hashAt.size(), players[j].hashAt.size());
			for (size_t k = 0; k < n; ++k)
				if (players[i].hashAt[k] != players[j].hashAt[k]) { r.identical = false; break; }
		}
	r.framesPlayed = common == (size_t)-1 ? 0 : (int)common;
	if (setup.hangSlot >= 0) {
		// the hung player is not part of the comparison of who kept playing
		common = (size_t)-1;
		for (int i = 0; i < setup.players; ++i)
			if (i != setup.hangSlot && !players[i].left && !players[i].crashed) common = std::min(common, players[i].hashAt.size());
		r.framesPlayed = (int)common;
	}

	// Every command of a player who stayed was executed exactly once (except the last ones still in
	// flight when the simulation stopped).
	r.exactlyOnce = true;
	for (int i = 0; i < setup.players; ++i) {
		const Player &obs = players[i];
		if (obs.left || obs.crashed) continue;
		for (std::map<std::pair<int, uint32_t>, int>::const_iterator it = obs.executed.begin(); it != obs.executed.end(); ++it)
			if (it->second != 1) r.exactlyOnce = false;
		if (i == setup.hangSlot) continue;
		for (int s = 0; s < setup.players; ++s) {
			if (players[s].left || players[s].crashed || s == setup.hangSlot) continue;
			for (std::map<uint32_t, double>::const_iterator it = players[s].issuedAtMs.begin(); it != players[s].issuedAtMs.end(); ++it)
				if (it->second < end - 2000 && !obs.executed.count(std::make_pair(s, it->first))) r.exactlyOnce = false;
		}
	}

	double sum = 0, mx = 0;
	int n = 0;
	r.frozenPercentWorst = 0;
	r.longestFreezeMs = 0;
	for (int i = 0; i < setup.players; ++i) {
		const Player &p = players[i];
		for (size_t k = 0; k < p.inputDelayMs.size(); ++k) { sum += p.inputDelayMs[k]; mx = std::max(mx, p.inputDelayMs[k]); ++n; }
		if (p.left || p.crashed || i == setup.hangSlot) continue;
		r.frozenPercentWorst = std::max(r.frozenPercentWorst, 100.0 * p.frozenMs / end);
		r.longestFreezeMs = std::max(r.longestFreezeMs, p.longestFreezeMs);
	}
	r.avgInputDelayMs = n ? sum / n : 0;
	r.maxInputDelayMs = mx;
	// Close the sessions while the simulated network still exists.
	for (int i = 0; i < setup.players; ++i) players[i].session->shutdown();
	if (outPlayers) outPlayers->swap(players);
	return r;
}

// =================================================================================================
// Tests
// =================================================================================================
static void TestWire()
{
	printf("Wire format\n");
	ByteWriter w;
	w.u8(200); w.u16(65000); w.u32(0xdeadbeef);
	w.str("hello"); std::vector<uint16_t> ws(3, 0x263a); w.wstr(ws);
	std::vector<uint8_t> blob(5, 7); w.blob(blob);
	ByteReader r(w.data());
	bool ok = r.u8() == 200 && r.u16() == 65000 && r.u32() == 0xdeadbeef && r.str() == "hello" && r.wstr() == ws && r.blob() == blob;
	CHECK(ok && r.ok() && r.atEnd(), "round trip of every field type");
	ByteReader shortR(&w.data()[0], 3);
	shortR.u32();
	CHECK(!shortR.ok(), "reading past the end fails instead of returning garbage");
}

static void TestLinks()
{
	printf("Simulated games, 3 players (wired host + two laptops), 10 minutes each\n");
	printf("  %-34s | %9s %9s | %8s %10s | %s\n", "laptop link", "avg delay", "max delay", "frozen", "longest", "identical");
	const LinkProfile *profiles[] = { &WIRED, &GOOD_WIFI, &BUSY_WIFI, &BAD_WIFI, &DROPOUT_WIFI };
	for (int k = 0; k < 5; ++k) {
		GameSetup s;
		s.players = 3;
		s.seconds = 600;
		s.links[1] = *profiles[k];
		s.links[2] = *profiles[k];
		s.seed = 10 + k;
		std::vector<Player> pl;
		GameResult r = RunSimulatedGame(s, &pl);
		printf("  %-34s | %6.0f ms %6.0f ms | %7.2f%% %7.0f ms | %s\n", profiles[k]->name, r.avgInputDelayMs, r.maxInputDelayMs,
			r.frozenPercentWorst, r.longestFreezeMs, r.identical ? "yes" : "NO");
		if (getenv("LOCKSTEP_DETAIL"))
			for (size_t i = 0; i < pl.size(); ++i)
				printf("      player %d: frozen %.2f%% (no turn %.2f%%), longest %.0f ms, %d frames, final input delay %d\n", (int)i,
					100.0 * pl[i].frozenMs / (s.seconds * 1000), 100.0 * pl[i].starvedMs / (s.seconds * 1000), pl[i].longestFreezeMs,
					(int)pl[i].hashAt.size(), pl[i].session->inputDelay());
		CHECK(r.identical && r.exactlyOnce, "%s: same commands on the same frames everywhere, each executed once", profiles[k]->name);
		CHECK(r.framesPlayed > 600 * 30 * 0.97, "%s: game ran at full speed (%d of %d frames)", profiles[k]->name, r.framesPlayed, 600 * 30);
		if (k == 0) {
			CHECK(r.avgInputDelayMs < 110, "wired: input delay %.0f ms (the old code: 333 ms)", r.avgInputDelayMs);
			CHECK(r.frozenPercentWorst < 0.1, "wired: never frozen");
		} else if (k == 1) {
			CHECK(r.frozenPercentWorst < 0.5, "good Wi-Fi: frozen %.2f%% of the time (the old code: 11.7%%)", r.frozenPercentWorst);
		}
	}
}

static void TestEightPlayers()
{
	printf("8 players, mixed links, 5 minutes\n");
	GameSetup s;
	s.players = 8;
	s.seconds = 300;
	s.links[1] = GOOD_WIFI; s.links[2] = GOOD_WIFI; s.links[3] = BUSY_WIFI; s.links[5] = GOOD_WIFI; s.links[7] = BUSY_WIFI;
	s.commandsPerSecond = 3;
	s.seed = 21;
	GameResult r = RunSimulatedGame(s);
	CHECK(r.identical && r.exactlyOnce, "8 players stay identical, every command once");
	printf("       avg input delay %.0f ms, worst frozen %.2f%%\n", r.avgInputDelayMs, r.frozenPercentWorst);
}

static void TestLeave()
{
	printf("A player quits after 20 s\n");
	GameSetup s;
	s.players = 3;
	s.seconds = 40;
	s.links[1] = GOOD_WIFI; s.links[2] = GOOD_WIFI;
	s.leaveSlot = 2;
	s.leaveAtMs = 20000;
	s.seed = 31;
	std::vector<Player> players;
	GameResult r = RunSimulatedGame(s, &players);
	CHECK(r.identical && r.exactlyOnce, "remaining players identical");
	bool sameFrame = !players[0].leftEvents.empty() && players[0].leftEvents == players[1].leftEvents &&
		players[0].leftEvents[0].second == 2;
	CHECK(sameFrame, "everyone saw 'player 2 left' on the same frame (%u)", players[0].leftEvents.empty() ? 0 : players[0].leftEvents[0].first);
	CHECK(r.framesPlayed > 40 * 30 * 0.95, "the game went on at full speed (%d frames)", r.framesPlayed);
	CHECK(!players[0].session->inGame(2) && players[0].session->inGame(1), "membership updated");
}

static void TestCrash()
{
	printf("A player's computer dies after 20 s (connection timeout 3 s)\n");
	GameSetup s;
	s.players = 3;
	s.seconds = 40;
	s.crashSlot = 1;
	s.crashAtMs = 20000;
	s.seed = 41;
	std::vector<Player> players;
	GameResult r = RunSimulatedGame(s, &players);
	CHECK(r.identical, "remaining players identical");
	CHECK(!players[0].leftEvents.empty() && players[0].leftEvents == players[2].leftEvents, "both saw it leave on the same frame");
	printf("       longest freeze %.0f ms\n", r.longestFreezeMs);
	CHECK(r.longestFreezeMs < 3500, "the game waited at most until the connection timed out");
	CHECK(r.framesPlayed > 40 * 30 * 0.85, "then continued (%d frames)", r.framesPlayed);
}

static void TestHostChange(bool crash)
{
	printf(crash ? "The host's computer dies after 20 s (4 players, two on Wi-Fi)\n"
	             : "The host quits after 20 s (4 players, two on Wi-Fi)\n");
	GameSetup s;
	s.players = 4;
	s.seconds = 50;
	s.links[2] = GOOD_WIFI;
	s.links[3] = BUSY_WIFI;
	s.commandsPerSecond = 4;
	if (crash) { s.crashSlot = 0; s.crashAtMs = 20000; }
	else { s.leaveSlot = 0; s.leaveAtMs = 20000; }
	s.seed = crash ? 71 : 72;
	std::vector<Player> players;
	GameResult r = RunSimulatedGame(s, &players);
	CHECK(r.identical && r.exactlyOnce, "remaining players identical, every command executed exactly once");
	bool newHost = players[1].newHost == 1 && players[2].newHost == 1 && players[3].newHost == 1;
	CHECK(newHost, "everyone moved to the same new host (slot 1)");
	bool lostNone = !players[1].hostLost && !players[2].hostLost && !players[3].hostLost;
	CHECK(lostNone, "nobody lost the game");
	bool same = !players[1].leftEvents.empty() && players[1].leftEvents == players[2].leftEvents &&
		players[2].leftEvents == players[3].leftEvents && players[1].leftEvents[0].second == 0;
	CHECK(same, "everyone saw the old host leave on the same frame (%u)", players[1].leftEvents.empty() ? 0 : players[1].leftEvents[0].first);
	if (!crash) {
		// The host's commands issued before it left reached everyone (e.g. its self-destruct).
		bool all = true;
		for (std::map<uint32_t, double>::const_iterator it = players[0].issuedAtMs.begin(); it != players[0].issuedAtMs.end(); ++it)
			if (!players[1].executed.count(std::make_pair(0, it->first))) all = false;
		CHECK(all, "every command the host issued before quitting was executed by the others");
	}
	printf("       frames played %d, longest freeze %.0f ms\n", r.framesPlayed, r.longestFreezeMs);
	if (getenv("LOCKSTEP_DETAIL"))
		for (int i = 0; i < 4; ++i) {
			SessionStats st = players[i].session->stats();
			printf("      player %d: next frame %u, buffered %d, host? %d\n", i, players[i].nextFrame, st.bufferedTurns,
				players[i].session->isHost());
		}
	CHECK(r.framesPlayed > 50 * 30 * 0.85, "the game went on (%d frames)", r.framesPlayed);
}

static void TestHang()
{
	printf("A player's game hangs after 20 s, its connection stays up (drop after 10 s)\n");
	GameSetup s;
	s.players = 3;
	s.seconds = 50;
	s.hangSlot = 2;
	s.hangAtMs = 20000;
	s.dropAfterMs = 10000;
	s.seed = 81;
	std::vector<Player> players;
	GameResult r = RunSimulatedGame(s, &players);
	CHECK(r.identical, "the others stay identical");
	bool dropped = !players[0].leftEvents.empty() && players[0].leftEvents == players[1].leftEvents && players[0].leftEvents[0].second == 2;
	CHECK(dropped, "it was dropped, on the same frame for both");
	printf("       longest freeze %.0f ms, frames played %d\n", r.longestFreezeMs, r.framesPlayed);
	CHECK(r.longestFreezeMs > 9000 && r.longestFreezeMs < 13000, "the game waited about the drop time, then went on");
	CHECK(r.framesPlayed > 50 * 30 * 0.7, "and continued (%d frames)", r.framesPlayed);
}

static void TestSlowComputer()
{
	printf("One computer runs its game loop only 25 times per second (needs 30)\n");
	GameSetup s;
	s.players = 3;
	s.seconds = 60;
	s.slowSlot = 2;
	s.slowUpdateMs = 40;
	s.seed = 51;
	std::vector<Player> players;
	GameResult r = RunSimulatedGame(s, &players);
	CHECK(r.identical && r.exactlyOnce, "still identical");
	int slowFrames = (int)players[2].hashAt.size(), hostFrames = (int)players[0].hashAt.size();
	printf("       host ran %d frames, slow computer %d\n", hostFrames, slowFrames);
	CHECK(hostFrames - slowFrames < 60, "the host slowed down to the slow computer instead of leaving it behind");
}

static void TestDesync()
{
	printf("Player 1's simulation diverges at frame 1000\n");
	GameSetup s;
	s.players = 3;
	s.seconds = 60;
	s.corruptSlot = 1;
	s.corruptAtFrame = 1000;
	s.seed = 61;
	std::vector<Player> players;
	RunSimulatedGame(s, &players);
	bool all = players[0].desyncSeen && players[1].desyncSeen && players[2].desyncSeen;
	CHECK(all, "every player was told about the mismatch");
	CHECK(all && players[0].desyncFrame == players[1].desyncFrame && players[1].desyncFrame == players[2].desyncFrame,
		"on the same frame (%u)", players[0].desyncFrame);
	CHECK(players[0].desyncFrame >= 1000 && players[0].desyncFrame < 1200, "soon after the first CRC that differed");
}

static void TestReject()
{
	printf("A player with a different executable is refused\n");
	SimNetwork net(7);
	SessionConfig base;
	base.hostSlot = 0;
	base.sessionId = 5;
	base.exeCRC = 1;
	for (int i = 0; i < 2; ++i) { base.players[i].human = true; base.players[i].ip = 0x0a000001 + i; base.players[i].port = 8088; }
	SimTransport t0(&net, base.players[0].ip), t1(&net, base.players[1].ip);
	SessionConfig c0 = base; c0.localSlot = 0;
	SessionConfig c1 = base; c1.localSlot = 1; c1.exeCRC = 2;
	Session host(&t0, c0), client(&t1, c1);
	host.start(0);
	client.start(0);
	bool rejected = false;
	for (net.now = 0; net.now < 500; net.now += 1) {
		host.update((uint32_t)net.now);
		client.update((uint32_t)net.now);
		Event e;
		while (client.pollEvent(e)) if (e.type == Event::CONNECT_FAILED && e.value == REJECT_EXECUTABLE) rejected = true;
		while (host.pollEvent(e)) {}
	}
	CHECK(rejected, "client told: REJECT_EXECUTABLE");
}

static void TestChatAndFiles()
{
	printf("Chat and file transfer through the host\n");
	SimNetwork net(8);
	SessionConfig base;
	base.hostSlot = 0;
	for (int i = 0; i < 3; ++i) { base.players[i].human = true; base.players[i].ip = 0x0a000001 + i; base.players[i].port = 8088; }
	SimTransport t0(&net, base.players[0].ip), t1(&net, base.players[1].ip), t2(&net, base.players[2].ip);
	SessionConfig c[3];
	for (int i = 0; i < 3; ++i) { c[i] = base; c[i].localSlot = i; }
	Session s0(&t0, c[0]), s1(&t1, c[1]), s2(&t2, c[2]);
	Session *s[3] = { &s0, &s1, &s2 };
	for (int i = 0; i < 3; ++i) s[i]->start(0);
	std::vector<Event> got[3];
	std::vector<uint16_t> text(5, 'h');
	std::vector<uint8_t> chunk(30000, 9);
	for (net.now = 0; net.now < 1000; net.now += 1) {
		if (net.now == 100) {
			s1.sendChat((1 << 2), text);			// 1 -> 2 only
			s0.sendChat((1 << 1) | (1 << 2), text);	// host -> both
			s2.announceFile(77, "maps/test.map", 60000, (1 << 0) | (1 << 1));
			s0.announceFile(78, "maps/other.map", 10, (1 << 1));	// player 2 is not a recipient...
			s2.sendFileData(77, 0, chunk);
			s2.sendFileData(77, 30000, chunk);
		}
		for (int i = 0; i < 3; ++i) {
			s[i]->update((uint32_t)net.now);
			Event e;
			while (s[i]->pollEvent(e)) got[i].push_back(e);
		}
		for (int i = 0; i < 3; ++i) {
			for (size_t k = 0; k < got[i].size(); ++k)
				if (got[i][k].type == Event::FILE_DATA && got[i][k].value == 30000) {
					s[i]->sendFileProgress(2, 77, 100);
					got[i][k].value = -1;
				}
		}
	}
	int chats[3] = { 0, 0, 0 }, data[3] = { 0, 0, 0 }, announce[3] = { 0, 0, 0 }, otherAnnounce[3] = { 0, 0, 0 }, progress = 0;
	for (int i = 0; i < 3; ++i)
		for (size_t k = 0; k < got[i].size(); ++k) {
			const Event &e = got[i][k];
			if (e.type == Event::CHAT) ++chats[i];
			if (e.type == Event::FILE_DATA) ++data[i];
			if (e.type == Event::FILE_ANNOUNCE && e.name == "maps/test.map") ++announce[i];
			if (e.type == Event::FILE_ANNOUNCE && e.name == "maps/other.map" && e.mask == (1 << 1)) ++otherAnnounce[i];
			if (e.type == Event::FILE_PROGRESS && e.value == 100) ++progress;
		}
	CHECK(chats[0] == 0 && chats[1] == 1 && chats[2] == 2, "chat reaches exactly the addressed players");
	CHECK(announce[0] == 1 && announce[1] == 1 && announce[2] == 0, "everyone else hears about the file");
	CHECK(data[0] == 2 && data[1] == 2 && data[2] == 0, "file data reaches the recipients");
	CHECK(otherAnnounce[1] == 1 && otherAnnounce[2] == 1, "...but still hears about it, with the recipient mask");
	CHECK(progress == 4, "each recipient's progress reaches the other two players");
}

static void TestRealEnet()
{
	printf("Real ENet on 127.0.0.1: 3 players, 5 seconds of real time\n");
	SessionConfig base;
	base.hostSlot = 0;
	base.sessionId = 77;
	for (int i = 0; i < 3; ++i) { base.players[i].human = true; base.players[i].ip = 0x7f000001; base.players[i].port = (uint16_t)(28088 + i); }
	EnetTransport t[3];
	std::unique_ptr<Session> s[3];
	for (int i = 0; i < 3; ++i) {
		SessionConfig c = base;
		c.localSlot = i;
		s[i].reset(new Session(&t[i], c));
		if (!s[i]->start(RealMs())) { CHECK(false, "transport %d opened", i); return; }
	}
	Frame next[3] = { 1, 1, 1 };
	uint64_t hash[3] = { 1, 1, 1 };
	std::vector<uint64_t> hashes[3];
	bool loaded = false, connected[3] = { true, false, false };
	uint32_t startMs = RealMs();
	Rng rng(5);
	int commands = 0;
	while (RealMs() - startMs < 5000) {
		uint32_t now = RealMs();
		for (int i = 0; i < 3; ++i) {
			s[i]->update(now);
			Event e;
			while (s[i]->pollEvent(e)) if (e.type == Event::CONNECTED) connected[i] = true;
		}
		if (!loaded && connected[1] && connected[2]) {
			for (int i = 0; i < 3; ++i) s[i]->sendLoadComplete();
			loaded = true;
		}
		for (int i = 0; i < 3; ++i) {
			if (s[i]->gameStarted() && rng.uniform() < 0.01) {
				std::vector<uint8_t> cmd(4, (uint8_t)i);
				s[i]->submitCommand(cmd);
				++commands;
			}
			if (s[i]->readyForFrame(next[i], now)) {
				Turn turn;
				if (s[i]->takeTurn(next[i], turn)) {
					for (size_t c = 0; c < turn.commands.size(); ++c) hash[i] = Mix(Mix(hash[i], turn.commands[c].slot), turn.commands[c].seq);
					hashes[i].push_back(hash[i]);
					++next[i];
				}
			}
		}
		SleepMs(1);
	}
	size_t n = std::min(hashes[0].size(), std::min(hashes[1].size(), hashes[2].size()));
	bool same = n > 0;
	for (size_t k = 0; k < n; ++k) same = same && hashes[0][k] == hashes[1][k] && hashes[1][k] == hashes[2][k];
	CHECK(connected[1] && connected[2], "clients connected and were accepted");
	CHECK(n > 100, "played %u frames in 5 s", (unsigned)n);
	CHECK(same, "identical on all three (%d commands)", commands);
	SessionStats st = s[0]->stats();
	printf("       input delay %d frames, round trip to client 1: %u ms\n", st.inputDelay, st.roundTripMs[1]);
	for (int i = 0; i < 3; ++i) s[i]->shutdown();
}

int main()
{
	TestWire();
	TestReject();
	TestChatAndFiles();
	TestLinks();
	TestEightPlayers();
	TestLeave();
	TestCrash();
	TestHostChange(false);
	TestHostChange(true);
	TestHang();
	TestSlowComputer();
	TestDesync();
	TestRealEnet();
	printf("%s\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED");
	return g_failures ? 1 : 0;
}
