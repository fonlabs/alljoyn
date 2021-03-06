/**
 * @file ArdpProtocol is an implementation of the Reliable Datagram Protocol
 * (RDP) adapted to AllJoyn.
 */

/******************************************************************************
 * Copyright (c) 2014, AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#ifndef _ALLJOYN_ARDP_PROTOCOL_H
#define _ALLJOYN_ARDP_PROTOCOL_H

#include <alljoyn/Status.h>

#include <qcc/platform.h>
#include <qcc/IPAddress.h>
#include <qcc/Socket.h>

namespace ajn {

const uint32_t ARDP_NO_TIMEOUT = 0xffffffff; /**< To indicate that no timed actions are pending */

const uint16_t ARDP_SEGBMAX = 65535 - 20 - 8; /**< Maximum size of a datagram (UDP payload minus IP header size minus UDP header size */
const uint16_t ARDP_SEGMAX = 16;              /**< Max number of segments in flight. */

const uint32_t ARDP_CONN_ID_INVALID = 0xffffffff; /* To indicate invalid connection */

/**
 * @brief Per-protocol-instance (global) configuration variables.
 */
typedef struct {
    uint32_t connectTimeout;  /**< udp_connect_timeout configuration variable */
    uint32_t connectRetries;  /**< udp_connect_retries configuration variable */
    uint32_t dataTimeout;     /**< udp_data_timeout configuration variable */
    uint32_t dataRetries;     /**< udp_data_retries configuration variable */
    uint32_t persistTimeout;  /**< udp_persist_timeout configuration variable */
    uint32_t persistRetries;  /**< udp_persist_retries configuration variable */
    uint32_t probeTimeout;    /**< udp_probe_timeout configuration variable */
    uint32_t probeRetries;    /**< udp_probe_retries configuration variable */
    uint32_t dupackCounter;   /**< udp_dupack_counter configuration variable */
    uint32_t timewait;        /**< udp_timewait configuration variable */
} ArdpGlobalConfig;

/**
 * @brief This is the format of an RDP datagram header on the wire.
 *
 * This is expected to be sent in "network byte order" AKA big endian.  Use the
 * usual htonl(), ntohl() functions.
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t flags;      /**< See Control flag definitions above */
    uint8_t hlen;       /**< Length of the header in units of two octets (number of uint16_t) */
    uint16_t src;       /**< Used to distinguish between multiple connections on the local side. */
    uint16_t dst;       /**< Used to distinguish between multiple connections on the foreign side. */
    uint16_t dlen;      /**< The length of the data in the current segment.  Does not include the header size. */
    uint32_t seq;       /**< The sequence number of the current segment. */
    uint32_t ack;       /**< The number of the segment that the sender of this segment last received correctly and in sequence. */
    uint32_t ttl;       /**< Time-to-live.  Zero means forever. */
    uint32_t lcs;       /**< Last "in-order" consumed segment. */
    uint32_t acknxt;    /**< First unexpired segment, TTL accounting */
    uint32_t som;       /**< Start sequence number for fragmented message */
    uint16_t fcnt;      /**< Number of segments comprising fragmented message */
} ArdpHeader;
#pragma pack(pop)

/* Maximum header length, implied by the fact that hlen filed is 8 bits */
static const uint16_t ARDP_MAX_HEADER_LEN = 255;

/* Length of fixed part of the header (sans EACKs), see below ArdpHeader structure */
static const uint16_t ARDP_FIXED_HEADER_LEN = sizeof(ArdpHeader);

/* Max size of  EACK mask (in 32-bit units) */
const uint8_t ARDP_MAX_EACK_MASK_SZ = (ARDP_MAX_HEADER_LEN - ARDP_FIXED_HEADER_LEN) >> 2;

/* Max number (times 32) of outstanding unacked segments the protocol allows per connection. */
const uint32_t ARDP_MAX_EACK = ARDP_MAX_EACK_MASK_SZ * 32;

const uint16_t ARDP_USRBMAX = (uint16_t)(ARDP_SEGBMAX - sizeof(ArdpHeader)); /**< Maximum size of an ARDP user datagram */

typedef struct ARDP_CONN_RECORD ArdpConnRecord;

#define ARDP_FLAG_SYN  0x01    /**< Control flag. Request to open a connection.  Must be separate segment. */
#define ARDP_FLAG_ACK  0x02    /**< Control flag. Acknowledge a segment. May accompany message */
#define ARDP_FLAG_EACK 0x04    /**< Control flag. Non-cumulative (extended) acknowledgement */
#define ARDP_FLAG_RST  0x08    /**< Control flag. Reset this connection. Must be separate segment. */
#define ARDP_FLAG_NUL  0x10    /**< Control flag. Null (zero-length) segment.  Must have zero data length but may share ACK and EACK info. */
#define ARDP_FLAG_VER  0x40    /**< Control flag. Bits 6-7 of flags byte.  Current version is (1) */
#define ARDP_FLAG_SDM  0x0001  /*<< Sequenced delivery mode option. Indicates in-order sequence delivery is in force. */

typedef struct ARDP_HANDLE ArdpHandle;

typedef struct ARDP_RCV_BUFFER {
    uint32_t seq;          /**< Sequence number */
    uint32_t datalen;      /**< Data payload size */
    uint8_t* data;         /**< Pointer to data payload */
    ARDP_RCV_BUFFER* next; /**< Pointer to the next buffer */
    bool inUse;            /**< Flag indicating that the buffer is occupied, but not delivered to the upper layer (fragment) */
    bool isDelivered;      /**< Flag indicating that the buffer is not delivered to the upper layer */
    uint32_t som;
    uint16_t fcnt;
    uint32_t ttl;          /**< Remaining time to live of the AllJoyn message */
    uint32_t tRecv;        /**< Local time at which the segment was received (used for TTL calcs) */
} ArdpRcvBuf;

typedef void (*ARDP_CONNECT_CB)(ArdpHandle* handle, ArdpConnRecord* conn, bool passive, uint8_t* buf, uint16_t len, QStatus status);
typedef void (*ARDP_DISCONNECT_CB)(ArdpHandle* handle, ArdpConnRecord* conn, QStatus status);
typedef bool (*ARDP_ACCEPT_CB)(ArdpHandle* handle, qcc::IPAddress ipAddr, uint16_t ipPort, ArdpConnRecord* conn, uint8_t* buf, uint16_t len, QStatus status);
typedef void (*ARDP_RECV_CB)(ArdpHandle* handle, ArdpConnRecord* conn, ArdpRcvBuf* rcvbufs, QStatus status);
typedef void (*ARDP_SEND_CB)(ArdpHandle* handle, ArdpConnRecord* conn, uint8_t* buf, uint32_t len, QStatus status);
typedef void (*ARDP_SEND_WINDOW_CB)(ArdpHandle* handle, ArdpConnRecord* conn, uint16_t window, QStatus status);

typedef struct {
    ARDP_ACCEPT_CB AcceptCb;          /**< Called when new connection comes in */
    ARDP_CONNECT_CB ConnectCb;        /**< Called when new connection transitions to OPEN state */
    ARDP_DISCONNECT_CB DisconnectCb;  /**< Called when connection goes to CLOSE_WAIT state */
    ARDP_RECV_CB RecvCb;              /**< Called when new data arrives */
    ARDP_SEND_CB SendCb;              /**< Called when the buffer is sent off into the network */
    ARDP_SEND_WINDOW_CB SendWindowCb; /**< Called when the send window changes for some reason */
} ArdpCallbacks;

/**
 * @brief The basic ARDP API.
 *
 * The ARDP is an asynchronous-sockets-like API.
 *
 *     - Connect is the active version of open.  Connect begins the SYN,
 *       SYN-ACK, ACK thre-way handshake.  The connect callback is fired when
 *       the outgoing connection enters the OPEN state.
 *
 *     - Accept is the passive version of open.  Accept simply enables accepting
 *       connection requests to do the three-way handshake.  The accept callback
 *       is fired when the incoming connection enters the OPEN state.
 *
 *     - Disconnect is used to close both Active and Passive connections.  The
 *       disconnect callback is fired when the connection enters the CLOSE_WAIT
 *       state.  Disconnect can be started by either end of the connection.
 *
 *     - Send is an asynchronous send that is complete when the write callback
 *       is fired.  Send data is not buffered if the send window does not allow
 *       immediate transmission.  This is to avoid buffering large numbers of
 *       messages that may have a short TTL.  the client should check TTL
 *       immediately before passing data into ARDP.  Returning from the send
 *       callback has the side effect of opening the send window (telling the
 *       other side it can send more data).  There is a separate helper callback
 *       that fires whenever the send window changes to allow triggering of
 *       sends.
 *
 *     - Recv is an asynchronous read that is complete when the receive callback
 *       is fired.  Calling receive with a buffer has the side-effect of
 *       widening the receive window (and telling the other side it can send
 *       data).  Returning from the read callback has the effect of
 *       acknowledging the underlying segment that was received (allowing the
 *       other side to send more data).
 *
 * Call-in functions are expected to be set by the ARDP protocol and call-out functions are
 * expected to be set by the client.  Since this code is expected to run on embedded
 * systems, we don't protect the API using class mechanisms, we just trust that since
 * it is we who are implementing the client, we'll do it correctly.
 */
ArdpHandle* ARDP_AllocHandle(ArdpGlobalConfig* config);
void ARDP_FreeHandle(ArdpHandle* handle);
void ARDP_SetHandleContext(ArdpHandle* handle, void* context);
void* ARDP_GetHandleContext(ArdpHandle* handle);
void ARDP_ReleaseConn(ArdpHandle* handle, ArdpConnRecord* conn);
QStatus ARDP_SetConnContext(ArdpHandle* handle, ArdpConnRecord* conn, void* context);
void* ARDP_GetConnContext(ArdpHandle* handle, ArdpConnRecord* conn);
uint32_t ARDP_GetConnId(ArdpHandle* handle, ArdpConnRecord* conn);
uint32_t ARDP_GetConnPending(ArdpHandle* handle, ArdpConnRecord* conn);
qcc::IPAddress ARDP_GetIpAddrFromConn(ArdpHandle* handle, ArdpConnRecord* conn);
uint16_t ARDP_GetIpPortFromConn(ArdpHandle* handle, ArdpConnRecord* conn);
QStatus ARDP_Run(ArdpHandle* handle, qcc::SocketFd sock, bool socketReady, uint32_t* ms);
QStatus ARDP_StartPassive(ArdpHandle* handle);
QStatus ARDP_Accept(ArdpHandle* handle, ArdpConnRecord* conn, uint16_t segmax, uint16_t segbmax, uint8_t* buf, uint16_t len);
QStatus ARDP_Acknowledge(ArdpHandle* handle, ArdpConnRecord* conn, uint8_t* buf, uint16_t len);
void ARDP_SetAcceptCb(ArdpHandle* handle, ARDP_ACCEPT_CB AcceptCb);
QStatus ARDP_Connect(ArdpHandle* handle, qcc::SocketFd sock, qcc::IPAddress ipAddr, uint16_t ipPort, uint16_t segmax, uint16_t segbmax, ArdpConnRecord** conn, uint8_t* buf, uint16_t len, void* context);
void ARDP_SetConnectCb(ArdpHandle* handle, ARDP_CONNECT_CB ConnectCb);
QStatus ARDP_Disconnect(ArdpHandle* handle, ArdpConnRecord* conn);
void ARDP_SetDisconnectCb(ArdpHandle* handle, ARDP_DISCONNECT_CB DisconnectCb);
void ARDP_ReleaseConnection(ArdpHandle* handle, ArdpConnRecord* conn);
QStatus ARDP_RecvReady(ArdpHandle* handle, ArdpConnRecord* conn, ArdpRcvBuf* rcvbuf);
void ARDP_SetRecvCb(ArdpHandle* handle, ARDP_RECV_CB RecvCb);
QStatus ARDP_Send(ArdpHandle* handle, ArdpConnRecord* conn, uint8_t* buf, uint32_t len, uint32_t ttl);
void ARDP_SetSendCb(ArdpHandle* handle, ARDP_SEND_CB SendCb);
void ARDP_SetSendWindowCb(ArdpHandle* handle, ARDP_SEND_WINDOW_CB SendWindowCb);

} // namespace ajn

#endif // _ALLJOYN_ARDP_PROTOCOL_H
