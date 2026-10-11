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

#ifndef FREERDP_LIB_CORE_UDPLISTENER_H
#define FREERDP_LIB_CORE_UDPLISTENER_H

#include <winpr/wtypes.h>
#include <winpr/winsock.h>

#include <freerdp/api.h>

/** @brief The UDP side of a server's listening port.
 *
 * Clients send their RDP-UDP SYN to the address and port of the TCP listener ([MS-RDPEMT] 1.3).
 * One socket per address family takes every session's datagrams. A version 3 SYN carries the
 * SHA-256 hash of the security cookie its Initiate Multitransport Request handed out, which says
 * what session it is for; after that the client's address does.
 *
 * The sockets open when the first session registers, on the address and port the TCP listener
 * was opened on.
 */

/** Called on the listener's thread with one datagram from the client of a registered session.
 *  The first one is the client's SYN. Returning FALSE drops the session's registration. */
typedef BOOL (*udpListenerInputFn)(void* custom, SOCKET sockfd, const struct sockaddr* addr,
                                   size_t addrlen, const BYTE* data, size_t length);

/** Remembers where the TCP listener listens, the UDP sockets open on the same address and port. */
FREERDP_LOCAL void udplistener_set_address(const char* bindAddress, UINT16 port);

/** Waits for the client whose SYN carries cookieHash. Opens the sockets if they are not open. */
WINPR_ATTR_NODISCARD
FREERDP_LOCAL BOOL udplistener_register(const BYTE cookieHash[32], udpListenerInputFn input,
                                        void* custom);

/** Removes a registration. Once it returns, input is not called for custom any more. */
FREERDP_LOCAL void udplistener_unregister(void* custom);

#endif /* FREERDP_LIB_CORE_UDPLISTENER_H */
