/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Session.h - lockstep game session for LAN play: the host sequences turns.
//
// Model
//   The game is deterministic: every machine runs the same simulation, so only player commands
//   travel.  One machine, the host, decides what happens on which frame:
//
//   * Clients send their commands to the host as soon as they are issued.
//   * When the host executes frame n it releases the TURN for frame n + D: every command that
//     reached it since the previous turn, plus frame-synchronized events (player left, desync).
//     D (the input delay, a few frames) absorbs network jitter.
//   * Every machine executes frame n only with turn n, so all apply the same commands on the same
//     frame.  A client that falls behind (a Wi-Fi hiccup) catches up by running frames faster;
//     the others keep playing.  Only a client far behind makes the host slow down and, beyond a
//     limit, wait (the disconnect screen).
//   * Machines report state CRCs to the host, which compares them and announces a mismatch in a
//     turn, so everyone reacts on the same frame.
//   * When the host leaves or its connection is lost, the remaining player with the lowest slot
//     takes over: the others reconnect to it and report the last turn they have; it collects any
//     turns it lacks, fills in everyone's gaps, announces that the old host left and goes on.
//
// The class knows nothing about the game: commands are opaque bytes.  LockstepNetwork connects it
// to the engine.  Time comes from the caller (milliseconds), which makes it testable.

#ifndef ZH_LOCKSTEP_SESSION_H
#define ZH_LOCKSTEP_SESSION_H

#include "GameNetwork/Lockstep/Transport.h"
#include "GameNetwork/Lockstep/Wire.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Lockstep {

typedef uint32_t Frame;

enum { MAX_PLAYERS = 8 };

// Timing policy.  Frames are logic frames (30 per second).
enum
{
	DEFAULT_FRAME_RATE = 30,
	MIN_INPUT_DELAY = 2,			// frames between the host receiving a command and executing it
	MAX_INPUT_DELAY = 12,
	INPUT_DELAY_UPDATE_MS = 500,	// how often the host re-evaluates the input delay
	INPUT_DELAY_DECREASE_MS = 2000,	// lower it by one frame at most this often...
	INPUT_DELAY_CALM_MS = 10000,	// ...and only when no client ran out of turns for this long
	CUSHION_BUCKET_MS = 500,		// clients report their smallest cushion over the last 0.5-1 s
	STATUS_MIN_INTERVAL_MS = 100,	// clients report at least this often, even when not advancing
	MIGRATION_TIMEOUT_MS = 5000,	// a new host waits this long for the other players to reconnect
	CONNECT_RETRY_MS = 500,			// before the game starts, a client keeps trying to reach the host...
	CONNECT_GIVE_UP_MS = 60000,		// ...for this long (the host may start listening a little later)
	LAG_SLOW_FRAMES = 4,			// a client this far behind (beyond the input delay) slows the host by 20%
	LAG_SLOWER_FRAMES = 12,			// ... this far: 50%
	LAG_STOP_FRAMES = 30,			// ... this far: the host waits for it
	WAITING_REPORT_MS = 1000,		// waiting this long is shown to the players (disconnect screen)
	WAITING_BROADCAST_MS = 500,
	STATUS_INTERVAL_FRAMES = 1,		// clients report their frame this often
	TURN_HISTORY = 900,				// turns kept after execution (30 s)
	REDUNDANT_TURNS = 3,			// turns repeated in each unreliable bundle
	REDUNDANT_BUNDLE_BYTES = 1100,	// keep a bundle within one UDP packet
	COMMAND_RESEND_MS = 15			// clients repeat unacknowledged commands this often
};

struct PlayerAddress
{
	bool human;			// a networked player (AI and empty slots are not)
	uint32_t ip;		// IPv4, host byte order
	uint16_t port;

	PlayerAddress() : human(false), ip(0), port(0) {}
};

struct SessionConfig
{
	int localSlot;
	int hostSlot;
	PlayerAddress players[MAX_PLAYERS];
	uint32_t sessionId;		// identifies the game; clients of another game are refused
	uint32_t exeCRC;		// players with a different executable or data are refused (desync)
	uint32_t iniCRC;
	Frame firstFrame;		// first frame that needs a turn
	int frameRate;
	uint32_t dropAfterMs;	// host: drop a player the game has waited for this long (0 = never)

	SessionConfig() : localSlot(0), hostSlot(0), sessionId(0), exeCRC(0), iniCRC(0), firstFrame(1),
		frameRate(DEFAULT_FRAME_RATE), dropAfterMs(0) {}
};

struct Command
{
	uint8_t slot;
	uint32_t seq;					// per-player sequence number
	std::vector<uint8_t> data;
};

struct TurnEvent
{
	uint8_t type;		// TurnEventType
	uint8_t slot;
	uint8_t reason;		// LeaveReason
	uint32_t frame;		// EVENT_DESYNC: the frame whose CRCs differed
};

struct Turn
{
	Frame frame;
	uint8_t inputDelay;		// the host's input delay when it released this turn
	std::vector<TurnEvent> events;
	std::vector<Command> commands;
};

// Notifications that are not tied to a frame.
struct Event
{
	enum Type
	{
		CONNECTED,			// client: the host accepted us
		CONNECT_FAILED,		// client: could not reach the host, or refused (value = RejectReason or 0)
		PLAYER_JOINED,		// host: slot connected
		LOAD_PROGRESS,		// slot, value = percent
		LOAD_COMPLETE,		// slot
		GAME_START,			// everyone has loaded
		CHAT,				// slot = sender, mask, text
		WAITING,			// waitingMs[] = how long the game has waited for each slot (0 = not waiting)
		HOST_LOST,			// client: the host is gone and nobody can take over (the game is over)
		HOST_CHANGED,		// slot = the new host (it may be this machine)
		PLAYER_LOST,		// a player's connection dropped (the PLAYER_LEFT turn event follows)
		FILE_ANNOUNCE,		// slot = sender, id, name, value = total size, mask = recipients
		FILE_DATA,			// slot = sender, id, value = offset, data
		FILE_PROGRESS		// slot = the player reporting, id, value = percent received
	};

	Type type;
	int slot;
	int value;
	uint32_t id;
	uint32_t mask;
	std::string name;
	std::vector<uint16_t> text;
	std::vector<uint8_t> data;
	uint32_t waitingMs[MAX_PLAYERS];

	Event(Type t = CONNECTED) : type(t), slot(-1), value(0), id(0), mask(0) { memset(waitingMs, 0, sizeof(waitingMs)); }
};

struct SessionStats
{
	int inputDelay;			// frames
	int bufferedTurns;		// turns received but not executed yet
	int maxClientLag;		// host: frames the slowest client is behind
	uint32_t roundTripMs[MAX_PLAYERS];
};

class Session
{
public:
	Session(Transport *transport, const SessionConfig &config);
	~Session();

	/// Opens the transport (on the local player's port) and, for a client, connects to the host.
	bool start(uint32_t nowMs);
	/// Sends and receives; call often (every game update and while loading).
	void update(uint32_t nowMs);
	/// Leaves the game (if in one) and closes the transport.
	void shutdown();

	bool isHost() const { return m_config.localSlot == m_config.hostSlot; }
	int localSlot() const { return m_config.localSlot; }
	/// In the game as of the last executed turn (frame-synchronized: the same on every machine).
	bool inGame(int slot) const;
	bool gameStarted() const { return m_started; }

	// --- loading ---
	void sendLoadProgress(int percent);
	void sendLoadComplete();
	/// Host: start without the players that have not loaded yet (they are removed from the game).
	void forceStart();

	// --- game ---
	void submitCommand(const std::vector<uint8_t> &data);
	/// True when frame `next` may be executed now: its turn is here and it is time.
	bool readyForFrame(Frame next, uint32_t nowMs);
	/// The turn of frame f has arrived.
	bool hasTurn(Frame f) const { return m_turns.find(f) != m_turns.end(); }
	/// Removes and returns the turn of frame f (call when executing it).
	bool takeTurn(Frame f, Turn &turn);
	/// The local game's CRC of frame f.
	void reportCRC(Frame f, uint32_t crc);
	void voteToDrop(int slot);
	void leave();
	/// After leave(): true once our last messages have been delivered (or nobody is left to tell).
	bool leaveDelivered();

	// --- chat and files (bulk channel) ---
	void sendChat(uint32_t mask, const std::vector<uint16_t> &text);
	void announceFile(uint32_t id, const std::string &name, uint32_t totalSize, uint32_t mask);
	void sendFileData(uint32_t id, uint32_t offset, const std::vector<uint8_t> &data);
	/// Tells every player how much of file `id` (sent by `senderSlot`) this machine has received.
	void sendFileProgress(int senderSlot, uint32_t id, int percent);

	bool pollEvent(Event &event);
	SessionStats stats() const;
	int inputDelay() const { return m_inputDelay; }
	int frameRate() const { return m_config.frameRate; }

private:
	struct Remote
	{
		PeerId peer;
		bool helloReceived;
		bool connected;
		bool left;
		bool loaded;
		Frame lastFrame;			// last frame it reported executing
		uint32_t lastStatusMs;
		int cushion;				// smallest number of spare turns it recently had (-1 = ran out)
		uint32_t cushionMs;			// when it reported that
		uint32_t waitingSinceMs;	// host: since when the game waits for it (0 = not waiting)
		bool dropVotes[MAX_PLAYERS];

		Remote() : peer(NO_PEER), helloReceived(false), connected(false), left(false), loaded(false),
			lastFrame(0), lastStatusMs(0), cushion(0), cushionMs(0), waitingSinceMs(0) { memset(dropVotes, 0, sizeof(dropVotes)); }
	};

	/// Connected and not gone, as the session currently knows it (not frame-synchronized).
	bool isPlaying(int slot) const;

	// shared
	void handleMessage(PeerId peer, int channel, const std::vector<uint8_t> &data, uint32_t nowMs);
	void sendTo(int slot, int channel, const ByteWriter &w);
	void sendToHost(int channel, const ByteWriter &w);
	void sendHandshake(const ByteWriter &w);	// to the host, before it accepted us
	void broadcast(int channel, const ByteWriter &w, int exceptSlot = -1);
	void deliverBulk(int fromSlot, uint32_t mask, const ByteWriter &w, const Event &localEvent);
	void storeTurn(const Turn &turn);
	void push(const Event &e) { m_events.push_back(e); }
	int slotOfPeer(PeerId peer) const;

	// host
	void hostHello(PeerId peer, ByteReader &r);
	void hostHandle(int slot, uint32_t type, ByteReader &r, uint32_t nowMs);
	void hostVote(int voter, int target);
	void hostRelease(Frame f);
	void hostCheckStart();
	void hostCheckCRCs();
	void hostPlayerGone(int slot, int reason);
	void hostUpdateInputDelay(uint32_t nowMs);
	void hostUpdateWaiting(uint32_t nowMs);
	int hostClientLag(int slot) const;
	int hostMaxClientLag(int *slowest) const;

	// client
	void clientHandle(uint32_t type, ByteReader &r, uint32_t nowMs);

	// host change
	void startMigration(uint32_t nowMs);
	void becomeHost(uint32_t nowMs);
	void hostResume(PeerId peer, ByteReader &r);
	void hostMigrationUpdate(uint32_t nowMs);
	bool findTurn(Frame f, Turn &turn) const;
	void sendTurn(int slot, const Turn &turn);
	void clientUpdateWaiting(uint32_t nowMs);

	static void encodeTurn(ByteWriter &w, const Turn &t);		// with the MSG_TURN byte
	static void encodeTurnBody(ByteWriter &w, const Turn &t);
	static bool decodeTurn(ByteReader &r, Turn &t);
	void hostAcceptCommand(int slot, uint32_t seq, const std::vector<uint8_t> &data);
	void clientSendCommands(uint32_t nowMs);

	Transport *m_transport;
	SessionConfig m_config;
	Remote m_remote[MAX_PLAYERS];
	PeerId m_hostPeer;
	bool m_welcomed;					// client: the current host accepted us
	uint32_t m_connectDeadlineMs;		// client: stop retrying the first connection then
	uint32_t m_reconnectAtMs;			// client: retry the connection then (0 = no retry pending)
	bool m_isolated;					// host change found nobody: we are the one cut off
	// Messages to players that are not connected yet (sent once they are): the host may announce
	// a file or report loading before every client has finished connecting.
	struct Queued { int channel; std::vector<uint8_t> data; };
	std::vector<Queued> m_queuedToSlot[MAX_PLAYERS];
	std::vector<Queued> m_queuedToHost;
	bool m_inGame[MAX_PLAYERS];			// frame-synchronized membership
	bool m_running;
	bool m_started;
	bool m_left;
	bool m_localLoaded;
	uint32_t m_nowMs;

	// turns
	std::map<Frame, Turn> m_turns;			// received, not executed
	std::deque<Turn> m_history;				// executed (for a host change)
	Frame m_lastTaken;						// last frame executed
	Frame m_lastReceived;					// highest turn received/released
	int m_inputDelay;

	// pacing
	double m_nextFrameMs;
	bool m_waitingShown;
	uint32_t m_starvedSinceMs;

	// commands
	uint32_t m_nextSeq;
	std::vector<Command> m_outgoing;		// client: not sent yet
	std::vector<Command> m_unacked;			// client: sent, not seen in a turn yet
	uint32_t m_lastBundleMs;
	uint32_t m_nextSeqFrom[MAX_PLAYERS];	// host: next command sequence number expected per player
	std::map<uint32_t, std::vector<uint8_t> > m_early[MAX_PLAYERS];	// host: arrived before an earlier one
	std::deque<std::vector<uint8_t> > m_recentTurns;	// host: encoded bodies of the last turns
	std::vector<Command> m_pending;			// host: waiting for the next turn
	std::vector<TurnEvent> m_pendingEvents;	// host

	// CRCs (host): frame -> per slot
	struct CRCSet { uint32_t crc[MAX_PLAYERS]; bool have[MAX_PLAYERS]; };
	std::map<Frame, CRCSet> m_crcs;
	std::vector<std::pair<Frame, uint32_t> > m_unsentCRCs;	// client
	bool m_desyncAnnounced;

	// client status reports
	Frame m_statusFrame;
	Frame m_statusSentFrame;
	uint32_t m_hostWaiting[MAX_PLAYERS];	// client: last WAITING from the host
	uint32_t m_hostWaitingMs;				// client: when it said someone is waited for (0 = nobody)

	// host: file transfers being relayed, (sender, id) -> recipients
	std::map<std::pair<int, uint32_t>, uint32_t> m_fileMasks;

	// host input delay adaptation
	uint32_t m_lastDelayUpdateMs;
	uint32_t m_lastDelayDecreaseMs;
	uint32_t m_lastStarveMs;				// host: when a client last ran out of turns

	// client: cushion = turns beyond the one being executed, the slack left for late packets
	int m_cushionCurrent;
	int m_cushionPrevious;
	uint32_t m_cushionBucketMs;
	uint32_t m_lastStatusSentMs;
	void noteCushion(int cushion);
	uint32_t m_lastWaitingBroadcastMs;

	// host change
	bool m_migrating;					// client: looking for the new host; host: waiting for the players
	uint32_t m_migrationDeadlineMs;
	int m_oldHost;
	bool m_leftSeen[MAX_PLAYERS];		// left according to the turns received so far
	bool m_failedHost[MAX_PLAYERS];		// tried as host and gone
	bool m_resumed[MAX_PLAYERS];		// new host: reconnected
	Frame m_resumeReceived[MAX_PLAYERS];
	std::vector<Command> m_migrationCommands;	// new host: commands received before taking over completed

	std::deque<Event> m_events;
};

} // namespace Lockstep

#endif // ZH_LOCKSTEP_SESSION_H
