/**
 * @file ac97.h
 * @brief AC97 Sound Card Driver for uintOS
 *
 * This driver provides support for AC97-compatible sound cards
 * using the PCI driver framework.
 */

#ifndef UINTOS_AC97_H
#define UINTOS_AC97_H

#include <stdint.h>
#include <stdbool.h>
#include "../pci/pci.h"
#include "../../kernel/device_manager.h"

// Common PCI vendor IDs for AC97 controllers
#define AC97_INTEL_VENDOR_ID      0x8086   // Intel
#define AC97_NVIDIA_VENDOR_ID     0x10DE   // NVIDIA
#define AC97_AMD_VENDOR_ID        0x1022   // AMD
#define AC97_SIS_VENDOR_ID        0x1039   // SiS

// Intel device IDs for AC97 controllers
#define AC97_INTEL_82801AA        0x2415   // 82801AA (ICH)
#define AC97_INTEL_82801AB        0x2425   // 82801AB (ICH0)
#define AC97_INTEL_82801BA        0x2445   // 82801BA (ICH2)
#define AC97_INTEL_82801CA        0x2485   // 82801CA (ICH3)
#define AC97_INTEL_82801DB        0x24C5   // 82801DB (ICH4)
#define AC97_INTEL_82801EB        0x24D5   // 82801EB (ICH5)
#define AC97_INTEL_82801FB        0x266E   // 82801FB (ICH6)
#define AC97_INTEL_82801GB        0x27DE   // 82801GB (ICH7)
#define AC97_INTEL_82801HB        0x284B   // 82801HB (ICH8)
#define AC97_INTEL_82801I         0x293E   // 82801I (ICH9)

// AC97 interface registers
#define AC97_REG_RESET           0x00      // Reset register
#define AC97_REG_MASTER_VOL      0x02      // Master volume
#define AC97_REG_HEADPHONE_VOL   0x04      // Headphone volume
#define AC97_REG_MASTER_MONO_VOL 0x06      // Master mono volume
#define AC97_REG_PC_BEEP_VOL     0x0A      // PC beep volume
#define AC97_REG_PHONE_VOL       0x0C      // Phone volume
#define AC97_REG_MIC_VOL         0x0E      // Mic volume
#define AC97_REG_LINE_IN_VOL     0x10      // Line in volume
#define AC97_REG_CD_VOL          0x12      // CD volume
#define AC97_REG_VIDEO_VOL       0x14      // Video volume
#define AC97_REG_AUX_VOL         0x16      // Aux volume
#define AC97_REG_PCM_OUT_VOL     0x18      // PCM out volume
#define AC97_REG_RECORD_SELECT   0x1A      // Record select
#define AC97_REG_RECORD_GAIN     0x1C      // Record gain
#define AC97_REG_RECORD_GAIN_MIC 0x1E      // Record gain mic
#define AC97_REG_GENERAL_PURPOSE 0x20      // General purpose
#define AC97_REG_3D_CONTROL      0x22      // 3D control
#define AC97_REG_EXTENDED_ID     0x28      // Extended audio ID
#define AC97_REG_EXTENDED_STATUS 0x2A      // Extended audio status
#define AC97_REG_PCM_FRONT_DAC_RATE 0x2C   // PCM front DAC rate
#define AC97_REG_PCM_SURR_DAC_RATE 0x2E    // PCM surround DAC rate
#define AC97_REG_PCM_LFE_DAC_RATE  0x30    // PCM LFE DAC rate
#define AC97_REG_VENDOR_ID1      0x7C      // Vendor ID1
#define AC97_REG_VENDOR_ID2      0x7E      // Vendor ID2

// AC97 native audio bus (NABM) registers
#define AC97_NABM_PCMOUT_BDBAR   0x10      // PCM out buffer descriptor BAR
#define AC97_NABM_PCMOUT_CIV     0x14      // PCM out current index value
#define AC97_NABM_PCMOUT_LVI     0x15      // PCM out last valid index
#define AC97_NABM_PCMOUT_SR      0x16      // PCM out status register
#define AC97_NABM_PCMOUT_PICB    0x18      // PCM out position in current buffer
#define AC97_NABM_PCMOUT_PIV     0x1A      // PCM out prefetched index value
#define AC97_NABM_PCMIN_BDBAR    0x20      // PCM in buffer descriptor BAR
#define AC97_NABM_PCMIN_CIV      0x24      // PCM in current index value
#define AC97_NABM_PCMIN_LVI      0x25      // PCM in last valid index
#define AC97_NABM_PCMIN_SR       0x26      // PCM in status register
#define AC97_NABM_PCMIN_PICB     0x28      // PCM in position in current buffer
#define AC97_NABM_PCMIN_PIV      0x2A      // PCM in prefetched index value
#define AC97_NABM_MICIN_BDBAR    0x30      // MIC in buffer descriptor BAR
#define AC97_NABM_MICIN_CIV      0x34      // MIC in current index value
#define AC97_NABM_MICIN_LVI      0x35      // MIC in last valid index
#define AC97_NABM_MICIN_SR       0x36      // MIC in status register
#define AC97_NABM_MICIN_PICB     0x38      // MIC in position in current buffer
#define AC97_NABM_MICIN_PIV      0x3A      // MIC in prefetched index value
#define AC97_NABM_GLOB_CNT       0x2C      // Global control register
#define AC97_NABM_GLOB_STA       0x30      // Global status register

// AC97 buffer descriptor list (BDL) entry
typedef struct {
    uint32_t buffer_addr;        // Physical address of buffer
    uint16_t buffer_samples;     // Buffer size in samples (0 = 65536)
    uint16_t flags;              // Flags (bit 15 = IOC - interrupt on completion)
} __attribute__((packed)) ac97_bdl_entry_t;

// Maximum number of buffer descriptors
#define AC97_BDL_SIZE           32

// Buffer size for each descriptor (in bytes)
#define AC97_BUFFER_SIZE        4096

// Sample rate for audio playback and capture
#define AC97_SAMPLE_RATE_48K    48000

// Audio format for playback and capture
#define AC97_FORMAT_STEREO      0x01
#define AC97_FORMAT_16BIT       0x02

// AC97 private device structure
typedef struct {
    uint32_t mixer_base;          // Mixer interface base address
    uint32_t bus_base;            // Bus master interface base address
    uint8_t irq;                  // IRQ number
    
    // Buffer descriptor list for playback
    ac97_bdl_entry_t* play_bdl;   // Buffer descriptor list for playback
    uint32_t play_bdl_phys;       // Physical address of BDL
    uint8_t* play_buffers[AC97_BDL_SIZE]; // Play buffer pointers
    uint32_t play_buffers_phys[AC97_BDL_SIZE]; // Play buffer physical addresses
    uint8_t play_lvi;            // Last valid index for playback
    bool play_active;            // Playback active flag
    
    // Buffer descriptor list for recording
    ac97_bdl_entry_t* record_bdl;  // Buffer descriptor list for recording
    uint32_t record_bdl_phys;      // Physical address of BDL
    uint8_t* record_buffers[AC97_BDL_SIZE]; // Record buffer pointers
    uint32_t record_buffers_phys[AC97_BDL_SIZE]; // Record buffer physical addresses
    uint8_t record_lvi;           // Last valid index for recording
    bool record_active;           // Recording active flag
    
    // Codec information
    uint32_t vendor_id;           // Vendor ID from codec
    uint32_t codec_id;            // Codec ID
    bool supports_variable_rate;  // Variable rate support
    
    // Statistics
    uint32_t bytes_played;        // Total bytes played
    uint32_t bytes_recorded;      // Total bytes recorded
} ac97_device_t;

/**
 * Initialize AC97 driver
 * 
 * @return 0 on success, negative error code on failure
 */
int ac97_init(void);

/**
 * Cleanup AC97 driver
 */
void ac97_exit(void);

/**
 * Reset and initialize an AC97 device
 * 
 * @param dev The PCI device structure
 * @return 0 on success, negative error code on failure
 */
int ac97_reset(pci_device_t* dev);

/**
 * Read a value from AC97 mixer register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read
 */
uint16_t ac97_mixer_read(ac97_device_t* priv, uint8_t reg);

/**
 * Write a value to AC97 mixer register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write
 */
void ac97_mixer_write(ac97_device_t* priv, uint8_t reg, uint16_t value);

/**
 * Read a value from AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read (8-bit)
 */
uint8_t ac97_bus_read8(ac97_device_t* priv, uint8_t reg);

/**
 * Read a value from AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read (16-bit)
 */
uint16_t ac97_bus_read16(ac97_device_t* priv, uint8_t reg);

/**
 * Read a value from AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @return Value read (32-bit)
 */
uint32_t ac97_bus_read32(ac97_device_t* priv, uint8_t reg);

/**
 * Write a value to AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write (8-bit)
 */
void ac97_bus_write8(ac97_device_t* priv, uint8_t reg, uint8_t value);

/**
 * Write a value to AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write (16-bit)
 */
void ac97_bus_write16(ac97_device_t* priv, uint8_t reg, uint16_t value);

/**
 * Write a value to AC97 bus master register
 * 
 * @param priv Device private data
 * @param reg Register offset
 * @param value Value to write (32-bit)
 */
void ac97_bus_write32(ac97_device_t* priv, uint8_t reg, uint32_t value);

/**
 * Start audio playback
 * 
 * @param priv Device private data
 * @param buffers Array of buffer pointers
 * @param buffer_count Number of buffers
 * @param buffer_size Size of each buffer in bytes
 * @return 0 on success, negative error code on failure
 */
int ac97_start_playback(ac97_device_t* priv, uint8_t** buffers, uint8_t buffer_count, uint16_t buffer_size);

/**
 * Stop audio playback
 * 
 * @param priv Device private data
 */
void ac97_stop_playback(ac97_device_t* priv);

/**
 * Start audio recording
 * 
 * @param priv Device private data
 * @param buffers Array of buffer pointers
 * @param buffer_count Number of buffers
 * @param buffer_size Size of each buffer in bytes
 * @return 0 on success, negative error code on failure
 */
int ac97_start_recording(ac97_device_t* priv, uint8_t** buffers, uint8_t buffer_count, uint16_t buffer_size);

/**
 * Stop audio recording
 * 
 * @param priv Device private data
 */
void ac97_stop_recording(ac97_device_t* priv);

/**
 * Set playback volume
 * 
 * @param priv Device private data
 * @param left Left channel volume (0-31, where 0 is maximum and 31 is minimum)
 * @param right Right channel volume (0-31, where 0 is maximum and 31 is minimum)
 * @param mute Mute flag
 * @return 0 on success, negative error code on failure
 */
int ac97_set_playback_volume(ac97_device_t* priv, uint8_t left, uint8_t right, bool mute);

/**
 * Set recording volume
 * 
 * @param priv Device private data
 * @param left Left channel volume (0-15, where 0 is minimum and 15 is maximum)
 * @param right Right channel volume (0-15, where 0 is minimum and 15 is maximum)
 * @param mute Mute flag
 * @return 0 on success, negative error code on failure
 */
int ac97_set_recording_volume(ac97_device_t* priv, uint8_t left, uint8_t right, bool mute);

/**
 * Set recording source
 * 
 * @param priv Device private data
 * @param source Recording source (0=mic, 1=CD, 2=video, 3=aux, 4=line in, 5=stereo mix, 6=mono mix, 7=phone)
 * @return 0 on success, negative error code on failure
 */
int ac97_set_recording_source(ac97_device_t* priv, uint8_t source);

/**
 * AC97 interrupt handler
 * 
 * @param dev The PCI device structure
 */
void ac97_interrupt(pci_device_t* dev);

#endif /* UINTOS_AC97_H */