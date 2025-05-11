/**
 * @file ac97.c
 * @brief AC97 Sound Card Driver for uintOS
 *
 * This driver provides support for AC97-compatible sound cards
 * using the PCI driver framework.
 */

#include "ac97.h"
#include "../../kernel/logging/log.h"
#include "../../memory/heap.h"
#include "../../hal/include/hal_io.h"
#include "../../hal/include/hal_interrupt.h"
#include "../../hal/include/hal_memory.h"
#include <string.h>

#define AC97_TAG "AC97"

// Array of supported PCI vendor IDs
static uint16_t ac97_vendor_ids[] = {
    AC97_INTEL_VENDOR_ID
};

// Array of supported PCI device IDs (Intel AC97 controllers)
static uint16_t ac97_device_ids[] = {
    AC97_INTEL_82801AA,
    AC97_INTEL_82801AB,
    AC97_INTEL_82801BA,
    AC97_INTEL_82801CA,
    AC97_INTEL_82801DB,
    AC97_INTEL_82801EB,
    AC97_INTEL_82801FB,
    AC97_INTEL_82801GB,
    AC97_INTEL_82801HB,
    AC97_INTEL_82801I
};

// Forward declaration of driver operations
static int ac97_probe(pci_device_t* dev);
static int ac97_initialize(pci_device_t* dev);
static int ac97_remove(pci_device_t* dev);
static int ac97_suspend(pci_device_t* dev);
static int ac97_resume(pci_device_t* dev);

// PCI driver structure
static pci_driver_t ac97_driver = {
    .name = "ac97",
    .vendor_ids = ac97_vendor_ids,
    .device_ids = ac97_device_ids,
    .num_supported_devices = sizeof(ac97_device_ids) / sizeof(ac97_device_ids[0]),
    .ops = {
        .probe = ac97_probe,
        .init = ac97_initialize,
        .remove = ac97_remove,
        .suspend = ac97_suspend,
        .resume = ac97_resume
    }
};

/**
 * Initialize AC97 driver
 */
int ac97_init(void) {
    log_info(AC97_TAG, "Initializing AC97 audio driver");
    return pci_register_driver(&ac97_driver);
}

/**
 * Cleanup AC97 driver
 */
void ac97_exit(void) {
    log_info(AC97_TAG, "Shutting down AC97 audio driver");
    pci_unregister_driver(&ac97_driver);
}

/**
 * Read a value from AC97 mixer register
 */
uint16_t ac97_mixer_read(ac97_device_t* priv, uint8_t reg) {
    // Wait for codec ready (bit 15 of status register cleared)
    int timeout = 1000;
    while (timeout-- > 0 && (hal_io_port_in32(priv->mixer_base) & 0x8000)) {
        hal_io_wait_us(1);
    }
    
    if (timeout <= 0) {
        log_warning(AC97_TAG, "Timeout waiting for codec ready");
    }
    
    // Read register value
    return hal_io_port_in16(priv->mixer_base + reg);
}

/**
 * Write a value to AC97 mixer register
 */
void ac97_mixer_write(ac97_device_t* priv, uint8_t reg, uint16_t value) {
    // Wait for codec ready (bit 15 of status register cleared)
    int timeout = 1000;
    while (timeout-- > 0 && (hal_io_port_in32(priv->mixer_base) & 0x8000)) {
        hal_io_wait_us(1);
    }
    
    if (timeout <= 0) {
        log_warning(AC97_TAG, "Timeout waiting for codec ready");
    }
    
    // Write register value
    hal_io_port_out16(priv->mixer_base + reg, value);
}

/**
 * Read an 8-bit value from AC97 bus master register
 */
uint8_t ac97_bus_read8(ac97_device_t* priv, uint8_t reg) {
    return hal_io_port_in8(priv->bus_base + reg);
}

/**
 * Read a 16-bit value from AC97 bus master register
 */
uint16_t ac97_bus_read16(ac97_device_t* priv, uint8_t reg) {
    return hal_io_port_in16(priv->bus_base + reg);
}

/**
 * Read a 32-bit value from AC97 bus master register
 */
uint32_t ac97_bus_read32(ac97_device_t* priv, uint8_t reg) {
    return hal_io_port_in32(priv->bus_base + reg);
}

/**
 * Write an 8-bit value to AC97 bus master register
 */
void ac97_bus_write8(ac97_device_t* priv, uint8_t reg, uint8_t value) {
    hal_io_port_out8(priv->bus_base + reg, value);
}

/**
 * Write a 16-bit value to AC97 bus master register
 */
void ac97_bus_write16(ac97_device_t* priv, uint8_t reg, uint16_t value) {
    hal_io_port_out16(priv->bus_base + reg, value);
}

/**
 * Write a 32-bit value to AC97 bus master register
 */
void ac97_bus_write32(ac97_device_t* priv, uint8_t reg, uint32_t value) {
    hal_io_port_out32(priv->bus_base + reg, value);
}

/**
 * Reset the AC97 audio controller
 */
int ac97_reset(pci_device_t* dev) {
    ac97_device_t* priv = (ac97_device_t*)dev->private_data;
    
    log_info(AC97_TAG, "Resetting AC97 audio controller");
    
    // Cold reset of AC97 controller
    ac97_bus_write32(priv, AC97_NABM_GLOB_CNT, 0x00000002);  // Set COLD_RESET bit
    hal_io_wait_us(100);                                     // Wait for reset
    ac97_bus_write32(priv, AC97_NABM_GLOB_CNT, 0x00000000);  // Clear COLD_RESET bit
    hal_io_wait_us(100);                                     // Wait for reset completion
    
    // Wait for codec ready 
    int timeout = 1000;
    while (timeout-- > 0) {
        if (ac97_mixer_read(priv, AC97_REG_RESET) != 0) {
            break;
        }
        hal_io_wait_us(10);
    }
    
    if (timeout <= 0) {
        log_error(AC97_TAG, "Timeout waiting for codec to become ready after reset");
        return -1;
    }
    
    // Read vendor ID
    uint16_t vendor_id1 = ac97_mixer_read(priv, AC97_REG_VENDOR_ID1);
    uint16_t vendor_id2 = ac97_mixer_read(priv, AC97_REG_VENDOR_ID2);
    priv->vendor_id = (vendor_id1 << 16) | vendor_id2;
    
    log_info(AC97_TAG, "AC97 codec vendor ID: 0x%08X", priv->vendor_id);
    
    // Read extended audio ID register
    uint16_t ext_id = ac97_mixer_read(priv, AC97_REG_EXTENDED_ID);
    priv->supports_variable_rate = (ext_id & (1 << 1)) != 0;
    
    log_info(AC97_TAG, "AC97 codec supports variable rate: %s", 
             priv->supports_variable_rate ? "yes" : "no");
    
    // Set master volume to 75% (value of 8 out of 31)
    ac97_set_playback_volume(priv, 8, 8, false);
    
    // Set PCM output volume to 75% (value of 8 out of 31)
    ac97_mixer_write(priv, AC97_REG_PCM_OUT_VOL, 0x0808);
    
    // Select line in as recording source
    ac97_set_recording_source(priv, 4);  // 4 = line in
    
    // Set recording gain to 50%
    ac97_set_recording_volume(priv, 8, 8, false);
    
    // Configure sample rate (if supported)
    if (priv->supports_variable_rate) {
        // Set front DAC rate to 48kHz
        ac97_mixer_write(priv, AC97_REG_PCM_FRONT_DAC_RATE, AC97_SAMPLE_RATE_48K);
    }
    
    log_info(AC97_TAG, "AC97 controller reset and configured successfully");
    return 0;
}

/**
 * Start audio playback
 */
int ac97_start_playback(ac97_device_t* priv, uint8_t** buffers, uint8_t buffer_count, uint16_t buffer_size) {
    if (!priv || !buffers || buffer_count == 0 || buffer_count > AC97_BDL_SIZE) {
        return -1;
    }
    
    // Stop any ongoing playback
    ac97_stop_playback(priv);
    
    // Initialize buffer descriptor list
    for (int i = 0; i < buffer_count; i++) {
        priv->play_buffers[i] = buffers[i];
        priv->play_buffers_phys[i] = (uint32_t)hal_memory_get_physical(buffers[i]);
        
        priv->play_bdl[i].buffer_addr = priv->play_buffers_phys[i];
        priv->play_bdl[i].buffer_samples = buffer_size / 4;  // Size in samples (16-bit stereo = 4 bytes per sample)
        priv->play_bdl[i].flags = 0x8000;  // Interrupt on completion
    }
    
    // Set last valid index
    priv->play_lvi = buffer_count - 1;
    ac97_bus_write8(priv, AC97_NABM_PCMOUT_LVI, priv->play_lvi);
    
    // Clear status register
    ac97_bus_write16(priv, AC97_NABM_PCMOUT_SR, 0x1C);
    
    // Set buffer descriptor list base address
    ac97_bus_write32(priv, AC97_NABM_PCMOUT_BDBAR, priv->play_bdl_phys);
    
    // Start playback
    ac97_bus_write8(priv, AC97_NABM_PCMOUT_LVI, priv->play_lvi);
    ac97_bus_write8(priv, AC97_NABM_PCMOUT_CR, 0x15);  // Enable interrupt, transfer, and busmaster
    
    priv->play_active = true;
    
    log_debug(AC97_TAG, "Started audio playback with %d buffers", buffer_count);
    return 0;
}

/**
 * Stop audio playback
 */
void ac97_stop_playback(ac97_device_t* priv) {
    if (!priv) {
        return;
    }
    
    if (priv->play_active) {
        // Stop playback
        ac97_bus_write8(priv, AC97_NABM_PCMOUT_CR, 0);
        
        // Wait for DMA to complete
        int timeout = 100;
        while (timeout-- > 0 && (ac97_bus_read8(priv, AC97_NABM_PCMOUT_CR) & 0x01)) {
            hal_io_wait_us(10);
        }
        
        // Reset
        ac97_bus_write8(priv, AC97_NABM_PCMOUT_CR, 0x02);
        
        // Wait for reset to complete
        timeout = 100;
        while (timeout-- > 0 && (ac97_bus_read8(priv, AC97_NABM_PCMOUT_CR) & 0x02)) {
            hal_io_wait_us(10);
        }
        
        // Reset current index
        ac97_bus_write8(priv, AC97_NABM_PCMOUT_CIV, 0);
        
        priv->play_active = false;
        
        log_debug(AC97_TAG, "Stopped audio playback");
    }
}

/**
 * Start audio recording
 */
int ac97_start_recording(ac97_device_t* priv, uint8_t** buffers, uint8_t buffer_count, uint16_t buffer_size) {
    if (!priv || !buffers || buffer_count == 0 || buffer_count > AC97_BDL_SIZE) {
        return -1;
    }
    
    // Stop any ongoing recording
    ac97_stop_recording(priv);
    
    // Initialize buffer descriptor list
    for (int i = 0; i < buffer_count; i++) {
        priv->record_buffers[i] = buffers[i];
        priv->record_buffers_phys[i] = (uint32_t)hal_memory_get_physical(buffers[i]);
        
        priv->record_bdl[i].buffer_addr = priv->record_buffers_phys[i];
        priv->record_bdl[i].buffer_samples = buffer_size / 4;  // Size in samples (16-bit stereo = 4 bytes per sample)
        priv->record_bdl[i].flags = 0x8000;  // Interrupt on completion
    }
    
    // Set last valid index
    priv->record_lvi = buffer_count - 1;
    ac97_bus_write8(priv, AC97_NABM_PCMIN_LVI, priv->record_lvi);
    
    // Clear status register
    ac97_bus_write16(priv, AC97_NABM_PCMIN_SR, 0x1C);
    
    // Set buffer descriptor list base address
    ac97_bus_write32(priv, AC97_NABM_PCMIN_BDBAR, priv->record_bdl_phys);
    
    // Start recording
    ac97_bus_write8(priv, AC97_NABM_PCMIN_LVI, priv->record_lvi);
    ac97_bus_write8(priv, AC97_NABM_PCMIN_CR, 0x15);  // Enable interrupt, transfer, and busmaster
    
    priv->record_active = true;
    
    log_debug(AC97_TAG, "Started audio recording with %d buffers", buffer_count);
    return 0;
}

/**
 * Stop audio recording
 */
void ac97_stop_recording(ac97_device_t* priv) {
    if (!priv) {
        return;
    }
    
    if (priv->record_active) {
        // Stop recording
        ac97_bus_write8(priv, AC97_NABM_PCMIN_CR, 0);
        
        // Wait for DMA to complete
        int timeout = 100;
        while (timeout-- > 0 && (ac97_bus_read8(priv, AC97_NABM_PCMIN_CR) & 0x01)) {
            hal_io_wait_us(10);
        }
        
        // Reset
        ac97_bus_write8(priv, AC97_NABM_PCMIN_CR, 0x02);
        
        // Wait for reset to complete
        timeout = 100;
        while (timeout-- > 0 && (ac97_bus_read8(priv, AC97_NABM_PCMIN_CR) & 0x02)) {
            hal_io_wait_us(10);
        }
        
        // Reset current index
        ac97_bus_write8(priv, AC97_NABM_PCMIN_CIV, 0);
        
        priv->record_active = false;
        
        log_debug(AC97_TAG, "Stopped audio recording");
    }
}

/**
 * Set playback volume
 */
int ac97_set_playback_volume(ac97_device_t* priv, uint8_t left, uint8_t right, bool mute) {
    if (!priv) {
        return -1;
    }
    
    // Clamp volume values to 0-31
    left = (left > 31) ? 31 : left;
    right = (right > 31) ? 31 : right;
    
    uint16_t value = (left << 8) | right;
    if (mute) {
        value |= 0x8000;  // Set mute bit
    }
    
    ac97_mixer_write(priv, AC97_REG_MASTER_VOL, value);
    log_debug(AC97_TAG, "Set playback volume: left=%d, right=%d, mute=%d", left, right, mute);
    
    return 0;
}

/**
 * Set recording volume
 */
int ac97_set_recording_volume(ac97_device_t* priv, uint8_t left, uint8_t right, bool mute) {
    if (!priv) {
        return -1;
    }
    
    // Clamp volume values to 0-15
    left = (left > 15) ? 15 : left;
    right = (right > 15) ? 15 : right;
    
    uint16_t value = (left << 8) | right;
    if (mute) {
        value |= 0x8000;  // Set mute bit
    }
    
    ac97_mixer_write(priv, AC97_REG_RECORD_GAIN, value);
    log_debug(AC97_TAG, "Set recording volume: left=%d, right=%d, mute=%d", left, right, mute);
    
    return 0;
}

/**
 * Set recording source
 */
int ac97_set_recording_source(ac97_device_t* priv, uint8_t source) {
    if (!priv || source > 7) {
        return -1;
    }
    
    // Set same source for left and right channels
    uint16_t value = (source << 8) | source;
    ac97_mixer_write(priv, AC97_REG_RECORD_SELECT, value);
    
    log_debug(AC97_TAG, "Set recording source to %d", source);
    return 0;
}

/**
 * AC97 interrupt handler
 */
void ac97_interrupt(pci_device_t* dev) {
    ac97_device_t* priv = (ac97_device_t*)dev->private_data;
    
    // Check playback interrupt status
    uint16_t play_status = ac97_bus_read16(priv, AC97_NABM_PCMOUT_SR);
    if (play_status & 0x04) {  // Buffer completion interrupt
        // Clear the interrupt
        ac97_bus_write16(priv, AC97_NABM_PCMOUT_SR, 0x04);
        
        // Get current buffer index
        uint8_t civ = ac97_bus_read8(priv, AC97_NABM_PCMOUT_CIV);
        
        // Update statistics
        priv->bytes_played += priv->play_bdl[civ].buffer_samples * 4;  // 4 bytes per sample
        
        log_debug(AC97_TAG, "Playback buffer completed: index=%d, total bytes=%d", 
                 civ, priv->bytes_played);
    }
    
    // Check recording interrupt status
    uint16_t record_status = ac97_bus_read16(priv, AC97_NABM_PCMIN_SR);
    if (record_status & 0x04) {  // Buffer completion interrupt
        // Clear the interrupt
        ac97_bus_write16(priv, AC97_NABM_PCMIN_SR, 0x04);
        
        // Get current buffer index
        uint8_t civ = ac97_bus_read8(priv, AC97_NABM_PCMIN_CIV);
        
        // Update statistics
        priv->bytes_recorded += priv->record_bdl[civ].buffer_samples * 4;  // 4 bytes per sample
        
        log_debug(AC97_TAG, "Recording buffer completed: index=%d, total bytes=%d", 
                 civ, priv->bytes_recorded);
    }
}

/**
 * AC97 interrupt callback for HAL
 */
static void ac97_interrupt_handler(void* context) {
    pci_device_t* dev = (pci_device_t*)context;
    ac97_interrupt(dev);
}

/**
 * Check if this driver can handle the device
 */
static int ac97_probe(pci_device_t* dev) {
    log_info(AC97_TAG, "Probing device %04X:%04X", 
            dev->id.vendor_id, dev->id.device_id);
    
    // Check if this is an audio device with AC97 interface
    if (dev->id.class_code == PCI_CLASS_MULTIMEDIA && 
        dev->id.subclass == 0x01) {
        return 0;  // This is an AC97-compatible audio device
    }
    
    return -1;  // Not an AC97 audio device
}

/**
 * Initialize the AC97 device after being claimed by the driver
 */
static int ac97_initialize(pci_device_t* dev) {
    log_info(AC97_TAG, "Initializing AC97 audio controller");
    
    // Allocate private device data
    ac97_device_t* priv = (ac97_device_t*)heap_alloc(sizeof(ac97_device_t));
    if (!priv) {
        log_error(AC97_TAG, "Failed to allocate device structure");
        return -1;
    }
    
    // Clear the structure
    memset(priv, 0, sizeof(ac97_device_t));
    
    // Store the private data in the PCI device structure
    dev->private_data = priv;
    
    // Enable PCI bus mastering and memory space
    pci_enable_bus_mastering(dev);
    pci_enable_memory_space(dev);
    pci_enable_io_space(dev);
    
    // Get the I/O base addresses for the mixer and bus master interfaces
    // These are typically in BAR0 and BAR1
    bool found_mixbar = false;
    bool found_bmbar = false;
    
    for (int i = 0; i < 6; i++) {
        if (dev->id.bar[i] != 0) {
            uint32_t base;
            uint32_t size;
            bool is_io;
            
            if (pci_get_bar_info(dev, i, &base, &size, &is_io) == 0) {
                if (is_io && !found_mixbar) {
                    priv->mixer_base = base;
                    found_mixbar = true;
                    log_info(AC97_TAG, "Using mixer interface at I/O port 0x%X", base);
                } else if (is_io && !found_bmbar) {
                    priv->bus_base = base;
                    found_bmbar = true;
                    log_info(AC97_TAG, "Using bus master interface at I/O port 0x%X", base);
                }
            }
            
            if (found_mixbar && found_bmbar) {
                break;
            }
        }
    }
    
    // Check if we found the required base addresses
    if (!found_mixbar || !found_bmbar) {
        log_error(AC97_TAG, "Failed to find mixer or bus master interface");
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Store IRQ number
    priv->irq = dev->id.interrupt_line;
    
    log_info(AC97_TAG, "Using IRQ %d", priv->irq);
    
    // Allocate buffer descriptor lists with 32-byte alignment
    priv->play_bdl = (ac97_bdl_entry_t*)hal_memory_allocate(sizeof(ac97_bdl_entry_t) * AC97_BDL_SIZE, 32);
    if (!priv->play_bdl) {
        log_error(AC97_TAG, "Failed to allocate playback BDL");
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    priv->record_bdl = (ac97_bdl_entry_t*)hal_memory_allocate(sizeof(ac97_bdl_entry_t) * AC97_BDL_SIZE, 32);
    if (!priv->record_bdl) {
        log_error(AC97_TAG, "Failed to allocate recording BDL");
        hal_memory_free(priv->play_bdl);
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Clear BDLs
    memset(priv->play_bdl, 0, sizeof(ac97_bdl_entry_t) * AC97_BDL_SIZE);
    memset(priv->record_bdl, 0, sizeof(ac97_bdl_entry_t) * AC97_BDL_SIZE);
    
    // Get physical addresses of BDLs
    priv->play_bdl_phys = (uint32_t)hal_memory_get_physical(priv->play_bdl);
    priv->record_bdl_phys = (uint32_t)hal_memory_get_physical(priv->record_bdl);
    
    // Register interrupt handler
    int irq_result = hal_interrupt_register_handler(priv->irq, ac97_interrupt_handler, dev);
    if (irq_result != 0) {
        log_error(AC97_TAG, "Failed to register interrupt handler");
        hal_memory_free(priv->play_bdl);
        hal_memory_free(priv->record_bdl);
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Reset and configure the device
    int reset_result = ac97_reset(dev);
    if (reset_result != 0) {
        log_error(AC97_TAG, "Failed to reset device");
        hal_interrupt_unregister_handler(priv->irq);
        hal_memory_free(priv->play_bdl);
        hal_memory_free(priv->record_bdl);
        heap_free(priv);
        dev->private_data = NULL;
        return -1;
    }
    
    // Create a device in the device manager
    device_t* audio_device = (device_t*)heap_alloc(sizeof(device_t));
    if (audio_device) {
        memset(audio_device, 0, sizeof(device_t));
        
        snprintf(audio_device->name, sizeof(audio_device->name), "ac97_%d", 0);
        audio_device->type = DEVICE_TYPE_AUDIO;
        audio_device->status = DEVICE_STATUS_ENABLED;
        audio_device->vendor_id = dev->id.vendor_id;
        audio_device->device_id = dev->id.device_id;
        audio_device->irq = priv->irq;
        audio_device->private_data = dev;
        
        // Register the device with the device manager
        device_register(audio_device);
        
        // Store OS device in PCI device structure
        dev->os_device = audio_device;
        
        log_info(AC97_TAG, "Registered audio device '%s'", audio_device->name);
    } else {
        log_warning(AC97_TAG, "Failed to create device manager entry");
    }
    
    log_info(AC97_TAG, "AC97 initialization complete");
    return 0;
}

/**
 * Remove the AC97 device
 */
static int ac97_remove(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    ac97_device_t* priv = (ac97_device_t*)dev->private_data;
    
    log_info(AC97_TAG, "Removing AC97 audio controller");
    
    // Stop playback and recording
    ac97_stop_playback(priv);
    ac97_stop_recording(priv);
    
    // Unregister interrupt handler
    hal_interrupt_unregister_handler(priv->irq);
    
    // Free BDLs
    if (priv->play_bdl) {
        hal_memory_free(priv->play_bdl);
    }
    
    if (priv->record_bdl) {
        hal_memory_free(priv->record_bdl);
    }
    
    // Free play and record buffers
    for (int i = 0; i < AC97_BDL_SIZE; i++) {
        if (priv->play_buffers[i]) {
            hal_memory_free(priv->play_buffers[i]);
        }
        
        if (priv->record_buffers[i]) {
            hal_memory_free(priv->record_buffers[i]);
        }
    }
    
    // Unregister from device manager
    if (dev->os_device) {
        device_unregister(dev->os_device);
        heap_free(dev->os_device);
        dev->os_device = NULL;
    }
    
    // Free private data
    heap_free(priv);
    dev->private_data = NULL;
    
    log_info(AC97_TAG, "AC97 removed successfully");
    return 0;
}

/**
 * Suspend the AC97 device
 */
static int ac97_suspend(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    ac97_device_t* priv = (ac97_device_t*)dev->private_data;
    
    log_info(AC97_TAG, "Suspending AC97 audio controller");
    
    // Stop playback and recording
    ac97_stop_playback(priv);
    ac97_stop_recording(priv);
    
    return 0;
}

/**
 * Resume the AC97 device
 */
static int ac97_resume(pci_device_t* dev) {
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    log_info(AC97_TAG, "Resuming AC97 audio controller");
    
    // Reset and reconfigure the device
    return ac97_reset(dev);
}