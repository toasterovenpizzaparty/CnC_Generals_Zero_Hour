/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the GameSpy Presence (GP) SDK header - see GameSpy/Peer/Peer.h.

#ifndef ZH_GAMESPY_GP_H
#define ZH_GAMESPY_GP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *GPConnection;
typedef int GPProfile;

#define GP_NICK_LEN					31
#define GP_UNIQUENICK_LEN			21
#define GP_FIRSTNAME_LEN			31
#define GP_LASTNAME_LEN				31
#define GP_EMAIL_LEN				51
#define GP_PASSWORD_LEN				31
#define GP_HOMEPAGE_LEN				76
#define GP_ZIPCODE_LEN				11
#define GP_COUNTRYCODE_LEN			3
#define GP_PLACE_LEN				128
#define GP_AIMNAME_LEN				51
#define GP_REASON_LEN				1025
#define GP_STATUS_STRING_LEN		256
#define GP_LOCATION_STRING_LEN		256

typedef enum _GPEnum
{
	// callbacks
	GP_ERROR = 0, GP_RECV_BUDDY_REQUEST, GP_RECV_BUDDY_STATUS, GP_RECV_BUDDY_MESSAGE, GP_RECV_GAME_INVITE,
	GP_TRANSFER_CALLBACK, GP_RECV_BUDDY_AUTH, GP_RECV_BUDDY_REVOKE,
	// blocking
	GP_BLOCKING = 1, GP_NON_BLOCKING = 0,
	// firewall
	GP_FIREWALL = 1, GP_NO_FIREWALL = 0,
	// cache
	GP_CHECK_CACHE = 1, GP_DONT_CHECK_CACHE = 0,
	// fatal
	GP_FATAL = 1, GP_NON_FATAL = 0,
	// status
	GP_OFFLINE = 0, GP_ONLINE = 1, GP_PLAYING = 2, GP_STAGING = 3, GP_CHATTING = 4, GP_AWAY = 5,
	// info masks
	GP_MASK_NONE = 0, GP_MASK_HOMEPAGE = 1, GP_MASK_ZIPCODE = 2, GP_MASK_COUNTRYCODE = 4,
	GP_MASK_BIRTHDAY = 8, GP_MASK_SEX = 16, GP_MASK_EMAIL = 32, GP_MASK_ALL = -1,
	// connection state
	GP_CONNECTED = 1, GP_NOT_CONNECTED = 0
} GPEnum;

typedef enum _GPResult
{
	GP_NO_ERROR, GP_MEMORY_ERROR, GP_PARAMETER_ERROR, GP_NETWORK_ERROR, GP_SERVER_ERROR
} GPResult;

typedef enum _GPErrorCode
{
	GP_GENERAL = 0x0000, GP_PARSE, GP_NOT_LOGGED_IN, GP_BAD_SESSKEY, GP_DATABASE, GP_NETWORK, GP_FORCED_DISCONNECT,
	GP_CONNECTION_CLOSED,
	GP_LOGIN = 0x0100, GP_LOGIN_TIMEOUT, GP_LOGIN_BAD_NICK, GP_LOGIN_BAD_EMAIL, GP_LOGIN_BAD_PASSWORD,
	GP_LOGIN_BAD_PROFILE, GP_LOGIN_PROFILE_DELETED, GP_LOGIN_CONNECTION_FAILED, GP_LOGIN_SERVER_AUTH_FAILED,
	GP_NEWUSER = 0x0200, GP_NEWUSER_BAD_NICK, GP_NEWUSER_BAD_PASSWORD,
	GP_UPDATEUI = 0x0300, GP_UPDATEUI_BAD_EMAIL,
	GP_NEWPROFILE = 0x0400, GP_NEWPROFILE_BAD_NICK, GP_NEWPROFILE_BAD_OLD_NICK,
	GP_UPDATEPRO = 0x0500, GP_UPDATEPRO_BAD_NICK,
	GP_ADDBUDDY = 0x0600, GP_ADDBUDDY_BAD_FROM, GP_ADDBUDDY_BAD_NEW, GP_ADDBUDDY_ALREADY_BUDDY,
	GP_AUTHADD = 0x0700, GP_AUTHADD_BAD_FROM, GP_AUTHADD_BAD_SIG,
	GP_STATUS = 0x0800,
	GP_BM = 0x0900, GP_BM_NOT_BUDDY,
	GP_GETPROFILE = 0x0A00, GP_GETPROFILE_BAD_PROFILE,
	GP_DELBUDDY = 0x0B00, GP_DELBUDDY_NOT_BUDDY,
	GP_DELPROFILE = 0x0C00, GP_DELPROFILE_LAST_PROFILE,
	GP_SEARCH = 0x0D00, GP_SEARCH_CONNECTION_FAILED
} GPErrorCode;

typedef void (*GPCallback)(GPConnection *connection, void *arg, void *param);

typedef struct
{
	GPResult result;
	GPErrorCode errorCode;
	char *errorString;
	GPEnum fatal;
} GPErrorArg;

typedef struct
{
	GPResult result;
	GPProfile profile;
} GPConnectResponseArg;

typedef struct
{
	GPProfile profile;
	unsigned int date;
	char reason[GP_REASON_LEN];
} GPRecvBuddyRequestArg;

typedef struct
{
	GPProfile profile;
	unsigned int date;
	int index;
} GPRecvBuddyStatusArg;

typedef struct
{
	GPProfile profile;
	unsigned int date;
	char *message;
} GPRecvBuddyMessageArg;

typedef struct
{
	GPProfile profile;
	GPEnum status;
	char statusString[GP_STATUS_STRING_LEN];
	char locationString[GP_LOCATION_STRING_LEN];
	unsigned int ip;
	int port;
} GPBuddyStatus;

typedef struct
{
	GPResult result;
	GPProfile profile;
	char nick[GP_NICK_LEN];
	char uniquenick[GP_UNIQUENICK_LEN];
	char email[GP_EMAIL_LEN];
	char firstname[GP_FIRSTNAME_LEN];
	char lastname[GP_LASTNAME_LEN];
	char homepage[GP_HOMEPAGE_LEN];
	int icquin;
	char zipcode[GP_ZIPCODE_LEN];
	char countrycode[GP_COUNTRYCODE_LEN];
	float longitude;
	float latitude;
	char place[GP_PLACE_LEN];
	int birthday;
	int birthmonth;
	int birthyear;
	GPEnum sex;
	int publicmask;
	char aimname[GP_AIMNAME_LEN];
	int pic;
} GPGetInfoResponseArg;

// Names used by the game for its callback function types.
typedef GPCallback GPConnectCallback;
typedef GPCallback GPErrorCallback;
typedef GPCallback GPRecvBuddyRequestCallback;
typedef GPCallback GPRecvBuddyStatusCallback;
typedef GPCallback GPRecvBuddyMessageCallback;

GPResult gpInitialize(GPConnection *connection, int productID);
void gpDestroy(GPConnection *connection);
GPResult gpProcess(GPConnection *connection);
GPResult gpSetCallback(GPConnection *connection, GPEnum func, GPCallback callback, void *param);
GPResult gpConnect(GPConnection *connection, const char *nick, const char *email, const char *password,
	GPEnum firewall, GPEnum blocking, GPCallback callback, void *param);
GPResult gpConnectNewUser(GPConnection *connection, const char *nick, const char *email, const char *password,
	GPEnum firewall, GPEnum blocking, GPCallback callback, void *param);
void gpDisconnect(GPConnection *connection);
GPResult gpIsConnected(GPConnection *connection, GPEnum *connected);
GPResult gpDeleteProfile(GPConnection *connection);
GPResult gpSetInfoMask(GPConnection *connection, GPEnum mask);
GPResult gpGetInfo(GPConnection *connection, GPProfile profile, GPEnum checkCache, GPEnum blocking,
	GPCallback callback, void *param);
GPResult gpSetStatus(GPConnection *connection, GPEnum status, const char *statusString, const char *locationString);
GPResult gpSendBuddyMessage(GPConnection *connection, GPProfile profile, const char *message);
GPResult gpSendBuddyRequest(GPConnection *connection, GPProfile profile, const char *reason);
GPResult gpAuthBuddyRequest(GPConnection *connection, GPProfile profile);
GPResult gpDenyBuddyRequest(GPConnection *connection, GPProfile profile);
GPResult gpDeleteBuddy(GPConnection *connection, GPProfile profile);
GPResult gpGetBuddyIndex(GPConnection *connection, GPProfile profile, int *index);
GPResult gpGetBuddyStatus(GPConnection *connection, int index, GPBuddyStatus *status);

#ifdef __cplusplus
}
#endif

#endif // ZH_GAMESPY_GP_H
