/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * UDP listener of the server side multitransport tunnels
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <string.h>

#include <winpr/assert.h>
#include <winpr/cast.h>
#include <winpr/crt.h>
#include <winpr/synch.h>
#include <winpr/thread.h>
#include <winpr/winsock.h>

#if !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <freerdp/log.h>

#include "rdpudp.h"
#include "udplistener.h"

#define TAG FREERDP_TAG("core.udplistener")

#define UDPLISTENER_MAX_SOCKETS 2
#define UDPLISTENER_MAX_SESSIONS 64
#define UDPLISTENER_DEFAULT_PORT 3389

/* As for a client connection, see RDPUDP_RECEIVE_BUFFER: here it takes every session's data. */
#define UDPLISTENER_RECEIVE_BUFFER (4 * 1024 * 1024)

typedef struct
{
	BOOL used;
	BYTE cookieHash[32];
	udpListenerInputFn input;
	void* custom;
	BOOL bound; /* the SYN came, from addr */
	struct sockaddr_storage addr;
	size_t addrlen;
} udpListenerSession;

typedef struct
{
	CRITICAL_SECTION lock;
	char* bindAddress;
	UINT16 port;

	SOCKET sockets[UDPLISTENER_MAX_SOCKETS];
	size_t socketCount;
	HANDLE thread;

	udpListenerSession sessions[UDPLISTENER_MAX_SESSIONS];
	BYTE buffer[0x10000];
} udpListener;

static udpListener g_listener;
static INIT_ONCE g_listenerOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK udplistener_init(WINPR_ATTR_UNUSED PINIT_ONCE once,
                                      WINPR_ATTR_UNUSED PVOID param,
                                      WINPR_ATTR_UNUSED PVOID* context)
{
	memset(&g_listener, 0, sizeof(g_listener));
	g_listener.port = UDPLISTENER_DEFAULT_PORT;
	for (size_t x = 0; x < UDPLISTENER_MAX_SOCKETS; x++)
		g_listener.sockets[x] = INVALID_SOCKET;
	return InitializeCriticalSectionAndSpinCount(&g_listener.lock, 4000);
}

WINPR_ATTR_NODISCARD
static udpListener* udplistener_get(void)
{
	if (!InitOnceExecuteOnce(&g_listenerOnce, udplistener_init, nullptr, nullptr))
		return nullptr;
	return &g_listener;
}

WINPR_ATTR_NODISCARD
static BOOL sockaddr_equal(const struct sockaddr_storage* a, const struct sockaddr* b, size_t blen)
{
	WINPR_ASSERT(a);
	WINPR_ASSERT(b);
	if (a->ss_family != b->sa_family)
		return FALSE;
	if (a->ss_family == AF_INET)
	{
		const struct sockaddr_in* x = (const struct sockaddr_in*)a;
		const struct sockaddr_in* y = (const struct sockaddr_in*)b;
		return (blen >= sizeof(*y)) && (x->sin_port == y->sin_port) &&
		       (memcmp(&x->sin_addr, &y->sin_addr, sizeof(x->sin_addr)) == 0);
	}
	if (a->ss_family == AF_INET6)
	{
		const struct sockaddr_in6* x = (const struct sockaddr_in6*)a;
		const struct sockaddr_in6* y = (const struct sockaddr_in6*)b;
		return (blen >= sizeof(*y)) && (x->sin6_port == y->sin6_port) &&
		       (memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(x->sin6_addr)) == 0);
	}
	return FALSE;
}

/* Called with the lock held. */
static void udplistener_dispatch(udpListener* listener, SOCKET sockfd, const struct sockaddr* addr,
                                 size_t addrlen, const BYTE* data, size_t length)
{
	WINPR_ASSERT(listener);

	udpListenerSession* session = nullptr;
	for (size_t x = 0; x < UDPLISTENER_MAX_SESSIONS; x++)
	{
		udpListenerSession* cur = &listener->sessions[x];
		if (cur->used && cur->bound && sockaddr_equal(&cur->addr, addr, addrlen))
		{
			session = cur;
			break;
		}
	}

	if (!session)
	{
		BYTE hash[32] = WINPR_C_ARRAY_INIT;
		if (!rdpudp_get_syn_cookie_hash(data, length, hash))
		{
			WLog_DBG(TAG, "dropping a datagram from an unknown client");
			return;
		}
		for (size_t x = 0; x < UDPLISTENER_MAX_SESSIONS; x++)
		{
			udpListenerSession* cur = &listener->sessions[x];
			if (cur->used && !cur->bound && (memcmp(cur->cookieHash, hash, sizeof(hash)) == 0))
			{
				session = cur;
				break;
			}
		}
		if (!session)
		{
			WLog_INFO(TAG, "dropping a SYN whose cookie hash matches no session");
			return;
		}
		if (addrlen > sizeof(session->addr))
			return;
		memcpy(&session->addr, addr, addrlen);
		session->addrlen = addrlen;
		session->bound = TRUE;
	}

	if (!session->input(session->custom, sockfd, addr, addrlen, data, length))
		memset(session, 0, sizeof(*session));
}

/* Waits up to timeout milliseconds for a socket to become readable. */
static void udplistener_wait(const udpListener* listener, UINT32 timeout)
{
	WINPR_ASSERT(listener);

	fd_set rset;
	FD_ZERO(&rset);
	SOCKET highest = 0;
	for (size_t x = 0; x < listener->socketCount; x++)
	{
		const SOCKET sockfd = listener->sockets[x];
#if !defined(_WIN32)
		if (sockfd >= FD_SETSIZE)
			continue;
#endif
		FD_SET(sockfd, &rset);
		if (sockfd > highest)
			highest = sockfd;
	}
	struct timeval tv = WINPR_C_ARRAY_INIT;
	tv.tv_sec = WINPR_ASSERTING_INT_CAST(long, timeout / 1000);
	tv.tv_usec = WINPR_ASSERTING_INT_CAST(long, (timeout % 1000) * 1000);
#if defined(_WIN32)
	(void)select(0, &rset, nullptr, nullptr, &tv);
#else
	(void)select(WINPR_ASSERTING_INT_CAST(int, highest) + 1, &rset, nullptr, nullptr, &tv);
#endif
}

WINPR_ATTR_NODISCARD
static DWORD WINAPI udplistener_thread(LPVOID arg)
{
	udpListener* listener = arg;
	WINPR_ASSERT(listener);

	while (TRUE)
	{
		udplistener_wait(listener, 500);

		EnterCriticalSection(&listener->lock);
		for (size_t x = 0; x < listener->socketCount; x++)
		{
			const SOCKET sockfd = listener->sockets[x];
			while (TRUE)
			{
				struct sockaddr_storage addr = WINPR_C_ARRAY_INIT;
				int addrlen = sizeof(addr);
				const int rc = _recvfrom(sockfd, (char*)listener->buffer,
				                         WINPR_ASSERTING_INT_CAST(int, sizeof(listener->buffer)), 0,
				                         (struct sockaddr*)&addr, &addrlen);
				if (rc < 0)
					break; /* nothing left, or an ICMP error for an earlier datagram */
				udplistener_dispatch(listener, sockfd, (const struct sockaddr*)&addr,
				                     WINPR_ASSERTING_INT_CAST(size_t, addrlen), listener->buffer,
				                     WINPR_ASSERTING_INT_CAST(size_t, rc));
			}
		}
		LeaveCriticalSection(&listener->lock);
	}

	ExitThread(0);
	return 0;
}

/* Called with the lock held. */
WINPR_ATTR_NODISCARD
static BOOL udplistener_open(udpListener* listener)
{
	WINPR_ASSERT(listener);

	if (listener->thread)
		return TRUE;

	char port[16] = WINPR_C_ARRAY_INIT;
	(void)_snprintf(port, sizeof(port), "%" PRIu16, listener->port);
	struct addrinfo hints = WINPR_C_ARRAY_INIT;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = listener->bindAddress ? 0 : AI_PASSIVE;
	struct addrinfo* result = nullptr;
	if ((getaddrinfo(listener->bindAddress, port, &hints, &result) != 0) || !result)
	{
		WLog_ERR(TAG, "cannot resolve the UDP listening address");
		return FALSE;
	}

	for (struct addrinfo* ai = result;
	     ai && (listener->socketCount < UDPLISTENER_MAX_SOCKETS); ai = ai->ai_next)
	{
		if ((ai->ai_family != AF_INET) && (ai->ai_family != AF_INET6))
			continue;

		SOCKET sockfd = _socket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);
		if (sockfd == INVALID_SOCKET)
			continue;

		const int on = 1;
		if ((ai->ai_family == AF_INET6) &&
		    (_setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on)) != 0))
			WLog_WARN(TAG, "setsockopt() IPV6_V6ONLY failed");
		if (_setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0)
			WLog_WARN(TAG, "setsockopt() SO_REUSEADDR failed");
		/* a smaller buffer only costs resends, see RDPUDP_RECEIVE_BUFFER */
		const UINT32 rcvbuf = UDPLISTENER_RECEIVE_BUFFER;
		if (_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf)) != 0)
			WLog_WARN(TAG, "setsockopt() SO_RCVBUF failed");

		u_long nonBlocking = 1;
		if ((_bind(sockfd, ai->ai_addr, WINPR_ASSERTING_INT_CAST(int, ai->ai_addrlen)) != 0) ||
		    (_ioctlsocket(sockfd, FIONBIO, &nonBlocking) != 0))
		{
			closesocket(sockfd);
			continue;
		}

		listener->sockets[listener->socketCount++] = sockfd;
		WLog_INFO(TAG, "listening for RDP-UDP on %s port %" PRIu16,
		          (ai->ai_family == AF_INET6) ? "IPv6" : "IPv4", listener->port);
	}
	freeaddrinfo(result);

	if (listener->socketCount == 0)
	{
		WLog_ERR(TAG, "cannot open a UDP socket on port %" PRIu16, listener->port);
		return FALSE;
	}

	/* runs for the life of the process, like the sockets it reads */
	listener->thread = CreateThread(nullptr, 0, udplistener_thread, listener, 0, nullptr);
	return listener->thread != nullptr;
}

void udplistener_set_address(const char* bindAddress, UINT16 port)
{
	udpListener* listener = udplistener_get();
	if (!listener)
		return;

	EnterCriticalSection(&listener->lock);
	if (!listener->thread)
	{
		free(listener->bindAddress);
		listener->bindAddress = bindAddress ? _strdup(bindAddress) : nullptr;
		listener->port = port;
	}
	LeaveCriticalSection(&listener->lock);
}

BOOL udplistener_register(const BYTE cookieHash[32], udpListenerInputFn input, void* custom)
{
	WINPR_ASSERT(cookieHash);
	WINPR_ASSERT(input);
	WINPR_ASSERT(custom);

	udpListener* listener = udplistener_get();
	if (!listener)
		return FALSE;

	BOOL rc = FALSE;
	EnterCriticalSection(&listener->lock);
	if (udplistener_open(listener))
	{
		for (size_t x = 0; x < UDPLISTENER_MAX_SESSIONS; x++)
		{
			udpListenerSession* session = &listener->sessions[x];
			if (session->used)
				continue;
			memset(session, 0, sizeof(*session));
			session->used = TRUE;
			memcpy(session->cookieHash, cookieHash, sizeof(session->cookieHash));
			session->input = input;
			session->custom = custom;
			rc = TRUE;
			break;
		}
		if (!rc)
			WLog_WARN(TAG, "too many sessions waiting for or using RDP-UDP");
	}
	LeaveCriticalSection(&listener->lock);
	return rc;
}

void udplistener_unregister(void* custom)
{
	udpListener* listener = udplistener_get();
	if (!listener || !custom)
		return;

	EnterCriticalSection(&listener->lock);
	for (size_t x = 0; x < UDPLISTENER_MAX_SESSIONS; x++)
	{
		udpListenerSession* session = &listener->sessions[x];
		if (session->used && (session->custom == custom))
			memset(session, 0, sizeof(*session));
	}
	LeaveCriticalSection(&listener->lock);
}
