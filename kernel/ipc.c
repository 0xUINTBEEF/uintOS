#include "ipc.h"
#include "task.h"
#include "../memory/heap.h"
#include "logging/log.h"

// Message queue for each task
typedef struct message_queue {
    ipc_message_t* messages[MAX_MESSAGES_PER_QUEUE];
    int head;
    int tail;
    int count;
    int task_id;
    spinlock_t lock;
} message_queue_t;

// System-wide message queues (one per task)
static message_queue_t message_queues[MAX_TASKS];

// IPC statistics
static ipc_stats_t ipc_stats = {0};

/**
 * Initialize the IPC subsystem
 */
void ipc_init(void) {
    log_info("IPC", "Initializing IPC subsystem");
    
    // Initialize all message queues
    for (int i = 0; i < MAX_TASKS; i++) {
        message_queues[i].head = 0;
        message_queues[i].tail = 0;
        message_queues[i].count = 0;
        message_queues[i].task_id = i;
        spinlock_init(&message_queues[i].lock);
    }
    
    // Initialize statistics
    memset(&ipc_stats, 0, sizeof(ipc_stats_t));
    
    log_info("IPC", "IPC subsystem initialized");
}

/**
 * Send a message to a task
 * 
 * @param to_task_id Task ID to send the message to
 * @param type Message type
 * @param data Message data
 * @param size Size of the data in bytes
 * @param flags Message flags
 * @return 0 on success, negative value on error
 */
int ipc_send_message(int to_task_id, int type, const void* data, int size, int flags) {
    // Validate parameters
    if (to_task_id < 0 || to_task_id >= MAX_TASKS || size < 0 || size > MAX_MESSAGE_SIZE) {
        log_error("IPC", "Invalid parameters for sending message to task %d", to_task_id);
        return IPC_ERR_INVALID_PARAM;
    }
    
    // Get the sender's task ID
    int from_task_id = get_current_task_id();
    
    // Allocate a new message
    ipc_message_t* msg = (ipc_message_t*)malloc(sizeof(ipc_message_t));
    if (msg == NULL) {
        log_error("IPC", "Failed to allocate memory for message");
        ipc_stats.errors++;
        return IPC_ERR_NO_MEMORY;
    }
    
    // Initialize the message
    msg->sender = from_task_id;
    msg->type = type;
    msg->flags = flags;
    msg->size = size;
    
    // Copy the message data if provided
    if (data != NULL && size > 0) {
        memcpy(msg->data, data, size);
    }
    
    // Acquire the lock for the destination task's queue
    spinlock_acquire(&message_queues[to_task_id].lock);
    
    // Check if the queue is full
    if (message_queues[to_task_id].count >= MAX_MESSAGES_PER_QUEUE) {
        spinlock_release(&message_queues[to_task_id].lock);
        free(msg);
        log_warning("IPC", "Message queue for task %d is full", to_task_id);
        ipc_stats.queue_full++;
        return IPC_ERR_QUEUE_FULL;
    }
    
    // Add the message to the queue
    int tail = message_queues[to_task_id].tail;
    message_queues[to_task_id].messages[tail] = msg;
    message_queues[to_task_id].tail = (tail + 1) % MAX_MESSAGES_PER_QUEUE;
    message_queues[to_task_id].count++;
    
    // Update statistics
    ipc_stats.messages_sent++;
    
    // Release the lock
    spinlock_release(&message_queues[to_task_id].lock);
    
    // Optional: Wake up the target task if it's waiting for messages
    // if (flags & IPC_FLAG_WAKE_TARGET) {
    //     wakeup_task(to_task_id);
    // }
    
    log_debug("IPC", "Message sent from task %d to task %d (type %d, size %d)", 
             from_task_id, to_task_id, type, size);
    
    return IPC_SUCCESS;
}

/**
 * Receive a message
 * 
 * @param msg Pointer to message structure to fill
 * @param flags Receive flags (e.g., IPC_FLAG_NOWAIT)
 * @return 0 on success, negative value on error
 */
int ipc_receive_message(ipc_message_t* msg, int flags) {
    if (msg == NULL) {
        return IPC_ERR_INVALID_PARAM;
    }
    
    // Get current task ID
    int task_id = get_current_task_id();
    
    // Acquire lock for our message queue
    spinlock_acquire(&message_queues[task_id].lock);
    
    // Check if the queue is empty
    if (message_queues[task_id].count == 0) {
        spinlock_release(&message_queues[task_id].lock);
        
        // If non-blocking, return immediately
        if (flags & IPC_FLAG_NOWAIT) {
            return IPC_ERR_NO_MESSAGE;
        }
        
        // Otherwise, we'll need to block until a message arrives
        // This is a simple busy waiting implementation
        // In a real implementation, we would use a proper wait mechanism
        spinlock_release(&message_queues[task_id].lock);
        
        // Simple busy waiting implementation
        while (1) {
            // Yield to other tasks
            switch_task();
            
            // Check if we have a message now
            spinlock_acquire(&message_queues[task_id].lock);
            if (message_queues[task_id].count > 0) {
                break;
            }
            spinlock_release(&message_queues[task_id].lock);
        }
        
        // We've got a message and already hold the lock
    }
    
    // Get the message from the queue
    int head = message_queues[task_id].head;
    ipc_message_t* received_msg = message_queues[task_id].messages[head];
    
    // Copy message content to user's buffer
    msg->sender = received_msg->sender;
    msg->type = received_msg->type;
    msg->flags = received_msg->flags;
    msg->size = received_msg->size;
    
    if (received_msg->size > 0) {
        memcpy(msg->data, received_msg->data, received_msg->size);
    }
    
    // Remove the message from the queue
    message_queues[task_id].messages[head] = NULL;
    message_queues[task_id].head = (head + 1) % MAX_MESSAGES_PER_QUEUE;
    message_queues[task_id].count--;
    
    // Update statistics
    ipc_stats.messages_received++;
    
    // Release the lock
    spinlock_release(&message_queues[task_id].lock);
    
    // Free the internal message structure
    free(received_msg);
    
    log_debug("IPC", "Message received by task %d from task %d (type %d, size %d)", 
             task_id, msg->sender, msg->type, msg->size);
    
    return IPC_SUCCESS;
}

/**
 * Peek at the next message without removing it
 * 
 * @param msg Pointer to message structure to fill
 * @param flags Peek flags
 * @return 0 on success, negative value on error
 */
int ipc_peek_message(ipc_message_t* msg, int flags) {
    if (msg == NULL) {
        return IPC_ERR_INVALID_PARAM;
    }
    
    // Get current task ID
    int task_id = get_current_task_id();
    
    // Acquire lock for our message queue
    spinlock_acquire(&message_queues[task_id].lock);
    
    // Check if the queue is empty
    if (message_queues[task_id].count == 0) {
        spinlock_release(&message_queues[task_id].lock);
        return IPC_ERR_NO_MESSAGE;
    }
    
    // Get the message from the queue without removing it
    int head = message_queues[task_id].head;
    ipc_message_t* peek_msg = message_queues[task_id].messages[head];
    
    // Copy message content to user's buffer
    msg->sender = peek_msg->sender;
    msg->type = peek_msg->type;
    msg->flags = peek_msg->flags;
    msg->size = peek_msg->size;
    
    if (peek_msg->size > 0) {
        memcpy(msg->data, peek_msg->data, peek_msg->size);
    }
    
    // Release the lock
    spinlock_release(&message_queues[task_id].lock);
    
    return IPC_SUCCESS;
}

/**
 * Get the number of pending messages for the current task
 * 
 * @return Number of pending messages
 */
int ipc_message_count(void) {
    int task_id = get_current_task_id();
    
    spinlock_acquire(&message_queues[task_id].lock);
    int count = message_queues[task_id].count;
    spinlock_release(&message_queues[task_id].lock);
    
    return count;
}

/**
 * Get IPC statistics
 * 
 * @param stats Pointer to statistics structure to fill
 */
void ipc_get_statistics(ipc_stats_t* stats) {
    if (stats) {
        *stats = ipc_stats;
    }
}

/**
 * Reset IPC statistics
 */
void ipc_reset_statistics(void) {
    memset(&ipc_stats, 0, sizeof(ipc_stats_t));
}

/**
 * Flush all messages for a task
 * 
 * @param task_id Task ID to flush messages for, or -1 for current task
 * @return Number of messages flushed
 */
int ipc_flush_messages(int task_id) {
    // Use current task if -1 is specified
    if (task_id == -1) {
        task_id = get_current_task_id();
    }
    
    // Validate task ID
    if (task_id < 0 || task_id >= MAX_TASKS) {
        return IPC_ERR_INVALID_PARAM;
    }
    
    spinlock_acquire(&message_queues[task_id].lock);
    
    // Free all messages in the queue
    int flushed = 0;
    for (int i = 0; i < message_queues[task_id].count; i++) {
        int index = (message_queues[task_id].head + i) % MAX_MESSAGES_PER_QUEUE;
        if (message_queues[task_id].messages[index]) {
            free(message_queues[task_id].messages[index]);
            message_queues[task_id].messages[index] = NULL;
            flushed++;
        }
    }
    
    // Reset the queue
    message_queues[task_id].head = 0;
    message_queues[task_id].tail = 0;
    message_queues[task_id].count = 0;
    
    spinlock_release(&message_queues[task_id].lock);
    
    return flushed;
}

/**
 * Send a response to a message
 * 
 * @param original_msg Original message to respond to
 * @param type Response message type
 * @param data Response data
 * @param size Size of the response data
 * @param flags Message flags
 * @return 0 on success, negative value on error
 */
int ipc_send_response(const ipc_message_t* original_msg, int type, const void* data, int size, int flags) {
    if (original_msg == NULL) {
        return IPC_ERR_INVALID_PARAM;
    }
    
    // Send message back to the sender task
    return ipc_send_message(original_msg->sender, type, data, size, flags);
}