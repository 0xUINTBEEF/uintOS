#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include "sync.h"

// Maximum number of messages in a task's queue
#define MAX_MESSAGES_PER_QUEUE 32

// Maximum message data size in bytes
#define MAX_MESSAGE_SIZE 256

// IPC message flags
#define IPC_FLAG_NONE      0x00
#define IPC_FLAG_NOWAIT    0x01  // Non-blocking operation
#define IPC_FLAG_PRIORITY  0x02  // High priority message
#define IPC_FLAG_WAKE_TARGET 0x04 // Wake up target task

// Message types (can be extended by applications)
#define IPC_MSG_TYPE_NONE   0
#define IPC_MSG_TYPE_DATA   1
#define IPC_MSG_TYPE_EVENT  2
#define IPC_MSG_TYPE_REQUEST 3
#define IPC_MSG_TYPE_RESPONSE 4
#define IPC_MSG_TYPE_ERROR  5
#define IPC_MSG_TYPE_SIGNAL 6

// IPC result codes
#define IPC_SUCCESS           0
#define IPC_ERR_INVALID_PARAM -1
#define IPC_ERR_NO_MEMORY     -2
#define IPC_ERR_QUEUE_FULL    -3
#define IPC_ERR_NO_MESSAGE    -4
#define IPC_ERR_TIMEOUT       -5

// IPC message structure
typedef struct ipc_message {
    int sender;                   // Sender task ID
    int type;                     // Message type
    int flags;                    // Message flags
    int size;                     // Size of data in bytes
    uint8_t data[MAX_MESSAGE_SIZE]; // Message data
} ipc_message_t;

// IPC statistics structure
typedef struct ipc_stats {
    uint32_t messages_sent;       // Number of messages sent
    uint32_t messages_received;   // Number of messages received
    uint32_t errors;              // Number of errors
    uint32_t queue_full;          // Number of queue full conditions
} ipc_stats_t;

// Initialize the IPC subsystem
void ipc_init(void);

// Send a message to a task
int ipc_send_message(int to_task_id, int type, const void* data, int size, int flags);

// Receive a message
int ipc_receive_message(ipc_message_t* msg, int flags);

// Peek at the next message without removing it
int ipc_peek_message(ipc_message_t* msg, int flags);

// Get the number of pending messages for the current task
int ipc_message_count(void);

// Get IPC statistics
void ipc_get_statistics(ipc_stats_t* stats);

// Reset IPC statistics
void ipc_reset_statistics(void);

// Flush all messages for a task
int ipc_flush_messages(int task_id);

// Send a response to a message
int ipc_send_response(const ipc_message_t* original_msg, int type, const void* data, int size, int flags);

#endif // IPC_H