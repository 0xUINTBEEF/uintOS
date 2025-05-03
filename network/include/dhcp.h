/**
 * @file dhcp.h
 * @brief DHCP client implementation for uintOS
 * 
 * This file defines the structures and functions for the DHCP protocol
 * implementation, allowing dynamic IP address configuration.
 */

#ifndef DHCP_H
#define DHCP_H

#include "network.h"
#include "udp.h"

/* DHCP message types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6
#define DHCP_RELEASE  7
#define DHCP_INFORM   8

/* DHCP option codes */
#define DHCP_OPT_PAD            0
#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS_SERVER     6
#define DHCP_OPT_HOSTNAME       12
#define DHCP_OPT_DOMAIN_NAME    15
#define DHCP_OPT_REQUESTED_IP   50
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_PARAM_REQ      55
#define DHCP_OPT_CLIENT_ID      61
#define DHCP_OPT_END            255

/* DHCP states */
#define DHCP_STATE_INIT         0
#define DHCP_STATE_SELECTING    1
#define DHCP_STATE_REQUESTING   2
#define DHCP_STATE_BOUND        3
#define DHCP_STATE_RENEWING     4
#define DHCP_STATE_REBINDING    5
#define DHCP_STATE_INIT_REBOOT  6
#define DHCP_STATE_REBOOTING    7

/* DHCP configuration */
#define DHCP_CLIENT_PORT        68
#define DHCP_SERVER_PORT        67
#define DHCP_MAX_OPTIONS_LEN    308

/* DHCP message structure (as per RFC 2131) */
typedef struct dhcp_message {
    uint8_t  op;                /* Message op code / message type */
    uint8_t  htype;             /* Hardware address type (Ethernet = 1) */
    uint8_t  hlen;              /* Hardware address length (6 for Ethernet) */
    uint8_t  hops;              /* Client sets to zero, used by relay agents */
    uint32_t xid;               /* Transaction ID, random number */
    uint16_t secs;              /* Seconds elapsed since client began acquisition */
    uint16_t flags;             /* Flags */
    uint32_t ciaddr;            /* Client IP address */
    uint32_t yiaddr;            /* 'Your' (client) IP address */
    uint32_t siaddr;            /* Next server IP address */
    uint32_t giaddr;            /* Relay agent IP address */
    uint8_t  chaddr[16];        /* Client hardware address */
    uint8_t  sname[64];         /* Optional server host name */
    uint8_t  file[128];         /* Boot file name */
    uint8_t  options[DHCP_MAX_OPTIONS_LEN]; /* Optional parameters field */
} __attribute__((packed)) dhcp_message_t;

/* DHCP client configuration */
typedef struct dhcp_config {
    ipv4_address_t ip_address;  /* Assigned IP address */
    ipv4_address_t subnet_mask; /* Subnet mask */
    ipv4_address_t gateway;     /* Default gateway */
    ipv4_address_t dns_server;  /* DNS server */
    uint32_t lease_time;        /* IP address lease time (in seconds) */
    uint32_t renewal_time;      /* Time until renewal */
    uint32_t rebind_time;       /* Time until rebind */
    ipv4_address_t server_id;   /* DHCP server ID */
    uint32_t last_update;       /* Last update time (for lease tracking) */
    uint8_t state;              /* Current DHCP state */
} dhcp_config_t;

/**
 * Initialize the DHCP client subsystem
 *
 * @return 0 on success, error code on failure
 */
int dhcp_init(void);

/**
 * Start DHCP discovery for the specified network device
 *
 * @param dev Network device to configure
 * @return 0 on success, error code on failure
 */
int dhcp_start(net_device_t* dev);

/**
 * Process a received DHCP packet
 *
 * @param dev Network device that received the packet
 * @param buffer Network buffer containing the DHCP packet
 * @return 0 on success, error code on failure
 */
int dhcp_process_packet(net_device_t* dev, net_buffer_t* buffer);

/**
 * Get the current DHCP configuration for a device
 *
 * @param dev Network device
 * @param config Pointer to store the configuration
 * @return 0 on success, error code on failure
 */
int dhcp_get_config(net_device_t* dev, dhcp_config_t* config);

/**
 * Release a DHCP lease
 *
 * @param dev Network device
 * @return 0 on success, error code on failure
 */
int dhcp_release(net_device_t* dev);

/**
 * DHCP client task - handles timeouts and lease renewals
 * Called periodically by the kernel
 */
void dhcp_client_task(void);

#endif /* DHCP_H */