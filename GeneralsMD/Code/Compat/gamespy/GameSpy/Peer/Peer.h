/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the GameSpy Peer SDK header (the SDK is not part of the source release and the
// GameSpy service no longer exists).  Declares what the game uses; the functions are implemented
// in gamespy_stub.cpp and behave as if the servers were unreachable.  A real implementation
// (for example one talking to an OpenSpy-compatible server) can replace the stub library.

#ifndef ZH_GAMESPY_PEER_H
#define ZH_GAMESPY_PEER_H

#include <winsock.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *PEER;
typedef int PEERBool;
#define PEERTrue	1
#define PEERFalse	0

typedef enum { TitleRoom, GroupRoom, StagingRoom, NumRooms } RoomType;
typedef enum { NormalMessage, ActionMessage, NoticeMessage } MessageType;
typedef enum
{
	PEERJoinSuccess, PEERFullRoom, PEERInviteOnlyRoom, PEERBannedFromRoom, PEERBadPassword,
	PEERAlreadyInRoom, PEERNoTitleSet, PEERNoConnection, PEERAutoMatching, PEERJoinFailed
} PEERJoinResult;

// player flags
#define PEER_FLAG_STAGING	0x01
#define PEER_FLAG_READY		0x02
#define PEER_FLAG_PLAYING	0x04
#define PEER_FLAG_AWAY		0x08
#define PEER_FLAG_HOST		0x10
#define PEER_FLAG_OP		0x20
#define PEER_FLAG_VOICE		0x40

// nick error types (peerNickErrorCallback)
#define PEER_IN_USE					1
#define PEER_INVALID				2
#define PEER_UNIQUENICK_EXPIRED		3
#define PEER_NO_UNIQUENICK			4
#define PEER_INVALID_UNIQUENICK		5
#define PEER_NICK_TOO_LONG			6

// game list messages
#define PEER_CLEAR		0
#define PEER_ADD		1
#define PEER_UPDATE		2
#define PEER_REMOVE		3
#define PEER_COMPLETE	4

// peerStartGame reporting options
#define PEER_KEEP_REPORTING		0
#define PEER_STOP_REPORTING		1

// ---- server browsing / query reporting (sb / qr2) ----------------------------------------------
struct _SBServer { void *keyvals; };	// the game checks keyvals before using a server
typedef struct _SBServer *SBServer;

// Query & reporting 2 reserved keys
#define HOSTNAME_KEY		1
#define GAMENAME_KEY		2
#define GAMEVER_KEY			3
#define HOSTPORT_KEY		4
#define MAPNAME_KEY			5
#define GAMETYPE_KEY		6
#define GAMEVARIANT_KEY		7
#define NUMPLAYERS_KEY		8
#define NUMTEAMS_KEY		9
#define MAXPLAYERS_KEY		10
#define GAMEMODE_KEY		11
#define TEAMPLAY_KEY		12
#define FRAGLIMIT_KEY		13
#define TEAMFRAGLIMIT_KEY	14
#define TIMEELAPSED_KEY		15
#define TIMELIMIT_KEY		16
#define ROUNDTIME_KEY		17
#define ROUNDELAPSED_KEY	18
#define PASSWORD_KEY		19
#define GROUPID_KEY			20
#define PLAYER__KEY			21
#define SCORE__KEY			22
#define SKILL__KEY			23
#define PING__KEY			24
#define TEAM__KEY			25
#define DEATHS__KEY			26
#define PID__KEY			27
#define TEAM_T_KEY			28
#define SCORE_T_KEY			29
#define NUM_RESERVED_KEYS	50
typedef struct _qr2_buffer *qr2_buffer_t;
typedef struct _qr2_keybuffer *qr2_keybuffer_t;
typedef enum { key_server, key_player, key_team, key_type_count } qr2_key_type;
typedef enum { e_qrnoerror, e_qrwsockerror, e_qrbinderror, e_qrdnserror, e_qrconnerror, e_qrnochallengeerror } qr2_error_t;
#define MAX_REGISTERED_KEYS 254
extern const char *qr2_registered_key_list[MAX_REGISTERED_KEYS];
typedef void (*SBServerKeyEnumFn)(char *key, char *value, void *instance);

void qr2_register_key(int keyid, const char *key);
void qr2_buffer_add(qr2_buffer_t outbuf, const char *value);
void qr2_buffer_add_int(qr2_buffer_t outbuf, int value);
void qr2_keybuffer_add(qr2_keybuffer_t keybuffer, int keyid);

const char *SBServerGetStringValue(SBServer server, const char *keyname, const char *def);
int SBServerGetIntValue(SBServer server, const char *key, int idefault);
const char *SBServerGetPlayerStringValue(SBServer server, int playernum, const char *key, const char *sdefault);
int SBServerGetPlayerIntValue(SBServer server, int playernum, const char *key, int idefault);
unsigned int SBServerGetPublicInetAddress(SBServer server);
unsigned int SBServerGetPrivateInetAddress(SBServer server);
unsigned short SBServerGetPrivateQueryPort(SBServer server);
int SBServerHasBasicKeys(SBServer server);
int SBServerHasFullKeys(SBServer server);
void SBServerEnumKeys(SBServer server, SBServerKeyEnumFn KeyFn, void *instance);

void chatSetLocalIP(unsigned int IP);

// ---- callbacks -----------------------------------------------------------------------------------
typedef void (*peerDisconnectedCallback)(PEER peer, const char *reason, void *param);
typedef void (*peerRoomMessageCallback)(PEER peer, RoomType roomType, const char *nick, const char *message, MessageType messageType, void *param);
typedef void (*peerPlayerMessageCallback)(PEER peer, const char *nick, const char *message, MessageType messageType, void *param);
typedef void (*peerReadyChangedCallback)(PEER peer, const char *nick, PEERBool ready, void *param);
typedef void (*peerGameStartedCallback)(PEER peer, unsigned int IP, const char *message, void *param);
typedef void (*peerPlayerJoinedCallback)(PEER peer, RoomType roomType, const char *nick, void *param);
typedef void (*peerPlayerLeftCallback)(PEER peer, RoomType roomType, const char *nick, const char *reason, void *param);
typedef void (*peerKickedCallback)(PEER peer, RoomType roomType, const char *nick, const char *reason, void *param);
typedef void (*peerNewPlayerListCallback)(PEER peer, RoomType roomType, void *param);
typedef void (*peerPlayerChangedNickCallback)(PEER peer, RoomType roomType, const char *oldNick, const char *newNick, void *param);
typedef void (*peerPlayerInfoCallback)(PEER peer, RoomType roomType, const char *nick, unsigned int IP, int profileID, void *param);
typedef void (*peerPlayerFlagsChangedCallback)(PEER peer, RoomType roomType, const char *nick, int oldFlags, int newFlags, void *param);
typedef void (*peerRoomUTMCallback)(PEER peer, RoomType roomType, const char *nick, const char *command, const char *parameters, PEERBool authenticated, void *param);
typedef void (*peerPlayerUTMCallback)(PEER peer, const char *nick, const char *command, const char *parameters, PEERBool authenticated, void *param);
typedef void (*peerGlobalKeyChangedCallback)(PEER peer, const char *nick, const char *key, const char *value, void *param);
typedef void (*peerRoomKeyChangedCallback)(PEER peer, RoomType roomType, const char *nick, const char *key, const char *value, void *param);
typedef void (*peerQRServerKeyCallback)(PEER peer, int key, qr2_buffer_t buffer, void *param);
typedef void (*peerQRPlayerKeyCallback)(PEER peer, int key, int index, qr2_buffer_t buffer, void *param);
typedef void (*peerQRTeamKeyCallback)(PEER peer, int key, int index, qr2_buffer_t buffer, void *param);
typedef void (*peerQRKeyListCallback)(PEER peer, qr2_key_type type, qr2_keybuffer_t keyBuffer, void *param);
typedef int (*peerQRCountCallback)(PEER peer, qr2_key_type type, void *param);
typedef void (*peerQRAddErrorCallback)(PEER peer, qr2_error_t error, char *errorString, void *param);
typedef void (*peerQRNatNegotiateCallback)(PEER peer, int cookie, void *param);

typedef void (*peerConnectCallback)(PEER peer, PEERBool success, void *param);
typedef void (*peerNickErrorCallback)(PEER peer, int type, const char *nick, void *param);
typedef void (*peerJoinRoomCallback)(PEER peer, PEERBool success, PEERJoinResult result, RoomType roomType, void *param);
typedef void (*peerListGroupRoomsCallback)(PEER peer, PEERBool success, int groupID, SBServer server, const char *name,
	int numWaiting, int maxWaiting, int numGames, int numPlaying, void *param);
typedef void (*peerListingGamesCallback)(PEER peer, PEERBool success, const char *name, SBServer server, PEERBool staging,
	int msg, int progress, void *param);
typedef void (*peerEnumPlayersCallback)(PEER peer, PEERBool success, RoomType roomType, int index, const char *nick, int flags, void *param);
typedef void (*peerGetPlayerProfileIDCallback)(PEER peer, PEERBool success, const char *nick, int profileID, void *param);
typedef void (*peerGetRoomKeysCallback)(PEER peer, PEERBool success, RoomType roomType, const char *nick, int num, char **keys, char **values, void *param);
typedef void (*peerAuthenticateCDKeyCallback)(PEER peer, int result, const char *message, void *param);

typedef struct PEERCallbacks
{
	peerDisconnectedCallback disconnected;
	peerRoomMessageCallback roomMessage;
	peerPlayerMessageCallback playerMessage;
	peerReadyChangedCallback readyChanged;
	peerGameStartedCallback gameStarted;
	peerPlayerJoinedCallback playerJoined;
	peerPlayerLeftCallback playerLeft;
	peerKickedCallback kicked;
	peerNewPlayerListCallback newPlayerList;
	peerPlayerChangedNickCallback playerChangedNick;
	peerPlayerInfoCallback playerInfo;
	peerPlayerFlagsChangedCallback playerFlagsChanged;
	peerRoomUTMCallback roomUTM;
	peerPlayerUTMCallback playerUTM;
	peerGlobalKeyChangedCallback globalKeyChanged;
	peerRoomKeyChangedCallback roomKeyChanged;
	peerQRServerKeyCallback qrServerKey;
	peerQRPlayerKeyCallback qrPlayerKey;
	peerQRTeamKeyCallback qrTeamKey;
	peerQRKeyListCallback qrKeyList;
	peerQRCountCallback qrCount;
	peerQRAddErrorCallback qrAddError;
	peerQRNatNegotiateCallback qrNatNegotiateCallback;
	void *param;
} PEERCallbacks;

// ---- functions -----------------------------------------------------------------------------------
PEER peerInitialize(PEERCallbacks *callbacks);
void peerShutdown(PEER peer);
void peerThink(PEER peer);
PEERBool peerSetTitle(PEER peer, const char *title, const char *qrSecretKey, const char *sbName, const char *sbSecretKey,
	int sbGameVersion, int sbMaxUpdates, PEERBool natNegotiate, PEERBool pingRooms[NumRooms], PEERBool crossPingRooms[NumRooms]);
void peerConnect(PEER peer, const char *nick, int profileID, peerNickErrorCallback nickErrorCallback,
	peerConnectCallback connectCallback, void *param, PEERBool blocking);
void peerRetryWithNick(PEER peer, const char *nick);
void peerDisconnect(PEER peer);
PEERBool peerIsConnected(PEER peer);
unsigned int peerGetLocalIP(PEER peer);
void peerAuthenticateCDKey(PEER peer, const char *cdkey, peerAuthenticateCDKeyCallback callback, void *param, PEERBool blocking);
void peerParseQuery(PEER peer, char *query, int len, struct sockaddr *sender);

void peerJoinTitleRoom(PEER peer, const char *password, peerJoinRoomCallback callback, void *param, PEERBool blocking);
void peerJoinGroupRoom(PEER peer, int groupID, peerJoinRoomCallback callback, void *param, PEERBool blocking);
void peerJoinStagingRoom(PEER peer, SBServer server, const char *password, peerJoinRoomCallback callback, void *param, PEERBool blocking);
void peerCreateStagingRoomWithSocket(PEER peer, const char *name, int maxPlayers, const char *password, SOCKET socket,
	unsigned short port, peerJoinRoomCallback callback, void *param, PEERBool blocking);
void peerLeaveRoom(PEER peer, RoomType roomType, const char *reason);
void peerListGroupRooms(PEER peer, const char *fields, peerListGroupRoomsCallback callback, void *param, PEERBool blocking);
void peerStartListingGames(PEER peer, const unsigned char *fields, int numFields, const char *filter,
	peerListingGamesCallback callback, void *param);
void peerStopListingGames(PEER peer);
void peerUpdateGame(PEER peer, SBServer server, PEERBool fullUpdate);

void peerMessageRoom(PEER peer, RoomType roomType, const char *message, MessageType messageType);
void peerMessagePlayer(PEER peer, const char *nick, const char *message, MessageType messageType);
void peerUTMRoom(PEER peer, RoomType roomType, const char *command, const char *parameters, PEERBool authenticate);
void peerUTMPlayer(PEER peer, const char *nick, const char *command, const char *parameters, PEERBool authenticate);

void peerSetReady(PEER peer, PEERBool ready);
void peerStartGame(PEER peer, const char *message, int reportingOptions);
void peerStopGame(PEER peer);
void peerStateChanged(PEER peer);
void peerStateChange(PEER peer);

void peerSetGlobalKeys(PEER peer, int num, const char **keys, const char **values);
void peerSetGlobalWatchKeys(PEER peer, RoomType roomType, int num, const char **keys, PEERBool addKeys);
const char *peerGetGlobalWatchKey(PEER peer, const char *nick, const char *key);
void peerSetRoomKeys(PEER peer, RoomType roomType, const char *nick, int num, const char **keys, const char **values);
void peerSetRoomWatchKeys(PEER peer, RoomType roomType, int num, const char **keys, PEERBool addKeys);
void peerGetRoomKeys(PEER peer, RoomType roomType, const char *nick, int num, const char **keys,
	peerGetRoomKeysCallback callback, void *param, PEERBool blocking);

void peerEnumPlayers(PEER peer, RoomType roomType, peerEnumPlayersCallback callback, void *param);
void peerGetPlayerProfileID(PEER peer, const char *nick, peerGetPlayerProfileIDCallback callback, void *param, PEERBool blocking);
PEERBool peerGetPlayerInfoNoWait(PEER peer, const char *nick, unsigned int *IP, int *profileID);
PEERBool peerGetPlayerFlags(PEER peer, const char *nick, RoomType roomType, int *flags);

#ifdef __cplusplus
}
#endif

#endif // ZH_GAMESPY_PEER_H
