/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the GameSpy HTTP SDK header - see GameSpy/Peer/Peer.h.

#ifndef ZH_GAMESPY_GHTTP_H
#define ZH_GAMESPY_GHTTP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int GHTTPBool;
#define GHTTPTrue	1
#define GHTTPFalse	0

typedef int GHTTPRequest;

typedef enum
{
	GHTTPSuccess, GHTTPOutOfMemory, GHTTPBufferOverflow, GHTTPParseURLFailed, GHTTPHostLookupFailed,
	GHTTPSocketFailed, GHTTPConnectFailed, GHTTPBadResponse, GHTTPRequestRejected, GHTTPUnauthorized,
	GHTTPForbidden, GHTTPFileNotFound, GHTTPServerError, GHTTPFileWriteFailed, GHTTPFileReadFailed,
	GHTTPFileIncomplete, GHTTPFileToBig, GHTTPEncryptionError, GHTTPRequestCancelled
} GHTTPResult;

typedef GHTTPBool (*ghttpCompletedCallback)(GHTTPRequest request, GHTTPResult result, char *buffer, int bufferLen, void *param);

void ghttpStartup(void);
void ghttpCleanup(void);
void ghttpThink(void);
GHTTPBool ghttpSetProxy(const char *server);
GHTTPRequest ghttpGet(const char *URL, GHTTPBool blocking, ghttpCompletedCallback completedCallback, void *param);
GHTTPRequest ghttpHead(const char *URL, GHTTPBool blocking, ghttpCompletedCallback completedCallback, void *param);
const char *ghttpGetHeaders(GHTTPRequest request);

#ifdef __cplusplus
}
#endif

#endif // ZH_GAMESPY_GHTTP_H
