/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// LockstepNetwork.cpp - the game's NetworkInterface on top of the lockstep session.
//
// Replaces Network.cpp / ConnectionManager / DisconnectManager (the original frame-info protocol).
// GameNetwork/Lockstep/Session.h describes the model: the host sequences turns, every machine
// executes frame n with turn n.  This file connects the session to the engine:
//
//   * outgoing: network-range GameMessages on TheCommandList are serialized and submitted;
//   * incoming: the turn of the frame about to run is turned back into GameMessages and appended
//     to TheCommandList (player index from the sender's slot), then its events (a player left, a
//     desync) are applied, so every machine applies them on the same frame;
//   * state CRCs (MSG_LOGIC_CRC) are reported to the host, which compares them.  The local CRC
//     message stays on TheCommandList for the replay recorder; GameLogic no longer compares CRCs
//     itself in network games (it assumed every player's CRC arrives on the same frame);
//   * loading progress, chat, map transfer and the disconnect screen use the session's events.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AudioEventRTS.h"
#include "Common/CRCDebug.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "GameClient/Diplomacy.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/Lockstep/EnetTransport.h"
#include "GameNetwork/Lockstep/Session.h"

#include <map>
#include <vector>

#if defined(DEBUG_CRC)
Int NET_CRC_INTERVAL = 1;
#else
Int NET_CRC_INTERVAL = 100;
#endif

NetworkInterface *TheNetwork = NULL;

namespace {

using Lockstep::ByteReader;
using Lockstep::ByteWriter;

const UnsignedInt DISCONNECT_CHAT = 0x80000000;	// chat mask bit: a line for the disconnect screen
const Int FILE_CHUNK_BYTES = 16 * 1024;
const UnsignedInt LEAVE_FLUSH_MS = 1000;		// how long a leaving player waits for its last messages

// ------------------------------------------------------------------------------------------------
// GameMessage <-> bytes
// ------------------------------------------------------------------------------------------------
inline void PutReal(ByteWriter &w, Real r) { UnsignedInt u; memcpy(&u, &r, 4); w.u32(u); }
inline Real GetReal(ByteReader &r) { UnsignedInt u = r.u32(); Real f; memcpy(&f, &u, 4); return f; }

std::vector<uint8_t> SerializeMessage(GameMessage *msg)
{
	ByteWriter w;
	w.u32((UnsignedInt)msg->getType());
	Int count = msg->getArgumentCount();
	w.u8((UnsignedInt)count);
	for (Int i = 0; i < count; ++i) {
		GameMessageArgumentDataType type = msg->getArgumentDataType(i);
		const GameMessageArgumentType *arg = msg->getArgument(i);
		w.u8((UnsignedInt)type);
		switch (type) {
			case ARGUMENTDATATYPE_INTEGER:		w.u32((UnsignedInt)arg->integer); break;
			case ARGUMENTDATATYPE_REAL:			PutReal(w, arg->real); break;		// bit-exact
			case ARGUMENTDATATYPE_BOOLEAN:		w.u8(arg->boolean ? 1 : 0); break;
			case ARGUMENTDATATYPE_OBJECTID:		w.u32((UnsignedInt)arg->objectID); break;
			case ARGUMENTDATATYPE_DRAWABLEID:	w.u32((UnsignedInt)arg->drawableID); break;
			case ARGUMENTDATATYPE_TEAMID:		w.u32(arg->teamID); break;
			case ARGUMENTDATATYPE_LOCATION:
				PutReal(w, arg->location.x); PutReal(w, arg->location.y); PutReal(w, arg->location.z); break;
			case ARGUMENTDATATYPE_PIXEL:		w.u32((UnsignedInt)arg->pixel.x); w.u32((UnsignedInt)arg->pixel.y); break;
			case ARGUMENTDATATYPE_PIXELREGION:
				w.u32((UnsignedInt)arg->pixelRegion.lo.x); w.u32((UnsignedInt)arg->pixelRegion.lo.y);
				w.u32((UnsignedInt)arg->pixelRegion.hi.x); w.u32((UnsignedInt)arg->pixelRegion.hi.y); break;
			case ARGUMENTDATATYPE_TIMESTAMP:	w.u32(arg->timestamp); break;
			case ARGUMENTDATATYPE_WIDECHAR:		w.u16((UnsignedInt)arg->wChar); break;
			default:							break;
		}
	}
	return w.data();
}

const Player *PlayerOfSlot(Int slot)
{
	AsciiString name;
	name.format("player%d", slot);
	return ThePlayerList ? ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(name)) : NULL;
}

// Rebuilds a command; it is executed as the player of `slot` (as the original NetGameCommandMsg did).
GameMessage *DeserializeMessage(const std::vector<uint8_t> &data, Int slot)
{
	ByteReader r(data);
	UnsignedInt type = r.u32();
	UnsignedInt count = r.u8();
	const Player *player = PlayerOfSlot(slot);
	if (!r.ok() || !player || type <= (UnsignedInt)GameMessage::MSG_BEGIN_NETWORK_MESSAGES ||
		type >= (UnsignedInt)GameMessage::MSG_END_NETWORK_MESSAGES) {
		DEBUG_CRASH(("Bad network command from slot %d", slot));
		return NULL;
	}
	GameMessage *msg = newInstance(GameMessage)((GameMessage::Type)type);
	msg->friend_setPlayerIndex(player->getPlayerIndex());
	for (UnsignedInt i = 0; i < count && r.ok(); ++i) {
		switch (r.u8()) {
			case ARGUMENTDATATYPE_INTEGER:		msg->appendIntegerArgument((Int)r.u32()); break;
			case ARGUMENTDATATYPE_REAL:			msg->appendRealArgument(GetReal(r)); break;
			case ARGUMENTDATATYPE_BOOLEAN:		msg->appendBooleanArgument(r.u8() != 0); break;
			case ARGUMENTDATATYPE_OBJECTID:		msg->appendObjectIDArgument((ObjectID)r.u32()); break;
			case ARGUMENTDATATYPE_DRAWABLEID:	msg->appendDrawableIDArgument((DrawableID)r.u32()); break;
			case ARGUMENTDATATYPE_TEAMID:		msg->appendTeamIDArgument(r.u32()); break;
			case ARGUMENTDATATYPE_LOCATION: {
				Coord3D c;
				c.x = GetReal(r); c.y = GetReal(r); c.z = GetReal(r);
				msg->appendLocationArgument(c);
				break;
			}
			case ARGUMENTDATATYPE_PIXEL: {
				ICoord2D p;
				p.x = (Int)r.u32(); p.y = (Int)r.u32();
				msg->appendPixelArgument(p);
				break;
			}
			case ARGUMENTDATATYPE_PIXELREGION: {
				IRegion2D reg;
				reg.lo.x = (Int)r.u32(); reg.lo.y = (Int)r.u32(); reg.hi.x = (Int)r.u32(); reg.hi.y = (Int)r.u32();
				msg->appendPixelRegionArgument(reg);
				break;
			}
			case ARGUMENTDATATYPE_TIMESTAMP:	msg->appendTimestampArgument(r.u32()); break;
			case ARGUMENTDATATYPE_WIDECHAR:		msg->appendWideCharArgument((WideChar)r.u16()); break;
			default:							r.bytes(NULL, (size_t)-1); break;	// unknown type: fail
		}
	}
	if (!r.ok()) {
		DEBUG_CRASH(("Truncated network command from slot %d", slot));
		msg->deleteInstance();
		return NULL;
	}
	return msg;
}

std::vector<uint16_t> ToUtf16(const UnicodeString &s)
{
	std::vector<uint16_t> v;
	const WideChar *p = s.str();
	for (Int i = 0; i < s.getLength(); ++i) {
		v.push_back((uint16_t)p[i]);
	}
	return v;
}

UnicodeString FromUtf16(const std::vector<uint16_t> &v)
{
	UnicodeString s;
	for (size_t i = 0; i < v.size(); ++i) {
		s.concat((WideChar)v[i]);
	}
	return s;
}

} // namespace

// =================================================================================================
// LockstepNetwork
// =================================================================================================
class LockstepNetwork : public NetworkInterface
{
public:
	LockstepNetwork();
	virtual ~LockstepNetwork();

	virtual void init( void );
	virtual void reset( void ) { init(); }
	virtual void update( void );
	virtual void liteupdate( void );
	virtual void setLocalAddress(UnsignedInt ip, UnsignedInt port) { m_localIP = ip; m_localPort = port; }
	virtual Bool isFrameDataReady( void ) { return m_frameDataReady || m_status == STATUS_POSTGAME; }
	virtual void parseUserList( const GameInfo *game );
	virtual void startGame(void) {}
	virtual UnsignedInt getRunAhead(void) { return m_session ? m_session->inputDelay() : 0; }
	virtual UnsignedInt getFrameRate(void) { return m_session ? m_session->frameRate() : Lockstep::DEFAULT_FRAME_RATE; }
	virtual UnsignedInt getPacketArrivalCushion(void) { return m_session ? m_session->stats().bufferedTurns : 0; }
	virtual void sendChat(UnicodeString text, Int playerMask);
	virtual void sendDisconnectChat(UnicodeString text);
	virtual void sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID);
	virtual UnsignedShort sendFileAnnounce(AsciiString path, UnsignedByte playerMask);
	virtual Int getFileTransferProgress(Int playerID, AsciiString path);
	virtual Bool areAllQueuesEmpty(void) { return TRUE; }	// announce and data travel in order on one channel
	virtual void quitGame();
	virtual void selfDestructPlayer(Int) {}
	virtual void voteForPlayerDisconnect(Int menuPosition);
	virtual Bool isPacketRouter( void ) { return m_session && m_session->isHost(); }
	virtual Real getIncomingBytesPerSecond( void ) { sampleRates(); return m_rateBytesIn; }
	virtual Real getIncomingPacketsPerSecond( void ) { sampleRates(); return m_ratePacketsIn; }
	virtual Real getOutgoingBytesPerSecond( void ) { sampleRates(); return m_rateBytesOut; }
	virtual Real getOutgoingPacketsPerSecond( void ) { sampleRates(); return m_ratePacketsOut; }
	virtual Real getUnknownBytesPerSecond( void ) { return 0.0f; }
	virtual Real getUnknownPacketsPerSecond( void ) { return 0.0f; }
	virtual void updateLoadProgress( Int percent );
	virtual void loadProgressComplete( void );
	virtual void sendTimeOutGameStart( void ) {}
	virtual UnsignedInt getLocalPlayerID() { return m_session ? (UnsignedInt)m_localSlot : 49; }
	virtual UnicodeString getPlayerName(Int playerNum);
	virtual Int getNumPlayers();
	virtual Int getAverageFPS() { return getFrameRate(); }
	virtual Int getSlotAverageFPS(Int) { return -1; }
	virtual void attachTransport(Transport *) {}			// GameSpy's NAT transport: not supported
	virtual void initTransport() {}							// the session opens its own (parseUserList)
	virtual Bool sawCRCMismatch() { return m_sawCRCMismatch; }
	virtual void setSawCRCMismatch();
	virtual Bool isPlayerConnected(Int playerID);
	virtual void notifyOthersOfCurrentFrame() {}
	virtual void notifyOthersOfNewFrame(UnsignedInt) {}
	virtual Int getExecutionFrame() { return (Int)TheGameLogic->getFrame() + (Int)getRunAhead(); }
#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void toggleNetworkOn( void ) { m_networkOn = !m_networkOn; }
#endif
	virtual UnsignedInt getPingFrame() { return 0; }
	virtual Int getPingsSent() { return 0; }
	virtual Int getPingsRecieved() { return 0; }

private:
	enum Status
	{
		STATUS_PREGAME,		// loading, frame 0: no turns needed
		STATUS_INGAME,		// frames run with turns
		STATUS_POSTGAME		// we left: frames run freely until the game is cleared
	};

	struct IncomingFile
	{
		AsciiString realPath;
		UnsignedInt size;
		UnsignedInt received;
		Int sender;
		std::vector<UnsignedByte> data;
		Bool written;
	};

	void deinit();
	void processEvents();
	void submitLocalCommands();
	void relayTurn(Lockstep::Frame frame);
	void playerLeft(Int slot, Int reason);
	void leaveGame();
	void showChat(Int fromSlot, UnsignedInt mask, const UnicodeString &text);
	void updateDisconnectScreen(const Lockstep::Event &e);
	void fileReceived(const Lockstep::Event &e);
	void sampleRates();
	Int menuPosition(Int slot) const { return slot < m_localSlot ? slot : slot - 1; }
	Int slotOfMenuPosition(Int pos) const { return pos < m_localSlot ? pos : pos + 1; }

	Lockstep::EnetTransport *m_transport;
	Lockstep::Session *m_session;
	UnsignedInt m_localIP;
	UnsignedInt m_localPort;
	Int m_localSlot;
	Status m_status;
	Bool m_frameDataReady;
	Bool m_sawCRCMismatch;
	Bool m_screenOn;
	Bool m_ownDisconnectMenu;
	GameWindow *m_messageWindow;
	UnicodeString m_names[MAX_SLOTS];

	// map transfer
	UnsignedShort m_nextFileID;
	std::map<AsciiString, UnsignedShort> m_fileIDByPath;
	std::map<UnsignedShort, std::vector<Int> > m_fileProgress;	// id -> percent per slot
	std::map<UnsignedShort, IncomingFile> m_incoming;

	// traffic rates (debug display)
	UnsignedInt m_rateSampleMs;
	Lockstep::TransportStats m_rateSample;
	Real m_rateBytesIn, m_ratePacketsIn, m_rateBytesOut, m_ratePacketsOut;

#if defined(_DEBUG) || defined(_INTERNAL)
	Bool m_networkOn;
#endif
};

NetworkInterface *NetworkInterface::createNetwork()
{
	return NEW LockstepNetwork;
}

LockstepNetwork::LockstepNetwork()
	: m_transport(NULL), m_session(NULL), m_localIP(0), m_localPort(NETWORK_BASE_PORT_NUMBER), m_localSlot(0),
	m_status(STATUS_PREGAME), m_frameDataReady(FALSE), m_sawCRCMismatch(FALSE), m_screenOn(FALSE),
	m_ownDisconnectMenu(FALSE), m_messageWindow(NULL), m_nextFileID(0), m_rateSampleMs(0),
	m_rateBytesIn(0), m_ratePacketsIn(0), m_rateBytesOut(0), m_ratePacketsOut(0)
{
#if defined(_DEBUG) || defined(_INTERNAL)
	m_networkOn = TRUE;
#endif
}

LockstepNetwork::~LockstepNetwork()
{
	deinit();
}

void LockstepNetwork::init()
{
	deinit();
	m_status = STATUS_PREGAME;
	m_frameDataReady = FALSE;
	m_sawCRCMismatch = FALSE;
	m_screenOn = FALSE;
	m_nextFileID = 0;
	m_fileIDByPath.clear();
	m_fileProgress.clear();
	m_incoming.clear();

	// The network layer owns the disconnect screen (as the original ConnectionManager did).
	if (TheDisconnectMenu == NULL) {
		TheDisconnectMenu = NEW DisconnectMenu;
		TheDisconnectMenu->init();
		TheDisconnectMenu->hideScreen();
		m_ownDisconnectMenu = TRUE;
	}
}

void LockstepNetwork::deinit()
{
	if (m_session) {
		// Leaving: give our last messages (our self-destruct, our departure) a moment to arrive.
		m_session->leave();
		UnsignedInt start = timeGetTime();
		while (!m_session->leaveDelivered() && timeGetTime() - start < LEAVE_FLUSH_MS) {
			Sleep(5);
		}
		m_session->shutdown();
		delete m_session;
		m_session = NULL;
	}
	delete m_transport;
	m_transport = NULL;

	if (m_ownDisconnectMenu) {
		delete TheDisconnectMenu;
		TheDisconnectMenu = NULL;
		m_ownDisconnectMenu = FALSE;
	}
	if (m_messageWindow) {
		TheWindowManager->winDestroy(m_messageWindow);
		m_messageWindow = NULL;
	}
}

void LockstepNetwork::parseUserList(const GameInfo *game)
{
	if (!game) {
		DEBUG_LOG(("LockstepNetwork::parseUserList - no game\n"));
		return;
	}
	Lockstep::SessionConfig config;
	config.localSlot = game->getLocalSlotNum();
	config.hostSlot = 0;		// the LAN game's creator
	config.sessionId = (UnsignedInt)game->getSeed();
	config.exeCRC = TheGlobalData->m_exeCRC;
	config.iniCRC = TheGlobalData->m_iniCRC;
	config.firstFrame = 1;		// frame 0 runs while the game starts, without commands
	config.frameRate = Lockstep::DEFAULT_FRAME_RATE;
	config.dropAfterMs = TheGlobalData->m_networkPlayerTimeoutTime;
	for (Int i = 0; i < MAX_SLOTS && i < Lockstep::MAX_PLAYERS; ++i) {
		const GameSlot *slot = game->getConstSlot(i);
		if (slot && slot->isHuman()) {
			config.players[i].human = true;
			config.players[i].ip = slot->getIP();
			config.players[i].port = slot->getPort() ? slot->getPort() : NETWORK_BASE_PORT_NUMBER;
			m_names[i] = slot->getName();
		}
	}
	if (config.localSlot < 0 || config.localSlot >= Lockstep::MAX_PLAYERS) {
		DEBUG_CRASH(("LockstepNetwork::parseUserList - we are not in the game"));
		return;
	}
	m_localSlot = config.localSlot;
	config.players[m_localSlot].port = (uint16_t)m_localPort;

	m_transport = NEW Lockstep::EnetTransport;
	m_session = NEW Lockstep::Session(m_transport, config);
	if (!m_session->start(timeGetTime())) {
		DEBUG_LOG(("LockstepNetwork::parseUserList - could not open UDP port %d\n", m_localPort));
	}
}

// -------------------------------------------------------------------------------------------------
// Per-frame work
// -------------------------------------------------------------------------------------------------
void LockstepNetwork::update()
{
	m_frameDataReady = FALSE;
#if defined(_DEBUG) || defined(_INTERNAL)
	if (!m_networkOn) {
		return;
	}
#endif
	if (!m_session) {
		return;
	}
	const Lockstep::Frame frame = TheGameLogic->getFrame();
	if (m_status == STATUS_PREGAME && frame >= 1) {
		m_status = STATUS_INGAME;	// frame 0 has run: from now on every frame needs its turn
	}

	submitLocalCommands();
	liteupdate();

	if (m_status == STATUS_PREGAME) {
		m_frameDataReady = TRUE;	// starting the game (loading runs inside the logic)
	} else if (m_status == STATUS_INGAME) {
		if (m_session->isHost() && !m_session->gameStarted()) {
			// The game started without everyone having loaded (the loading screen timed out).
			m_session->forceStart();
		}
		if (m_session->readyForFrame(frame, timeGetTime())) {
			relayTurn(frame);
			m_frameDataReady = TRUE;
		}
	}
}

void LockstepNetwork::liteupdate()
{
	if (m_session) {
		m_session->update(timeGetTime());
		processEvents();
	}
}

void LockstepNetwork::submitLocalCommands()
{
	GameMessage *msg = TheCommandList->getFirstMessage();
	while (msg) {
		GameMessage *next = msg->next();
		GameMessage::Type type = msg->getType();

		if (type == GameMessage::MSG_LOGIC_CRC) {
			// Computed at the end of the previous frame.  The host compares everyone's; the message
			// itself stays on the list for the replay recorder.
			if (m_status == STATUS_INGAME && !msg->getArgument(1)->boolean) {
				m_session->reportCRC(TheGameLogic->getFrame() - 1, (UnsignedInt)msg->getArgument(0)->integer);
			}
		} else if (type > GameMessage::MSG_BEGIN_NETWORK_MESSAGES && type < GameMessage::MSG_END_NETWORK_MESSAGES) {
			if (m_status == STATUS_INGAME) {
				m_session->submitCommand(SerializeMessage(msg));
			}
			TheCommandList->removeMessage(msg);
			msg->deleteInstance();
		} else if (type == GameMessage::MSG_CLEAR_GAME_DATA && m_status == STATUS_INGAME) {
			// We are leaving (quit menu, or the game ended).  Our commands so far (our
			// self-destruct) go first; the clear itself proceeds to the score screen.
			leaveGame();
		}
		msg = next;
	}
}

void LockstepNetwork::leaveGame()
{
	m_session->leave();
	m_status = STATUS_POSTGAME;
	if (m_screenOn && TheDisconnectMenu) {
		TheDisconnectMenu->hideScreen();
		m_screenOn = FALSE;
	}
}

void LockstepNetwork::relayTurn(Lockstep::Frame frame)
{
	Lockstep::Turn turn;
	if (!m_session->takeTurn(frame, turn)) {
		return;
	}
	// Commands in the host's order: the same on every machine.
	for (size_t i = 0; i < turn.commands.size(); ++i) {
		GameMessage *msg = DeserializeMessage(turn.commands[i].data, turn.commands[i].slot);
		if (msg) {
			TheCommandList->appendMessage(msg);
		}
	}
	// Then the frame's events (after the commands, as the original DESTROYPLAYER).
	for (size_t i = 0; i < turn.events.size(); ++i) {
		const Lockstep::TurnEvent &ev = turn.events[i];
		if (ev.type == Lockstep::EVENT_PLAYER_LEFT) {
			playerLeft(ev.slot, ev.reason);
		} else if (ev.type == Lockstep::EVENT_DESYNC && !m_sawCRCMismatch) {
			setSawCRCMismatch();
			// Every machine writes its state on this same frame: compare the files to find what
			// diverged first (Tests/Determinism/compare_desync_dumps.py).
			AsciiString dumpName;
			dumpName.format("%sDesync_frame%d_player%d.crc", TheGlobalData->getPath_UserData().str(), frame, m_localSlot);
			TheGameLogic->getCRC(CRC_RECALC, dumpName);
			DEBUG_LOG(("CRC mismatch (CRCs of frame %u) - wrote %s\n", ev.frame, dumpName.str()));
		}
	}
}

void LockstepNetwork::playerLeft(Int slot, Int reason)
{
	if (slot < 0 || slot >= MAX_SLOTS || slot == m_localSlot) {
		return;
	}
	if (TheGameInfo) {
		GameSlot *gameSlot = TheGameInfo->getSlot(slot);
		if (gameSlot && !gameSlot->lastFrameInGame()) {
			gameSlot->setLastFrameInGame(TheGameLogic->getFrame());
		}
		if (gameSlot && reason != Lockstep::LEAVE_QUIT) {
			gameSlot->markAsDisconnected();
		}
	}
	if (reason != Lockstep::LEAVE_QUIT) {
		// Lost or dropped: its army is destroyed (a player who quits sent its own self-destruct).
		const Player *player = PlayerOfSlot(slot);
		if (player) {
			GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_SELF_DESTRUCT);
			msg->appendBooleanArgument(FALSE);
			msg->friend_setPlayerIndex(player->getPlayerIndex());
			TheCommandList->appendMessage(msg);
		}
		TheRecorder->logPlayerDisconnect(m_names[slot], slot);
		TheInGameUI->message("Network:PlayerLeftGame", m_names[slot].str());
		static AudioEventRTS leftGameSound("GUIMessageReceived");
		TheAudio->addAudioEvent(&leftGameSound);
		if (TheDisconnectMenu) {
			TheDisconnectMenu->removePlayer(menuPosition(slot), m_names[slot]);
		}
	}
	PopulateInGameDiplomacyPopup();
}

// -------------------------------------------------------------------------------------------------
// Session events
// -------------------------------------------------------------------------------------------------
void LockstepNetwork::processEvents()
{
	Lockstep::Event e;
	while (m_session->pollEvent(e)) {
		switch (e.type) {
			case Lockstep::Event::LOAD_PROGRESS:
				TheGameLogic->processProgress(e.slot, e.value);
				break;
			case Lockstep::Event::LOAD_COMPLETE:
				TheGameLogic->processProgressComplete(e.slot);
				break;
			case Lockstep::Event::CHAT: {
				UnicodeString text = FromUtf16(e.text);
				if (e.mask & DISCONNECT_CHAT) {
					UnicodeString line;
					line.format(L"[%ls] %ls", m_names[e.slot].str(), text.str());
					if (TheDisconnectMenu) {
						TheDisconnectMenu->showChat(line);
					}
				} else {
					showChat(e.slot, e.mask, text);
				}
				break;
			}
			case Lockstep::Event::WAITING:
				updateDisconnectScreen(e);
				break;
			case Lockstep::Event::HOST_LOST:
				// Nobody can take over: the game cannot go on.  The disconnect screen offers to quit.
				if (TheDisconnectMenu && m_status == STATUS_INGAME) {
					TheDisconnectMenu->showScreen();
					TheDisconnectMenu->showPacketRouterTimeout();
					TheDisconnectMenu->setPacketRouterTimeoutTime(0);
					m_screenOn = TRUE;
				}
				break;
			case Lockstep::Event::HOST_CHANGED:
				DEBUG_LOG(("LockstepNetwork - slot %d is the host now\n", e.slot));
				break;
			case Lockstep::Event::CONNECT_FAILED:
				DEBUG_LOG(("LockstepNetwork - could not join the game (reason %d)\n", e.value));
				break;
			case Lockstep::Event::FILE_ANNOUNCE:
			case Lockstep::Event::FILE_DATA:
				fileReceived(e);
				break;
			case Lockstep::Event::FILE_PROGRESS:
				if (m_fileProgress.count((UnsignedShort)e.id) && e.slot >= 0 && e.slot < MAX_SLOTS) {
					m_fileProgress[(UnsignedShort)e.id][e.slot] = e.value;
				}
				break;
			default:
				break;
		}
	}
}

void LockstepNetwork::updateDisconnectScreen(const Lockstep::Event &e)
{
	if (!TheDisconnectMenu || m_status != STATUS_INGAME) {
		return;
	}
	UnsignedInt longest = 0;
	for (Int i = 0; i < Lockstep::MAX_PLAYERS; ++i) {
		longest = max(longest, (UnsignedInt)e.waitingMs[i]);
	}
	if (longest == 0) {
		if (m_screenOn) {
			TheDisconnectMenu->hideScreen();
			m_screenOn = FALSE;
		}
		return;
	}
	// Short waits are not worth a screen (as before: the network disconnect time, 5 s by default).
	if (!m_screenOn && longest < TheGlobalData->m_networkDisconnectTime) {
		return;
	}
	if (!m_screenOn) {
		TheDisconnectMenu->showScreen();
		m_screenOn = TRUE;
		for (Int i = 0; i < MAX_SLOTS; ++i) {
			if (i != m_localSlot) {
				TheDisconnectMenu->setPlayerName(menuPosition(i), isPlayerConnected(i) ? m_names[i] : UnicodeString::TheEmptyString);
				TheDisconnectMenu->updateVotes(menuPosition(i), 0);
			}
		}
	}
	const UnsignedInt timeout = TheGlobalData->m_networkPlayerTimeoutTime;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (i == m_localSlot || !isPlayerConnected(i)) {
			continue;
		}
		UnsignedInt waited = e.waitingMs[i];
		time_t remaining = waited >= timeout ? 0 : (time_t)(timeout - waited);
		TheDisconnectMenu->setPlayerTimeoutTime(menuPosition(i), remaining);
	}
}

// -------------------------------------------------------------------------------------------------
// Loading
// -------------------------------------------------------------------------------------------------
void LockstepNetwork::updateLoadProgress(Int percent)
{
	TheGameLogic->processProgress(m_localSlot, percent);
	if (m_session) {
		m_session->sendLoadProgress(percent);
	}
}

void LockstepNetwork::loadProgressComplete()
{
	TheGameLogic->processProgressComplete(m_localSlot);
	if (m_session) {
		m_session->sendLoadComplete();
	}
}

// -------------------------------------------------------------------------------------------------
// Players
// -------------------------------------------------------------------------------------------------
Bool LockstepNetwork::isPlayerConnected(Int slot)
{
	if (!m_session || slot < 0 || slot >= MAX_SLOTS) {
		return FALSE;
	}
	if (slot == m_localSlot) {
		return m_status != STATUS_POSTGAME;
	}
	return m_session->inGame(slot);
}

UnicodeString LockstepNetwork::getPlayerName(Int slot)
{
	if (slot < 0 || slot >= MAX_SLOTS || !isPlayerConnected(slot)) {
		return UnicodeString::TheEmptyString;
	}
	return m_names[slot];
}

Int LockstepNetwork::getNumPlayers()
{
	Int n = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (isPlayerConnected(i)) {
			++n;
		}
	}
	return n;
}

void LockstepNetwork::quitGame()
{
	// From the disconnect screen.  Unlike the original, our army leaves with us instead of
	// standing idle: the self-destruct goes out before we leave.
	if (m_status == STATUS_INGAME && m_session) {
		GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_SELF_DESTRUCT);
		msg->appendBooleanArgument(TRUE);
		m_session->submitCommand(SerializeMessage(msg));
		msg->deleteInstance();
		leaveGame();
	}
	m_status = STATUS_POSTGAME;
	TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
}

void LockstepNetwork::voteForPlayerDisconnect(Int menuPos)
{
	Int slot = slotOfMenuPosition(menuPos);
	if (m_session && slot >= 0 && slot < MAX_SLOTS) {
		m_session->voteToDrop(slot);
		if (TheDisconnectMenu) {
			TheDisconnectMenu->updateVotes(menuPos, 1);
		}
	}
}

void LockstepNetwork::setSawCRCMismatch()
{
	m_sawCRCMismatch = TRUE;
	TheScriptActions->closeWindows(TRUE);
	m_messageWindow = TheWindowManager->winCreateFromScript("Menus/CRCMismatch.wnd");
	TheScriptEngine->startEndGameTimer();
	TheRecorder->logCRCMismatch();
	DEBUG_LOG(("CRC mismatch on frame %d\n", TheGameLogic->getFrame()));
}

// -------------------------------------------------------------------------------------------------
// Chat
// -------------------------------------------------------------------------------------------------
void LockstepNetwork::showChat(Int fromSlot, UnsignedInt mask, const UnicodeString &text)
{
	UnicodeString line;
	line.format(L"[%ls] %ls", (fromSlot >= 0 && fromSlot < MAX_SLOTS) ? m_names[fromSlot].str() : L"", text.str());
	const Player *player = PlayerOfSlot(fromSlot);
	if (!player) {
		TheInGameUI->message(UnicodeString(L"%ls"), line.str());
		return;
	}
	Bool fromObserver = !player->isPlayerActive();
	Bool amIObserver = !ThePlayerList->getLocalPlayer()->isPlayerActive();
	Bool canSeeChat = amIObserver || (!fromObserver && !(TheGameInfo && TheGameInfo->getConstSlot(fromSlot)->isMuted()));
	if (((1 << m_localSlot) & mask) && canSeeChat) {
		RGBColor rgb;
		rgb.setFromInt(player->getPlayerColor());
		TheInGameUI->messageColor(&rgb, UnicodeString(L"%ls"), line.str());
		AudioEventRTS audioEvent("GUICommunicatorIncoming");
		TheAudio->addAudioEvent(&audioEvent);
	}
}

void LockstepNetwork::sendChat(UnicodeString text, Int playerMask)
{
	showChat(m_localSlot, (UnsignedInt)playerMask, text);	// our own line, as the original echoed it
	if (m_session) {
		m_session->sendChat((UnsignedInt)playerMask & 0xff, ToUtf16(text));
	}
}

void LockstepNetwork::sendDisconnectChat(UnicodeString text)
{
	UnicodeString line;
	line.format(L"[%ls] %ls", m_names[m_localSlot].str(), text.str());
	if (TheDisconnectMenu) {
		TheDisconnectMenu->showChat(line);
	}
	if (m_session) {
		m_session->sendChat(0xff | DISCONNECT_CHAT, ToUtf16(text));
	}
}

// -------------------------------------------------------------------------------------------------
// Map transfer (FileTransfer.cpp): the host sends, everyone shows everyone's progress
// -------------------------------------------------------------------------------------------------
UnsignedShort LockstepNetwork::sendFileAnnounce(AsciiString path, UnsignedByte playerMask)
{
	UnsignedShort id = ++m_nextFileID;
	UnsignedInt size = 0;
	File *fp = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
	if (fp) {
		size = fp->size();
		fp->close();
	}
	std::vector<Int> &progress = m_fileProgress[id];
	progress.assign(MAX_SLOTS, 100);
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (playerMask & (1 << i)) {
			progress[i] = 0;
		}
	}
	m_fileIDByPath[path] = id;
	if (m_session) {
		m_session->announceFile(id, TheGameState->realMapPathToPortableMapPath(path).str(), size, playerMask);
	}
	return id;
}

void LockstepNetwork::sendFile(AsciiString path, UnsignedByte, UnsignedShort commandID)
{
	File *fp = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
	if (!fp || !m_session) {
		DEBUG_LOG(("LockstepNetwork::sendFile - cannot open %s\n", path.str()));
		if (fp) {
			fp->close();
		}
		return;
	}
	Int size = fp->size();
	std::vector<uint8_t> chunk;
	for (Int offset = 0; offset < size; offset += FILE_CHUNK_BYTES) {
		Int n = min(FILE_CHUNK_BYTES, size - offset);
		chunk.resize(n);
		fp->read(&chunk[0], n);
		m_session->sendFileData(commandID, (UnsignedInt)offset, chunk);
	}
	fp->close();
}

void LockstepNetwork::fileReceived(const Lockstep::Event &e)
{
	UnsignedShort id = (UnsignedShort)e.id;
	if (e.type == Lockstep::Event::FILE_ANNOUNCE) {
		IncomingFile &f = m_incoming[id];
		f.realPath = TheGameState->portableMapPathToRealMapPath(AsciiString(e.name.c_str()));
		f.size = (UnsignedInt)e.value;
		f.received = 0;
		f.sender = e.slot;
		f.data.assign(f.size, 0);
		f.written = FALSE;
		m_fileIDByPath[f.realPath] = id;
		std::vector<Int> &progress = m_fileProgress[id];
		progress.assign(MAX_SLOTS, 100);
		for (Int i = 0; i < MAX_SLOTS; ++i) {
			if (e.mask & (1 << i)) {
				progress[i] = 0;
			}
		}
		return;
	}

	std::map<UnsignedShort, IncomingFile>::iterator it = m_incoming.find(id);
	if (it == m_incoming.end() || it->second.written) {
		return;
	}
	IncomingFile &f = it->second;
	UnsignedInt offset = (UnsignedInt)e.value;
	if (offset > f.size || e.data.size() > f.size - offset) {
		DEBUG_CRASH(("File data past the end of %s", f.realPath.str()));
		return;
	}
	if (!e.data.empty()) {
		memcpy(&f.data[offset], &e.data[0], e.data.size());
	}
	f.received += (UnsignedInt)e.data.size();

	Int percent = f.size ? (Int)((unsigned long long)min(f.received, f.size) * 99 / f.size) : 99;
	if (f.received >= f.size) {
		File *fp = TheFileSystem->openFile(f.realPath.str(), File::CREATE | File::BINARY | File::WRITE);
		if (fp) {
			if (f.size) {
				fp->write(&f.data[0], f.size);
			}
			fp->close();
			DEBUG_LOG(("Received %s (%d bytes)\n", f.realPath.str(), f.size));
		} else {
			DEBUG_LOG(("Cannot write %s\n", f.realPath.str()));
		}
		f.written = TRUE;
		f.data.clear();
		percent = 100;
	}
	m_fileProgress[id][m_localSlot] = percent;
	m_session->sendFileProgress(f.sender, id, percent);
}

Int LockstepNetwork::getFileTransferProgress(Int playerID, AsciiString path)
{
	std::map<AsciiString, UnsignedShort>::const_iterator it = m_fileIDByPath.find(path);
	if (it == m_fileIDByPath.end() || playerID < 0 || playerID >= MAX_SLOTS) {
		return 0;
	}
	std::map<UnsignedShort, std::vector<Int> >::const_iterator p = m_fileProgress.find(it->second);
	return p == m_fileProgress.end() ? 0 : p->second[playerID];
}

// -------------------------------------------------------------------------------------------------
// Traffic rates (debug display)
// -------------------------------------------------------------------------------------------------
void LockstepNetwork::sampleRates()
{
	if (!m_transport) {
		return;
	}
	UnsignedInt now = timeGetTime();
	if (m_rateSampleMs && now - m_rateSampleMs < 1000) {
		return;
	}
	Lockstep::TransportStats s = m_transport->stats();
	if (m_rateSampleMs) {
		Real seconds = (now - m_rateSampleMs) / 1000.0f;
		m_rateBytesIn = (Real)(s.bytesReceived - m_rateSample.bytesReceived) / seconds;
		m_ratePacketsIn = (Real)(s.packetsReceived - m_rateSample.packetsReceived) / seconds;
		m_rateBytesOut = (Real)(s.bytesSent - m_rateSample.bytesSent) / seconds;
		m_ratePacketsOut = (Real)(s.packetsSent - m_rateSample.packetsSent) / seconds;
	}
	m_rateSample = s;
	m_rateSampleMs = now;
}
