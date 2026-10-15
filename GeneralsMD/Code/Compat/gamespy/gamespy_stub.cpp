/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Offline implementation of the GameSpy SDK functions used by the game.
//
// The GameSpy servers were shut down in 2014 and the SDK is not part of the source release.
// Every function behaves like an unreachable server: connection attempts fail, lists are empty,
// no callbacks fire.  Online play is therefore unavailable; LAN and direct IP play are not
// affected.  Replace this library with a real implementation to restore online play.

#include "GameSpy/Peer/Peer.h"
#include "GameSpy/GP/GP.h"
#include "GameSpy/ghttp/ghttp.h"
#include "GameSpy/gstats/gpersist.h"
#include <string.h>

extern "C" {

const char *qr2_registered_key_list[MAX_REGISTERED_KEYS] = { 0 };

void qr2_register_key(int keyid, const char *key)
{
	if (keyid >= 0 && keyid < MAX_REGISTERED_KEYS)
		qr2_registered_key_list[keyid] = key;
}
void qr2_buffer_add(qr2_buffer_t, const char *) {}
void qr2_buffer_add_int(qr2_buffer_t, int) {}
void qr2_keybuffer_add(qr2_keybuffer_t, int) {}

const char *SBServerGetStringValue(SBServer, const char *, const char *def) { return def; }
int SBServerGetIntValue(SBServer, const char *, int idefault) { return idefault; }
const char *SBServerGetPlayerStringValue(SBServer, int, const char *, const char *sdefault) { return sdefault; }
int SBServerGetPlayerIntValue(SBServer, int, const char *, int idefault) { return idefault; }
unsigned int SBServerGetPublicInetAddress(SBServer) { return 0; }
unsigned int SBServerGetPrivateInetAddress(SBServer) { return 0; }
unsigned short SBServerGetPrivateQueryPort(SBServer) { return 0; }
int SBServerHasBasicKeys(SBServer) { return 0; }
int SBServerHasFullKeys(SBServer) { return 0; }
void SBServerEnumKeys(SBServer, SBServerKeyEnumFn, void *) {}

void chatSetLocalIP(unsigned int) {}

// ---- peer ----------------------------------------------------------------------------------------

PEER peerInitialize(PEERCallbacks *) { return 0; }
void peerShutdown(PEER) {}
void peerThink(PEER) {}
PEERBool peerSetTitle(PEER, const char *, const char *, const char *, const char *, int, int, PEERBool, PEERBool *, PEERBool *)
{
	return PEERFalse;
}
void peerConnect(PEER peer, const char *, int, peerNickErrorCallback, peerConnectCallback connectCallback, void *param, PEERBool)
{
	// Report the failure the way the SDK would, so the game shows "could not connect".
	if (connectCallback)
		connectCallback(peer, PEERFalse, param);
}
void peerRetryWithNick(PEER, const char *) {}
void peerDisconnect(PEER) {}
PEERBool peerIsConnected(PEER) { return PEERFalse; }
unsigned int peerGetLocalIP(PEER) { return 0; }
void peerAuthenticateCDKey(PEER, const char *, peerAuthenticateCDKeyCallback, void *, PEERBool) {}
void peerParseQuery(PEER, char *, int, struct sockaddr *) {}

void peerJoinTitleRoom(PEER, const char *, peerJoinRoomCallback, void *, PEERBool) {}
void peerJoinGroupRoom(PEER, int, peerJoinRoomCallback, void *, PEERBool) {}
void peerJoinStagingRoom(PEER, SBServer, const char *, peerJoinRoomCallback, void *, PEERBool) {}
void peerCreateStagingRoomWithSocket(PEER, const char *, int, const char *, SOCKET, unsigned short, peerJoinRoomCallback, void *, PEERBool) {}
void peerLeaveRoom(PEER, RoomType, const char *) {}
void peerListGroupRooms(PEER, const char *, peerListGroupRoomsCallback, void *, PEERBool) {}
void peerStartListingGames(PEER, const unsigned char *, int, const char *, peerListingGamesCallback, void *) {}
void peerStopListingGames(PEER) {}
void peerUpdateGame(PEER, SBServer, PEERBool) {}

void peerMessageRoom(PEER, RoomType, const char *, MessageType) {}
void peerMessagePlayer(PEER, const char *, const char *, MessageType) {}
void peerUTMRoom(PEER, RoomType, const char *, const char *, PEERBool) {}
void peerUTMPlayer(PEER, const char *, const char *, const char *, PEERBool) {}

void peerSetReady(PEER, PEERBool) {}
void peerStartGame(PEER, const char *, int) {}
void peerStopGame(PEER) {}
void peerStateChanged(PEER) {}
void peerStateChange(PEER) {}

void peerSetGlobalKeys(PEER, int, const char **, const char **) {}
void peerSetGlobalWatchKeys(PEER, RoomType, int, const char **, PEERBool) {}
const char *peerGetGlobalWatchKey(PEER, const char *, const char *) { return 0; }
void peerSetRoomKeys(PEER, RoomType, const char *, int, const char **, const char **) {}
void peerSetRoomWatchKeys(PEER, RoomType, int, const char **, PEERBool) {}
void peerGetRoomKeys(PEER, RoomType, const char *, int, const char **, peerGetRoomKeysCallback, void *, PEERBool) {}

void peerEnumPlayers(PEER, RoomType, peerEnumPlayersCallback, void *) {}
void peerGetPlayerProfileID(PEER, const char *, peerGetPlayerProfileIDCallback, void *, PEERBool) {}
PEERBool peerGetPlayerInfoNoWait(PEER, const char *, unsigned int *, int *) { return PEERFalse; }
PEERBool peerGetPlayerFlags(PEER, const char *, RoomType, int *flags) { if (flags) *flags = 0; return PEERFalse; }

// ---- presence (GP) -------------------------------------------------------------------------------

GPResult gpInitialize(GPConnection *connection, int) { if (connection) *connection = 0; return GP_NO_ERROR; }
void gpDestroy(GPConnection *) {}
GPResult gpProcess(GPConnection *) { return GP_NO_ERROR; }
GPResult gpSetCallback(GPConnection *, GPEnum, GPCallback, void *) { return GP_NO_ERROR; }
GPResult gpConnect(GPConnection *, const char *, const char *, const char *, GPEnum, GPEnum, GPCallback, void *) { return GP_NETWORK_ERROR; }
GPResult gpConnectNewUser(GPConnection *, const char *, const char *, const char *, GPEnum, GPEnum, GPCallback, void *) { return GP_NETWORK_ERROR; }
void gpDisconnect(GPConnection *) {}
GPResult gpIsConnected(GPConnection *, GPEnum *connected) { if (connected) *connected = GP_NOT_CONNECTED; return GP_NO_ERROR; }
GPResult gpDeleteProfile(GPConnection *) { return GP_NETWORK_ERROR; }
GPResult gpSetInfoMask(GPConnection *, GPEnum) { return GP_NETWORK_ERROR; }
GPResult gpGetInfo(GPConnection *, GPProfile, GPEnum, GPEnum, GPCallback, void *) { return GP_NETWORK_ERROR; }
GPResult gpSetStatus(GPConnection *, GPEnum, const char *, const char *) { return GP_NETWORK_ERROR; }
GPResult gpSendBuddyMessage(GPConnection *, GPProfile, const char *) { return GP_NETWORK_ERROR; }
GPResult gpSendBuddyRequest(GPConnection *, GPProfile, const char *) { return GP_NETWORK_ERROR; }
GPResult gpAuthBuddyRequest(GPConnection *, GPProfile) { return GP_NETWORK_ERROR; }
GPResult gpDenyBuddyRequest(GPConnection *, GPProfile) { return GP_NETWORK_ERROR; }
GPResult gpDeleteBuddy(GPConnection *, GPProfile) { return GP_NETWORK_ERROR; }
GPResult gpGetBuddyIndex(GPConnection *, GPProfile, int *index) { if (index) *index = -1; return GP_PARAMETER_ERROR; }
GPResult gpGetBuddyStatus(GPConnection *, int, GPBuddyStatus *status)
{
	if (status) memset(status, 0, sizeof(*status));
	return GP_PARAMETER_ERROR;
}

// ---- HTTP ----------------------------------------------------------------------------------------

void ghttpStartup(void) {}
void ghttpCleanup(void) {}
void ghttpThink(void) {}
GHTTPBool ghttpSetProxy(const char *) { return GHTTPTrue; }
GHTTPRequest ghttpGet(const char *, GHTTPBool, ghttpCompletedCallback, void *) { return -1; }
GHTTPRequest ghttpHead(const char *, GHTTPBool, ghttpCompletedCallback, void *) { return -1; }
const char *ghttpGetHeaders(GHTTPRequest) { return ""; }

// ---- persistent storage --------------------------------------------------------------------------

char gcd_gamename[256];
char gcd_secret_key[256];
int NewGame(int) { return 0; }
int SendGameSnapShot(void *, const char *, int) { return GE_NOCONNECT; }
void FreeGame(void *) {}

int InitStatsConnection(int) { return GE_NOCONNECT; }
int IsStatsConnected(void) { return 0; }
void CloseStatsConnection(void) {}
int PersistThink(void) { return 0; }
char *GetChallenge(void *) { static char empty[1] = { 0 }; return empty; }
char *GenerateAuth(const char *, char *, char response[33]) { response[0] = 0; return response; }
void PreAuthenticatePlayerPM(int, int, char *, PersAuthCallbackFn, void *) {}
void PreAuthenticatePlayerCD(int, const char *, char *, char *, PersAuthCallbackFn, void *) {}
void GetPersistDataValues(int, int, persisttype_t, int, const char *, PersDataCallbackFn, void *) {}
void SetPersistDataValues(int, int, persisttype_t, int, const char *, PersDataSaveCallbackFn, void *) {}

} // extern "C"
