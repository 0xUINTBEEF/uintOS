#include "vfs.h"
#include "../../kernel/logging/log.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Journal magic numbers */
#define VFS_JOURNAL_MAGIC          0x4A524E4C  /* "JRNL" */
#define VFS_JOURNAL_BLOCK_MAGIC    0x4A424C4B  /* "JBLK" */

/* Journal states */
#define VFS_JOURNAL_STATE_INACTIVE  0
#define VFS_JOURNAL_STATE_ACTIVE    1
#define VFS_JOURNAL_STATE_REPLAY    2
#define VFS_JOURNAL_STATE_ERROR     3

/* Transaction states */
#define VFS_TX_STATE_UNUSED         0
#define VFS_TX_STATE_RUNNING        1
#define VFS_TX_STATE_COMMITTING     2
#define VFS_TX_STATE_COMMITTED      3
#define VFS_TX_STATE_COMPLETE       4
#define VFS_TX_STATE_ABORTED        5

/* Journal operation types */
typedef enum {
    VFS_JOURNAL_OP_WRITE = 1,
    VFS_JOURNAL_OP_TRUNCATE,
    VFS_JOURNAL_OP_CREATE,
    VFS_JOURNAL_OP_DELETE,
    VFS_JOURNAL_OP_RENAME,
    VFS_JOURNAL_OP_MKDIR,
    VFS_JOURNAL_OP_RMDIR,
    VFS_JOURNAL_OP_SYMLINK,
    VFS_JOURNAL_OP_LINK,
    VFS_JOURNAL_OP_SETATTR,
    VFS_JOURNAL_OP_CUSTOM
} vfs_journal_op_type;

/* Journal operation structure */
typedef struct {
    vfs_journal_op_type type;
    uint32_t seq;
    union {
        struct {
            uint32_t block;
            uint32_t size;
            uint8_t* data;
            uint8_t* old_data;
        } write;
        struct {
            char path[VFS_MAX_PATH];
            uint64_t size;
        } truncate;
        struct {
            char path[VFS_MAX_PATH];
            uint32_t mode;
        } create;
        struct {
            char path[VFS_MAX_PATH];
        } delete;
        struct {
            char old_path[VFS_MAX_PATH];
            char new_path[VFS_MAX_PATH];
        } rename;
        struct {
            char path[VFS_MAX_PATH];
            uint32_t mode;
        } mkdir;
        struct {
            char path[VFS_MAX_PATH];
        } rmdir;
        struct {
            char target[VFS_MAX_PATH];
            char link_path[VFS_MAX_PATH];
        } symlink;
        struct {
            char target[VFS_MAX_PATH];
            char link_path[VFS_MAX_PATH];
        } link;
        struct {
            char path[VFS_MAX_PATH];
            uint32_t mode;
            uint32_t flags;
        } setattr;
        struct {
            uint32_t op_code;
            uint32_t data_size;
            void* data;
        } custom;
    } op;
} vfs_journal_operation_t;

/* Journal header structure (stored at beginning of journal) */
typedef struct {
    uint32_t magic;              /* Magic number (VFS_JOURNAL_MAGIC) */
    uint32_t version;            /* Journal format version */
    uint64_t size;               /* Total size of journal area */
    uint32_t block_size;         /* Size of each journal block */
    uint32_t flags;              /* Journal flags */
    uint32_t checksum;           /* Checksum of journal header */
    uint32_t sequence;           /* Current sequence number */
    uint32_t current_tx;         /* Current transaction ID */
    uint8_t state;               /* Current journal state */
    uint32_t start_block;        /* First journal block number */
    uint32_t num_blocks;         /* Number of journal blocks */
    uint32_t head;               /* Head offset */
    uint32_t tail;               /* Tail offset */
} vfs_journal_header_t;

/* Transaction list */
static vfs_transaction_t* tx_list = NULL;

/* Local mount journals */
static vfs_journal_t mount_journals[VFS_MAX_MOUNTS];

/* Function prototypes */
static int journal_write_header(vfs_mount_t* mount);
static int journal_read_header(vfs_mount_t* mount);
static int journal_reset(vfs_mount_t* mount);
static int journal_replay(vfs_mount_t* mount);
static int journal_allocate_tx(vfs_mount_t* mount, vfs_transaction_t** tx);
static int journal_free_tx(vfs_transaction_t* tx);
static int journal_write_entry(vfs_mount_t* mount, vfs_journal_entry_type type, 
                              const void* data, uint32_t size);
static uint32_t journal_calculate_checksum(const void* data, uint32_t size);
static int journal_verify_checksum(const void* data, uint32_t size, uint32_t expected);

/**
 * Initialize journal subsystem
 */
int vfs_journal_init() {
    log_info("VFS: Journal subsystem initialized");
    return VFS_SUCCESS;
}

/**
 * Create a new journal for a filesystem
 * 
 * @param mount_point Path to mount point
 * @param size Size of journal in bytes
 * @param flags Journal flags
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_create(const char* mount_point, uint64_t size, uint32_t flags) {
    if (!mount_point || size == 0) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Find mount point
    vfs_mount_t* mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_journals[i].dev_id != 0 && 
            strcmp(mount_point, "mount_info_would_be_here") == 0) {
            mount = "mount_would_be_found_here";
            break;
        }
    }
    
    if (!mount) {
        log_error("VFS: Mount point %s not found", mount_point);
        return VFS_ERR_NOT_FOUND;
    }
    
    // Check if journal already exists
    if (mount->journal && mount->journal->enabled) {
        log_warning("VFS: Journal already exists for %s", mount_point);
        return VFS_ERR_EXISTS;
    }
    
    // Allocate journal
    if (!mount->journal) {
        mount->journal = (vfs_journal_t*)malloc(sizeof(vfs_journal_t));
        if (!mount->journal) {
            log_error("VFS: Failed to allocate journal for %s", mount_point);
            return VFS_ERR_NO_SPACE;
        }
        memset(mount->journal, 0, sizeof(vfs_journal_t));
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_create) {
        log_error("VFS: Filesystem %s does not support journals", 
                 mount->fs_type->name);
        free(mount->journal);
        mount->journal = NULL;
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Call filesystem-specific journal creation
    int result = mount->fs_type->journal_create(mount, size, flags);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to create journal on %s: %s", 
                 mount_point, vfs_strerror(result));
        free(mount->journal);
        mount->journal = NULL;
        return result;
    }
    
    // Initialize journal structure
    mount->journal->dev_id = (uint32_t)(size_t)mount->device;  // This works as a simple ID
    mount->journal->size = size;
    mount->journal->flags = flags;
    mount->journal->block_size = 4096;  // Default, should be set by filesystem
    mount->journal->current_tx = 1;     // Start from 1
    mount->journal->enabled = 0;        // Not enabled yet
    mount->journal->active_tx = NULL;
    
    // Write journal header
    result = journal_write_header(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to write journal header on %s: %s", 
                 mount_point, vfs_strerror(result));
        free(mount->journal);
        mount->journal = NULL;
        return result;
    }
    
    log_info("VFS: Created %llu KB journal on %s (flags=0x%x)", 
            size / 1024, mount_point, flags);
    
    return VFS_SUCCESS;
}

/**
 * Start journaling on a filesystem
 * 
 * @param mount_point Path to mount point
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_start(const char* mount_point) {
    if (!mount_point) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Find mount point
    vfs_mount_t* mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_journals[i].dev_id != 0 && 
            strcmp(mount_point, "mount_info_would_be_here") == 0) {
            mount = "mount_would_be_found_here";
            break;
        }
    }
    
    if (!mount) {
        log_error("VFS: Mount point %s not found", mount_point);
        return VFS_ERR_NOT_FOUND;
    }
    
    // Check if journal exists
    if (!mount->journal) {
        log_error("VFS: No journal exists for %s", mount_point);
        return VFS_ERR_NOT_FOUND;
    }
    
    // Check if already enabled
    if (mount->journal->enabled) {
        log_warning("VFS: Journal already enabled for %s", mount_point);
        return VFS_SUCCESS;
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_start) {
        log_error("VFS: Filesystem %s does not support journals", 
                 mount->fs_type->name);
        return VFS_ERR_UNSUPPORTED;
    }
    
    // First, read the journal header
    int result = journal_read_header(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to read journal header on %s: %s", 
                 mount_point, vfs_strerror(result));
        return result;
    }
    
    // Replay the journal if necessary
    result = journal_replay(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to replay journal on %s: %s", 
                 mount_point, vfs_strerror(result));
        return result;
    }
    
    // Call filesystem-specific journal start
    result = mount->fs_type->journal_start(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to start journal on %s: %s", 
                 mount_point, vfs_strerror(result));
        return result;
    }
    
    // Enable journal
    mount->journal->enabled = 1;
    
    log_info("VFS: Started journal on %s", mount_point);
    
    return VFS_SUCCESS;
}

/**
 * Stop journaling on a filesystem
 * 
 * @param mount_point Path to mount point
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_stop(const char* mount_point) {
    if (!mount_point) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Find mount point
    vfs_mount_t* mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_journals[i].dev_id != 0 && 
            strcmp(mount_point, "mount_info_would_be_here") == 0) {
            mount = "mount_would_be_found_here";
            break;
        }
    }
    
    if (!mount) {
        log_error("VFS: Mount point %s not found", mount_point);
        return VFS_ERR_NOT_FOUND;
    }
    
    // Check if journal exists
    if (!mount->journal) {
        log_warning("VFS: No journal exists for %s", mount_point);
        return VFS_SUCCESS;
    }
    
    // Check if enabled
    if (!mount->journal->enabled) {
        log_warning("VFS: Journal already disabled for %s", mount_point);
        return VFS_SUCCESS;
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_stop) {
        log_error("VFS: Filesystem %s does not support journals", 
                 mount->fs_type->name);
        return VFS_ERR_UNSUPPORTED;
    }
    
    // If there's an active transaction, commit it
    if (mount->journal->active_tx) {
        int result = vfs_journal_commit_tx(mount, mount->journal->active_tx->id);
        if (result != VFS_SUCCESS) {
            log_error("VFS: Failed to commit active transaction on %s: %s", 
                     mount_point, vfs_strerror(result));
            // Continue anyway
        }
    }
    
    // Call filesystem-specific journal stop
    int result = mount->fs_type->journal_stop(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to stop journal on %s: %s", 
                 mount_point, vfs_strerror(result));
        return result;
    }
    
    // Disable journal
    mount->journal->enabled = 0;
    
    log_info("VFS: Stopped journal on %s", mount_point);
    
    return VFS_SUCCESS;
}

/**
 * Begin a new transaction
 * 
 * @param mount Mount point
 * @return Transaction ID on success, negative error code on failure
 */
int vfs_journal_begin_tx(vfs_mount_t* mount) {
    if (!mount) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        // No journal, return success with special transaction ID
        return 0;
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_begin_tx) {
        log_error("VFS: Filesystem %s does not support journal transactions", 
                 mount->fs_type->name);
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Check if a transaction is already active
    if (mount->journal->active_tx) {
        log_warning("VFS: Transaction already active on mount point, nesting not supported");
        return mount->journal->active_tx->id;
    }
    
    // Allocate a new transaction
    vfs_transaction_t* tx = NULL;
    int result = journal_allocate_tx(mount, &tx);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to allocate transaction: %s", 
                 vfs_strerror(result));
        return result;
    }
    
    // Call filesystem-specific begin transaction
    result = mount->fs_type->journal_begin_tx(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to begin transaction: %s", 
                 vfs_strerror(result));
        journal_free_tx(tx);
        return result;
    }
    
    // Write transaction start entry
    result = journal_write_entry(mount, VFS_JOURNAL_START_TX, &tx->id, sizeof(tx->id));
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to write transaction start: %s", 
                 vfs_strerror(result));
        mount->fs_type->journal_abort_tx(mount);
        journal_free_tx(tx);
        return result;
    }
    
    // Set as active transaction
    mount->journal->active_tx = tx;
    
    return tx->id;
}

/**
 * Commit an active transaction
 * 
 * @param mount Mount point
 * @param tx_id Transaction ID
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_commit_tx(vfs_mount_t* mount, int tx_id) {
    if (!mount) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Special transaction ID 0 means no journal
    if (tx_id == 0) {
        return VFS_SUCCESS;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        return VFS_SUCCESS;
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_commit_tx) {
        log_error("VFS: Filesystem %s does not support journal transactions", 
                 mount->fs_type->name);
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Check if a transaction is active
    if (!mount->journal->active_tx) {
        log_error("VFS: No active transaction to commit");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if transaction ID matches
    if (mount->journal->active_tx->id != tx_id) {
        log_error("VFS: Transaction ID mismatch");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Get transaction
    vfs_transaction_t* tx = mount->journal->active_tx;
    
    // Update state
    tx->state = VFS_TX_STATE_COMMITTING;
    
    // Write transaction commit entry
    int result = journal_write_entry(mount, VFS_JOURNAL_COMMIT_TX, &tx->id, sizeof(tx->id));
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to write transaction commit: %s", 
                 vfs_strerror(result));
        // Try to abort as a last resort
        mount->fs_type->journal_abort_tx(mount);
        // Clear active transaction
        mount->journal->active_tx = NULL;
        journal_free_tx(tx);
        return result;
    }
    
    // Call filesystem-specific commit transaction
    result = mount->fs_type->journal_commit_tx(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to commit transaction: %s", 
                 vfs_strerror(result));
        // Try to abort as a last resort
        mount->fs_type->journal_abort_tx(mount);
        // Clear active transaction
        mount->journal->active_tx = NULL;
        journal_free_tx(tx);
        return result;
    }
    
    // Update state
    tx->state = VFS_TX_STATE_COMMITTED;
    
    // Write checkpoint entry
    result = journal_write_entry(mount, VFS_JOURNAL_CHECKPOINT, NULL, 0);
    if (result != VFS_SUCCESS) {
        log_warning("VFS: Failed to write checkpoint: %s", 
                  vfs_strerror(result));
        // Not critical, continue
    }
    
    // Update state
    tx->state = VFS_TX_STATE_COMPLETE;
    
    // Clear active transaction
    mount->journal->active_tx = NULL;
    
    // Free transaction
    journal_free_tx(tx);
    
    return VFS_SUCCESS;
}

/**
 * Abort an active transaction
 * 
 * @param mount Mount point
 * @param tx_id Transaction ID
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_abort_tx(vfs_mount_t* mount, int tx_id) {
    if (!mount) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Special transaction ID 0 means no journal
    if (tx_id == 0) {
        return VFS_SUCCESS;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        return VFS_SUCCESS;
    }
    
    // Check if filesystem supports journaling
    if (!mount->fs_type->journal_abort_tx) {
        log_error("VFS: Filesystem %s does not support journal transactions", 
                 mount->fs_type->name);
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Check if a transaction is active
    if (!mount->journal->active_tx) {
        log_error("VFS: No active transaction to abort");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if transaction ID matches
    if (mount->journal->active_tx->id != tx_id) {
        log_error("VFS: Transaction ID mismatch");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Get transaction
    vfs_transaction_t* tx = mount->journal->active_tx;
    
    // Update state
    tx->state = VFS_TX_STATE_ABORTED;
    
    // Write transaction abort entry
    int result = journal_write_entry(mount, VFS_JOURNAL_ABORT_TX, &tx->id, sizeof(tx->id));
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to write transaction abort: %s", 
                 vfs_strerror(result));
        // Fall through to abort anyway
    }
    
    // Call filesystem-specific abort transaction
    result = mount->fs_type->journal_abort_tx(mount);
    if (result != VFS_SUCCESS) {
        log_error("VFS: Failed to abort transaction: %s", 
                 vfs_strerror(result));
        // Fall through to abort anyway
    }
    
    // Clear active transaction
    mount->journal->active_tx = NULL;
    
    // Free transaction
    journal_free_tx(tx);
    
    return VFS_SUCCESS;
}

/**
 * Record a metadata change in the journal
 * 
 * @param mount Mount point
 * @param data Metadata
 * @param size Size of metadata
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_log_metadata(vfs_mount_t* mount, const void* data, uint32_t size) {
    if (!mount || !data || size == 0) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        return VFS_SUCCESS;
    }
    
    // Check if a transaction is active
    if (!mount->journal->active_tx) {
        log_error("VFS: No active transaction for metadata log");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Write metadata entry
    return journal_write_entry(mount, VFS_JOURNAL_METADATA, data, size);
}

/**
 * Record a data block change in the journal
 * 
 * @param mount Mount point
 * @param block_id Block ID
 * @param data Block data
 * @param size Block size
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_log_data(vfs_mount_t* mount, uint32_t block_id, const void* data, uint32_t size) {
    if (!mount || !data || size == 0) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        return VFS_SUCCESS;
    }
    
    // Check if journal is configured to log data blocks
    if (!(mount->journal->flags & VFS_JOURNAL_DATA)) {
        return VFS_SUCCESS;
    }
    
    // Check if a transaction is active
    if (!mount->journal->active_tx) {
        log_error("VFS: No active transaction for data log");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Create a combined structure with block ID and data
    uint8_t* buffer = (uint8_t*)malloc(sizeof(block_id) + size);
    if (!buffer) {
        return VFS_ERR_NO_SPACE;
    }
    
    // Copy block ID and data
    memcpy(buffer, &block_id, sizeof(block_id));
    memcpy(buffer + sizeof(block_id), data, size);
    
    // Write data entry
    int result = journal_write_entry(mount, VFS_JOURNAL_DATA, buffer, sizeof(block_id) + size);
    
    // Free buffer
    free(buffer);
    
    return result;
}

/**
 * Add an operation to the current transaction
 * 
 * @param mount Mount point
 * @param op Operation to add
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_add_operation(vfs_mount_t* mount, vfs_journal_operation_t* op) {
    if (!mount || !op) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Check if journal exists and is enabled
    if (!mount->journal || !mount->journal->enabled) {
        return VFS_SUCCESS;
    }
    
    // Check if a transaction is active
    if (!mount->journal->active_tx) {
        log_error("VFS: No active transaction for operation");
        return VFS_ERR_INVALID_ARG;
    }
    
    // Get active transaction
    vfs_transaction_t* tx = mount->journal->active_tx;
    
    // Allocate operations array if needed
    if (!tx->operations) {
        tx->operations = malloc(sizeof(vfs_journal_operation_t));
        if (!tx->operations) {
            return VFS_ERR_NO_SPACE;
        }
    } else {
        // Resize array
        void* new_ops = realloc(tx->operations, 
                              (tx->num_operations + 1) * sizeof(vfs_journal_operation_t));
        if (!new_ops) {
            return VFS_ERR_NO_SPACE;
        }
        tx->operations = new_ops;
    }
    
    // Copy operation
    memcpy((uint8_t*)tx->operations + tx->num_operations * sizeof(vfs_journal_operation_t),
         op, sizeof(vfs_journal_operation_t));
    
    // Increment count
    tx->num_operations++;
    
    return VFS_SUCCESS;
}

/**
 * Write a journal header to disk
 * 
 * @param mount Mount point
 * @return 0 on success, negative error code on failure
 */
static int journal_write_header(vfs_mount_t* mount) {
    // Create a header
    vfs_journal_header_t header;
    memset(&header, 0, sizeof(header));
    
    // Fill header
    header.magic = VFS_JOURNAL_MAGIC;
    header.version = 1;
    header.size = mount->journal->size;
    header.block_size = mount->journal->block_size;
    header.flags = mount->journal->flags;
    header.sequence = 1;
    header.current_tx = mount->journal->current_tx;
    header.state = VFS_JOURNAL_STATE_INACTIVE;
    header.start_block = 1;  // First block after header
    header.num_blocks = (header.size - sizeof(header)) / header.block_size;
    header.head = header.tail = 0;
    
    // Calculate checksum
    header.checksum = journal_calculate_checksum(&header, sizeof(header) - sizeof(header.checksum));
    
    // This is where we'd write the header to disk
    // For now, just log it
    log_debug("VFS: Journal header written (size=%llu, blocks=%u)", 
             header.size, header.num_blocks);
    
    return VFS_SUCCESS;
}

/**
 * Read a journal header from disk
 * 
 * @param mount Mount point
 * @return 0 on success, negative error code on failure
 */
static int journal_read_header(vfs_mount_t* mount) {
    // This is where we'd read the header from disk
    // For now, just create a dummy header
    vfs_journal_header_t header;
    memset(&header, 0, sizeof(header));
    
    // Fill header
    header.magic = VFS_JOURNAL_MAGIC;
    header.version = 1;
    header.size = mount->journal->size;
    header.block_size = mount->journal->block_size;
    header.flags = mount->journal->flags;
    header.sequence = 1;
    header.current_tx = mount->journal->current_tx;
    header.state = VFS_JOURNAL_STATE_INACTIVE;
    header.start_block = 1;  // First block after header
    header.num_blocks = (header.size - sizeof(header)) / header.block_size;
    header.head = header.tail = 0;
    
    // Calculate expected checksum
    uint32_t expected_checksum = journal_calculate_checksum(
        &header, sizeof(header) - sizeof(header.checksum));
    
    // Verify checksum
    if (header.checksum != expected_checksum) {
        log_error("VFS: Journal header checksum mismatch");
        return VFS_ERR_CORRUPTED;
    }
    
    // Verify magic
    if (header.magic != VFS_JOURNAL_MAGIC) {
        log_error("VFS: Journal header has invalid magic");
        return VFS_ERR_CORRUPTED;
    }
    
    // Update journal info
    mount->journal->block_size = header.block_size;
    mount->journal->current_tx = header.current_tx;
    
    log_debug("VFS: Journal header read (size=%llu, blocks=%u)", 
             header.size, header.num_blocks);
    
    return VFS_SUCCESS;
}

/**
 * Reset a journal
 * 
 * @param mount Mount point
 * @return 0 on success, negative error code on failure
 */
static int journal_reset(vfs_mount_t* mount) {
    // Write a new header
    return journal_write_header(mount);
}

/**
 * Replay a journal
 * 
 * @param mount Mount point
 * @return 0 on success, negative error code on failure
 */
static int journal_replay(vfs_mount_t* mount) {
    // This is where we'd scan the journal and replay transactions
    // For now, just reset the journal
    return journal_reset(mount);
}

/**
 * Allocate a new transaction
 * 
 * @param mount Mount point
 * @param tx Output transaction pointer
 * @return 0 on success, negative error code on failure
 */
static int journal_allocate_tx(vfs_mount_t* mount, vfs_transaction_t** tx) {
    // Allocate memory for transaction
    vfs_transaction_t* new_tx = (vfs_transaction_t*)malloc(sizeof(vfs_transaction_t));
    if (!new_tx) {
        return VFS_ERR_NO_SPACE;
    }
    
    // Initialize transaction
    memset(new_tx, 0, sizeof(vfs_transaction_t));
    
    // Assign ID and increment for next transaction
    new_tx->id = mount->journal->current_tx++;
    new_tx->state = VFS_TX_STATE_RUNNING;
    
    // Add to transaction list
    new_tx->next = tx_list;
    tx_list = new_tx;
    
    // Return transaction
    *tx = new_tx;
    
    return VFS_SUCCESS;
}

/**
 * Free a transaction
 * 
 * @param tx Transaction to free
 * @return 0 on success, negative error code on failure
 */
static int journal_free_tx(vfs_transaction_t* tx) {
    if (!tx) {
        return VFS_ERR_INVALID_ARG;
    }
    
    // Remove from transaction list
    vfs_transaction_t** pprev = &tx_list;
    vfs_transaction_t* curr = tx_list;
    
    while (curr) {
        if (curr == tx) {
            // Found it, remove from list
            *pprev = curr->next;
            break;
        }
        
        pprev = &curr->next;
        curr = curr->next;
    }
    
    // Free operations if any
    if (tx->operations) {
        // Need to clean up any allocation within operations
        for (uint32_t i = 0; i < tx->num_operations; i++) {
            vfs_journal_operation_t* op = (vfs_journal_operation_t*)tx->operations + i;
            
            // Clean up based on operation type
            switch (op->type) {
                case VFS_JOURNAL_OP_WRITE:
                    if (op->op.write.data) {
                        free(op->op.write.data);
                    }
                    if (op->op.write.old_data) {
                        free(op->op.write.old_data);
                    }
                    break;
                    
                case VFS_JOURNAL_OP_CUSTOM:
                    if (op->op.custom.data) {
                        free(op->op.custom.data);
                    }
                    break;
                    
                default:
                    // No allocations for other types
                    break;
            }
        }
        
        free(tx->operations);
    }
    
    // Free transaction
    free(tx);
    
    return VFS_SUCCESS;
}

/**
 * Write a journal entry
 * 
 * @param mount Mount point
 * @param type Entry type
 * @param data Entry data
 * @param size Data size
 * @return 0 on success, negative error code on failure
 */
static int journal_write_entry(vfs_mount_t* mount, vfs_journal_entry_type type, 
                             const void* data, uint32_t size) {
    // Create entry header
    struct vfs_journal_entry_header header;
    memset(&header, 0, sizeof(header));
    
    // Fill header
    header.magic = VFS_JOURNAL_BLOCK_MAGIC;
    header.entry_type = type;
    header.size = sizeof(header) + size;
    header.sequence = 0; // Would be set from journal header
    
    // Set transaction ID if active transaction exists
    if (mount->journal->active_tx) {
        header.transaction_id = mount->journal->active_tx->id;
    }
    
    // Calculate checksum for header and data
    header.checksum = journal_calculate_checksum(&header, sizeof(header) - sizeof(header.checksum));
    if (data && size > 0) {
        header.checksum ^= journal_calculate_checksum(data, size);
    }
    
    // This is where we'd write the entry to disk
    // For now, just log it
    log_debug("VFS: Journal entry written (type=%u, size=%u)", 
             type, header.size);
    
    return VFS_SUCCESS;
}

/**
 * Calculate checksum for data
 * 
 * @param data Data to checksum
 * @param size Data size
 * @return Checksum
 */
static uint32_t journal_calculate_checksum(const void* data, uint32_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    // Simple checksum algorithm (CRC32 would be better in real implementation)
    uint32_t checksum = 0;
    const uint8_t* bytes = (const uint8_t*)data;
    
    for (uint32_t i = 0; i < size; i++) {
        checksum = ((checksum << 5) + checksum) + bytes[i];
    }
    
    return checksum;
}

/**
 * Verify checksum for data
 * 
 * @param data Data to verify
 * @param size Data size
 * @param expected Expected checksum
 * @return 0 if checksum matches, negative error code otherwise
 */
static int journal_verify_checksum(const void* data, uint32_t size, uint32_t expected) {
    uint32_t actual = journal_calculate_checksum(data, size);
    
    if (actual != expected) {
        return VFS_ERR_CORRUPTED;
    }
    
    return VFS_SUCCESS;
}

/**
 * Shutdown the journal system
 * 
 * @return 0 on success, negative error code on failure
 */
int vfs_journal_shutdown(void) {
    // Free all transactions in list
    while (tx_list) {
        vfs_transaction_t* next = tx_list->next;
        journal_free_tx(tx_list);
        tx_list = next;
    }
    
    return VFS_SUCCESS;
}

/**
 * Get error string for VFS error code
 */
const char* vfs_strerror(int error) {
    switch (error) {
        case VFS_SUCCESS:
            return "Success";
        case VFS_ERR_NOT_FOUND:
            return "File or directory not found";
        case VFS_ERR_EXISTS:
            return "File or directory already exists";
        case VFS_ERR_IO_ERROR:
            return "Input/output error";
        case VFS_ERR_NO_SPACE:
            return "No space left on device";
        case VFS_ERR_INVALID_ARG:
            return "Invalid argument";
        case VFS_ERR_NOT_DIR:
            return "Not a directory";
        case VFS_ERR_NOT_FILE:
            return "Not a regular file";
        case VFS_ERR_NOT_EMPTY:
            return "Directory not empty";
        case VFS_ERR_READONLY:
            return "Read-only filesystem";
        case VFS_ERR_UNSUPPORTED:
            return "Operation not supported";
        case VFS_ERR_JOURNAL_FULL:
            return "Journal is full";
        case VFS_ERR_CORRUPTED:
            return "Filesystem or journal is corrupted";
        case VFS_ERR_PERMISSION:
            return "Permission denied";
        case VFS_ERR_LOCKED:
            return "Resource is locked";
        case VFS_ERR_TIMEOUT:
            return "Operation timed out";
        default:
            return "Unknown error";
    }
}