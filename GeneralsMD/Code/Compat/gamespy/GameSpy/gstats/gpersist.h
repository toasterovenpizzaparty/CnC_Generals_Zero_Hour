/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the GameSpy persistent storage SDK header - see GameSpy/Peer/Peer.h.

#ifndef ZH_GAMESPY_GPERSIST_H
#define ZH_GAMESPY_GPERSIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { pd_private_ro, pd_private_rw, pd_public_ro, pd_public_rw } persisttype_t;

// ---- gstats (game result reporting) ----
#define GE_NOERROR		0
#define GE_NODNS		1
#define GE_NOSOCKET		2
#define GE_NOCONNECT	3
#define GE_BUSY			4
#define GE_DATAERROR	5
#define SNAP_UPDATE		0
#define SNAP_FINAL		1
extern char gcd_gamename[256];
extern char gcd_secret_key[256];
int NewGame(int usebuckets);
int SendGameSnapShot(void *game, const char *snapshot, int final);
void FreeGame(void *game);

typedef void (*PersAuthCallbackFn)(int localid, int profileid, int authenticated, char *errmsg, void *instance);
typedef void (*PersDataCallbackFn)(int localid, int profileid, persisttype_t type, int index, int success, char *data, int len, void *instance);
typedef void (*PersDataSaveCallbackFn)(int localid, int profileid, persisttype_t type, int index, int success, void *instance);

int InitStatsConnection(int gameport);
int IsStatsConnected(void);
void CloseStatsConnection(void);
int PersistThink(void);
char *GetChallenge(void *game);
char *GenerateAuth(const char *challenge, char *password, char response[33]);
void PreAuthenticatePlayerPM(int localid, int profileid, char *challengeresponse, PersAuthCallbackFn callback, void *instance);
void PreAuthenticatePlayerCD(int localid, const char *nick, char *keyhash, char *challengeresponse, PersAuthCallbackFn callback, void *instance);
void GetPersistDataValues(int localid, int profileid, persisttype_t type, int index, const char *keys, PersDataCallbackFn callback, void *instance);
void SetPersistDataValues(int localid, int profileid, persisttype_t type, int index, const char *keyvalues, PersDataSaveCallbackFn callback, void *instance);

#ifdef __cplusplus
}
#endif

#endif // ZH_GAMESPY_GPERSIST_H
