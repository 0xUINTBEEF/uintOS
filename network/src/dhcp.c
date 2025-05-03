/**
 * @file dhcp.c
 * @brief DHCP client implementation for uintOS
 */

#include "../include/dhcp.h"
#include "../include/udp.h"
#include "../../kernel/logging/log.h"
#include "../../hal/include/hal_timer.h"
#include <string.h>

/* DHCP message opcodes */
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2

/* DHCP magic cookie (RFC 1497) */
#define DHCP_MAGIC_COOKIE 0x63825363

/* DHCP configuration storage - one per network device */
static dhcp_config_t dhcp_configs[NET_MAX_DEVICES];

/* Transaction ID for DHCP requests */
static uint32_t current_xid = 0;

/* Forward declarations */
static int dhcp_send_discover(net_device_t* dev);
static int dhcp_send_request(net_device_t* dev, const ipv4_address_t* requested_ip, const ipv4_address_t* server_id);
static int dhcp_send_release(net_device_t* dev);
static int dhcp_parse_options(dhcp_message_t* msg, dhcp_config_t* config);
static void dhcp_apply_config(net_device_t* dev, dhcp_config_t* config);
static uint32_t dhcp_get_xid(void);

/**
 * Initialize the DHCP client subsystem
 */
int dhcp_init(void) {
    LOG(LOG_INFO, "Initializing DHCP client");
    memset(dhcp_configs, 0, sizeof(dhcp_configs));
    
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        dhcp_configs[i].state = DHCP_STATE_INIT;
    }

    // Register UDP port for DHCP client
    // TODO: Implement UDP port registration when available
    
    return NET_ERR_OK;
}

/**
 * Start DHCP discovery for the specified network device
 */
int dhcp_start(net_device_t* dev) {
    if (!dev) {
        return NET_ERR_INVALID;
    }

    int dev_id = -1;
    
    // Find the device's config slot
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            dev_id = i;
            break;
        } else if (dhcp_configs[i].name[0] == '\0' && dev_id == -1) {
            // Use first empty slot if not found
            dev_id = i;
        }
    }
    
    if (dev_id == -1) {
        LOG(LOG_ERROR, "DHCP: No free slots for device %s", dev->name);
        return NET_ERR_NOMEM;
    }
    
    // Initialize the config
    memset(&dhcp_configs[dev_id], 0, sizeof(dhcp_config_t));
    strncpy(dhcp_configs[dev_id].name, dev->name, sizeof(dhcp_configs[dev_id].name) - 1);
    dhcp_configs[dev_id].state = DHCP_STATE_INIT;
    dhcp_configs[dev_id].last_update = hal_get_time_ms();
    
    // Send DHCP DISCOVER
    LOG(LOG_INFO, "DHCP: Starting discovery for %s", dev->name);
    return dhcp_send_discover(dev);
}

/**
 * Send a DHCP DISCOVER message
 */
static int dhcp_send_discover(net_device_t* dev) {
    net_buffer_t* buffer = net_buffer_alloc(sizeof(dhcp_message_t), 0);
    if (!buffer) {
        return NET_ERR_NOMEM;
    }
    
    dhcp_message_t* msg = (dhcp_message_t*)buffer->data;
    memset(msg, 0, sizeof(dhcp_message_t));
    
    // Fill in standard fields
    msg->op = DHCP_BOOTREQUEST;
    msg->htype = 1;  // Ethernet
    msg->hlen = 6;   // MAC address length
    msg->xid = dhcp_get_xid();
    
    // Copy MAC address to chaddr
    memcpy(msg->chaddr, &dev->mac, 6);
    
    // Set up options
    uint8_t* opts = msg->options;
    
    // Magic cookie
    *(uint32_t*)opts = htonl(DHCP_MAGIC_COOKIE);
    opts += 4;
    
    // Message type: DISCOVER
    *opts++ = DHCP_OPT_MSG_TYPE;
    *opts++ = 1;  // Length
    *opts++ = DHCP_DISCOVER;
    
    // Client identifier
    *opts++ = DHCP_OPT_CLIENT_ID;
    *opts++ = 7;  // Length (1 + MAC length)
    *opts++ = 1;  // Hardware type (Ethernet)
    memcpy(opts, &dev->mac, 6);
    opts += 6;
    
    // Parameter request list
    *opts++ = DHCP_OPT_PARAM_REQ;
    *opts++ = 4;  // Length
    *opts++ = DHCP_OPT_SUBNET_MASK;
    *opts++ = DHCP_OPT_ROUTER;
    *opts++ = DHCP_OPT_DNS_SERVER;
    *opts++ = DHCP_OPT_DOMAIN_NAME;
    
    // End of options
    *opts++ = DHCP_OPT_END;
    
    // Update device's DHCP state
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            dhcp_configs[i].state = DHCP_STATE_SELECTING;
            dhcp_configs[i].last_update = hal_get_time_ms();
            break;
        }
    }
    
    // Set buffer length to actual size
    buffer->len = sizeof(dhcp_message_t) - DHCP_MAX_OPTIONS_LEN + (opts - msg->options);
    
    // Create a broadcast destination
    ipv4_address_t dest_ip;
    memset(&dest_ip, 0xFF, sizeof(ipv4_address_t));  // 255.255.255.255
    
    // Send via UDP
    int result = udp_send(dev, &dest_ip, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buffer);
    
    net_buffer_free(buffer);
    return result;
}

/**
 * Send a DHCP REQUEST message
 */
static int dhcp_send_request(net_device_t* dev, const ipv4_address_t* requested_ip, const ipv4_address_t* server_id) {
    net_buffer_t* buffer = net_buffer_alloc(sizeof(dhcp_message_t), 0);
    if (!buffer) {
        return NET_ERR_NOMEM;
    }
    
    dhcp_message_t* msg = (dhcp_message_t*)buffer->data;
    memset(msg, 0, sizeof(dhcp_message_t));
    
    // Fill in standard fields
    msg->op = DHCP_BOOTREQUEST;
    msg->htype = 1;  // Ethernet
    msg->hlen = 6;   // MAC address length
    msg->xid = dhcp_get_xid();
    
    // Copy MAC address to chaddr
    memcpy(msg->chaddr, &dev->mac, 6);
    
    // Set up options
    uint8_t* opts = msg->options;
    
    // Magic cookie
    *(uint32_t*)opts = htonl(DHCP_MAGIC_COOKIE);
    opts += 4;
    
    // Message type: REQUEST
    *opts++ = DHCP_OPT_MSG_TYPE;
    *opts++ = 1;  // Length
    *opts++ = DHCP_REQUEST;
    
    // Client identifier
    *opts++ = DHCP_OPT_CLIENT_ID;
    *opts++ = 7;  // Length (1 + MAC length)
    *opts++ = 1;  // Hardware type (Ethernet)
    memcpy(opts, &dev->mac, 6);
    opts += 6;
    
    // Requested IP
    *opts++ = DHCP_OPT_REQUESTED_IP;
    *opts++ = 4;  // Length
    memcpy(opts, requested_ip, 4);
    opts += 4;
    
    // Server identifier
    *opts++ = DHCP_OPT_SERVER_ID;
    *opts++ = 4;  // Length
    memcpy(opts, server_id, 4);
    opts += 4;
    
    // Parameter request list
    *opts++ = DHCP_OPT_PARAM_REQ;
    *opts++ = 4;  // Length
    *opts++ = DHCP_OPT_SUBNET_MASK;
    *opts++ = DHCP_OPT_ROUTER;
    *opts++ = DHCP_OPT_DNS_SERVER;
    *opts++ = DHCP_OPT_DOMAIN_NAME;
    
    // End of options
    *opts++ = DHCP_OPT_END;
    
    // Update device's DHCP state
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            dhcp_configs[i].state = DHCP_STATE_REQUESTING;
            dhcp_configs[i].last_update = hal_get_time_ms();
            break;
        }
    }
    
    // Set buffer length to actual size
    buffer->len = sizeof(dhcp_message_t) - DHCP_MAX_OPTIONS_LEN + (opts - msg->options);
    
    // Create a broadcast destination
    ipv4_address_t dest_ip;
    memset(&dest_ip, 0xFF, sizeof(ipv4_address_t));  // 255.255.255.255
    
    // Send via UDP
    int result = udp_send(dev, &dest_ip, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buffer);
    
    net_buffer_free(buffer);
    return result;
}

/**
 * Send a DHCP RELEASE message
 */
static int dhcp_send_release(net_device_t* dev) {
    // Find the device's config slot
    dhcp_config_t* config = NULL;
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            config = &dhcp_configs[i];
            break;
        }
    }
    
    if (!config || config->state != DHCP_STATE_BOUND) {
        return NET_ERR_INVALID;
    }

    net_buffer_t* buffer = net_buffer_alloc(sizeof(dhcp_message_t), 0);
    if (!buffer) {
        return NET_ERR_NOMEM;
    }
    
    dhcp_message_t* msg = (dhcp_message_t*)buffer->data;
    memset(msg, 0, sizeof(dhcp_message_t));
    
    // Fill in standard fields
    msg->op = DHCP_BOOTREQUEST;
    msg->htype = 1;  // Ethernet
    msg->hlen = 6;   // MAC address length
    msg->xid = dhcp_get_xid();
    
    // Copy current IP address
    memcpy(&msg->ciaddr, &config->ip_address, 4);
    
    // Copy MAC address to chaddr
    memcpy(msg->chaddr, &dev->mac, 6);
    
    // Set up options
    uint8_t* opts = msg->options;
    
    // Magic cookie
    *(uint32_t*)opts = htonl(DHCP_MAGIC_COOKIE);
    opts += 4;
    
    // Message type: RELEASE
    *opts++ = DHCP_OPT_MSG_TYPE;
    *opts++ = 1;  // Length
    *opts++ = DHCP_RELEASE;
    
    // Server identifier
    *opts++ = DHCP_OPT_SERVER_ID;
    *opts++ = 4;  // Length
    memcpy(opts, &config->server_id, 4);
    opts += 4;
    
    // End of options
    *opts++ = DHCP_OPT_END;
    
    // Set buffer length to actual size
    buffer->len = sizeof(dhcp_message_t) - DHCP_MAX_OPTIONS_LEN + (opts - msg->options);
    
    // Send to DHCP server
    int result = udp_send(dev, &config->server_id, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buffer);
    
    // Update state
    config->state = DHCP_STATE_INIT;
    
    net_buffer_free(buffer);
    return result;
}

/**
 * Process a received DHCP packet
 */
int dhcp_process_packet(net_device_t* dev, net_buffer_t* buffer) {
    if (!dev || !buffer || buffer->len < sizeof(dhcp_message_t) - DHCP_MAX_OPTIONS_LEN) {
        return NET_ERR_INVALID;
    }
    
    dhcp_message_t* msg = (dhcp_message_t*)buffer->data;
    
    // Verify this is a BOOTREPLY message
    if (msg->op != DHCP_BOOTREPLY) {
        return NET_ERR_INVALID;
    }
    
    // Verify the transaction ID matches our current one
    if (msg->xid != current_xid) {
        return NET_ERR_INVALID;
    }
    
    // Verify MAC address matches
    if (memcmp(msg->chaddr, &dev->mac, 6) != 0) {
        return NET_ERR_INVALID;
    }
    
    // Find the device's config slot
    dhcp_config_t* config = NULL;
    int config_idx = -1;
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            config = &dhcp_configs[i];
            config_idx = i;
            break;
        }
    }
    
    if (!config) {
        return NET_ERR_INVALID;
    }
    
    // Parse options to determine message type and extract configuration
    dhcp_config_t temp_config;
    memset(&temp_config, 0, sizeof(dhcp_config_t));
    
    // Copy the offered IP address
    memcpy(&temp_config.ip_address, &msg->yiaddr, 4);
    
    int msg_type = dhcp_parse_options(msg, &temp_config);
    
    switch (msg_type) {
        case DHCP_OFFER:
            if (config->state == DHCP_STATE_SELECTING) {
                LOG(LOG_INFO, "DHCP: Received OFFER from server");
                
                // Send REQUEST message
                return dhcp_send_request(dev, &temp_config.ip_address, &temp_config.server_id);
            }
            break;
            
        case DHCP_ACK:
            LOG(LOG_INFO, "DHCP: Received ACK from server");
            
            // Copy configuration and apply it
            memcpy(config, &temp_config, sizeof(dhcp_config_t));
            config->state = DHCP_STATE_BOUND;
            config->last_update = hal_get_time_ms();
            
            // Apply the configuration to the device
            dhcp_apply_config(dev, config);
            
            LOG(LOG_INFO, "DHCP: Configuration applied to %s", dev->name);
            break;
            
        case DHCP_NAK:
            LOG(LOG_WARNING, "DHCP: Received NAK from server");
            
            // Reset state and restart discovery
            config->state = DHCP_STATE_INIT;
            return dhcp_send_discover(dev);
            
        default:
            LOG(LOG_WARNING, "DHCP: Received unknown message type: %d", msg_type);
            return NET_ERR_INVALID;
    }
    
    return NET_ERR_OK;
}

/**
 * Parse DHCP options from a message
 * 
 * @return The DHCP message type or -1 on error
 */
static int dhcp_parse_options(dhcp_message_t* msg, dhcp_config_t* config) {
    uint8_t* opts = msg->options;
    uint8_t* end = opts + DHCP_MAX_OPTIONS_LEN;
    int msg_type = -1;
    
    // Check for DHCP magic cookie
    if (ntohl(*(uint32_t*)opts) != DHCP_MAGIC_COOKIE) {
        return -1;
    }
    
    opts += 4; // Skip past magic cookie
    
    // Parse options
    while (opts < end && *opts != DHCP_OPT_END) {
        uint8_t opt = *opts++;
        
        // Skip padding
        if (opt == DHCP_OPT_PAD) {
            continue;
        }
        
        // Get option length
        if (opts >= end) {
            break;
        }
        uint8_t len = *opts++;
        
        // Make sure we have enough data
        if (opts + len > end) {
            break;
        }
        
        // Process option
        switch (opt) {
            case DHCP_OPT_MSG_TYPE:
                if (len == 1) {
                    msg_type = *opts;
                }
                break;
                
            case DHCP_OPT_SUBNET_MASK:
                if (len == 4) {
                    memcpy(&config->subnet_mask, opts, 4);
                }
                break;
                
            case DHCP_OPT_ROUTER:
                if (len >= 4) {
                    memcpy(&config->gateway, opts, 4);
                }
                break;
                
            case DHCP_OPT_DNS_SERVER:
                if (len >= 4) {
                    memcpy(&config->dns_server, opts, 4);
                }
                break;
                
            case DHCP_OPT_LEASE_TIME:
                if (len == 4) {
                    config->lease_time = ntohl(*(uint32_t*)opts);
                    // Default renewal time is half the lease time
                    config->renewal_time = config->lease_time / 2;
                    // Default rebind time is 7/8 the lease time
                    config->rebind_time = config->lease_time - config->lease_time / 8;
                }
                break;
                
            case DHCP_OPT_SERVER_ID:
                if (len == 4) {
                    memcpy(&config->server_id, opts, 4);
                }
                break;
        }
        
        // Move to next option
        opts += len;
    }
    
    return msg_type;
}

/**
 * Apply DHCP configuration to a network device
 */
static void dhcp_apply_config(net_device_t* dev, dhcp_config_t* config) {
    // Set IP address
    memcpy(&dev->ip, &config->ip_address, sizeof(ipv4_address_t));
    
    // Set netmask
    memcpy(&dev->netmask, &config->subnet_mask, sizeof(ipv4_address_t));
    
    // Set gateway
    memcpy(&dev->gateway, &config->gateway, sizeof(ipv4_address_t));
    
    // Log the new configuration
    char ip_str[16], nm_str[16], gw_str[16], dns_str[16];
    LOG(LOG_INFO, "DHCP config for %s: IP=%s, Mask=%s, GW=%s, DNS=%s",
        dev->name,
        ipv4_to_str(&config->ip_address, ip_str),
        ipv4_to_str(&config->subnet_mask, nm_str),
        ipv4_to_str(&config->gateway, gw_str),
        ipv4_to_str(&config->dns_server, dns_str));
}

/**
 * Get the current DHCP configuration for a device
 */
int dhcp_get_config(net_device_t* dev, dhcp_config_t* config) {
    if (!dev || !config) {
        return NET_ERR_INVALID;
    }
    
    // Find the device's config
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        if (strcmp(dhcp_configs[i].name, dev->name) == 0) {
            memcpy(config, &dhcp_configs[i], sizeof(dhcp_config_t));
            return NET_ERR_OK;
        }
    }
    
    return NET_ERR_INVALID;
}

/**
 * Release a DHCP lease
 */
int dhcp_release(net_device_t* dev) {
    if (!dev) {
        return NET_ERR_INVALID;
    }
    
    return dhcp_send_release(dev);
}

/**
 * DHCP client task - handles timeouts and lease renewals
 */
void dhcp_client_task(void) {
    uint32_t current_time = hal_get_time_ms();
    
    // Check each device
    for (int i = 0; i < NET_MAX_DEVICES; i++) {
        dhcp_config_t* config = &dhcp_configs[i];
        
        // Skip if not configured
        if (config->name[0] == '\0') {
            continue;
        }
        
        // Get the device
        net_device_t* dev = network_find_device_by_name(config->name);
        if (!dev) {
            continue;
        }
        
        // Check state and elapsed time
        uint32_t elapsed_ms = current_time - config->last_update;
        uint32_t elapsed_sec = elapsed_ms / 1000;
        
        switch (config->state) {
            case DHCP_STATE_INIT:
                // Do nothing - waiting for dhcp_start to be called
                break;
                
            case DHCP_STATE_SELECTING:
                // Timeout for DISCOVER -> retry
                if (elapsed_ms > 5000) {
                    dhcp_send_discover(dev);
                }
                break;
                
            case DHCP_STATE_REQUESTING:
                // Timeout for REQUEST -> retry discovery
                if (elapsed_ms > 5000) {
                    config->state = DHCP_STATE_INIT;
                    dhcp_send_discover(dev);
                }
                break;
                
            case DHCP_STATE_BOUND:
                // Check if it's time to renew
                if (elapsed_sec >= config->renewal_time) {
                    LOG(LOG_INFO, "DHCP: Starting renewal for %s", dev->name);
                    config->state = DHCP_STATE_RENEWING;
                    dhcp_send_request(dev, &config->ip_address, &config->server_id);
                }
                break;
                
            case DHCP_STATE_RENEWING:
                // If we've reached the rebind time, broadcast
                if (elapsed_sec >= config->rebind_time) {
                    LOG(LOG_INFO, "DHCP: Starting rebinding for %s", dev->name);
                    config->state = DHCP_STATE_REBINDING;
                    
                    // Send a broadcast request
                    ipv4_address_t broadcast;
                    memset(&broadcast, 0xFF, sizeof(ipv4_address_t));
                    dhcp_send_request(dev, &config->ip_address, &broadcast);
                }
                // Otherwise, retry the renew every 60 seconds
                else if ((elapsed_sec - config->renewal_time) % 60 == 0) {
                    dhcp_send_request(dev, &config->ip_address, &config->server_id);
                }
                break;
                
            case DHCP_STATE_REBINDING:
                // If lease expired, go back to init
                if (elapsed_sec >= config->lease_time) {
                    LOG(LOG_WARNING, "DHCP: Lease expired for %s", dev->name);
                    config->state = DHCP_STATE_INIT;
                    
                    // Clear IP configuration
                    memset(&dev->ip, 0, sizeof(ipv4_address_t));
                    memset(&dev->netmask, 0, sizeof(ipv4_address_t));
                    memset(&dev->gateway, 0, sizeof(ipv4_address_t));
                    
                    // Start discovery again
                    dhcp_send_discover(dev);
                }
                // Otherwise, retry the rebind every 60 seconds
                else if ((elapsed_sec - config->rebind_time) % 60 == 0) {
                    ipv4_address_t broadcast;
                    memset(&broadcast, 0xFF, sizeof(ipv4_address_t));
                    dhcp_send_request(dev, &config->ip_address, &broadcast);
                }
                break;
        }
    }
}

/**
 * Generate a new transaction ID
 */
static uint32_t dhcp_get_xid(void) {
    // TODO: Use a proper random number generator
    current_xid = hal_get_time_ms() ^ (hal_get_time_ms() << 16);
    return current_xid;
}