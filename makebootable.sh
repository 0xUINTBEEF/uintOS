#!/bin/bash
# makebootable.sh - Create a bootable USB drive for uintOS
# This script creates a bootable USB drive with proper partitioning,
# bootloader installation, and kernel setup for real hardware.

# Define colors for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
RESET='\033[0m'

# Function to check if script is run as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo -e "${RED}Error: This script must be run as root${RESET}"
        echo "Please run with: sudo $0"
        exit 1
    fi
}

# Function to check if necessary tools are available
check_tools() {
    local missing_tools=()
    
    for tool in dd parted mkfs.fat mcopy sfdisk fdisk losetup mktemp; do
        if ! command -v "$tool" &> /dev/null; then
            missing_tools+=("$tool")
        fi
    done
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        echo -e "${RED}Error: Required tools not found:${RESET}"
        printf "  - %s\n" "${missing_tools[@]}"
        echo ""
        echo "Please install them with:"
        echo "  sudo apt-get install dosfstools mtools parted util-linux"
        exit 1
    fi
}

# Function to create a bootable disk image
create_disk_image() {
    echo -e "${BLUE}Creating bootable disk image...${RESET}"
    
    # Create a 32MB disk image
    dd if=/dev/zero of="bootable.img" bs=1M count=32
    
    # Create a partition table
    parted -s "bootable.img" mklabel msdos
    parted -s "bootable.img" mkpart primary fat32 1MiB 100%
    parted -s "bootable.img" set 1 boot on
    
    # Setup loopback device
    LOOPDEV=$(losetup -f)
    losetup -P "$LOOPDEV" "bootable.img"
    
    # Format the partition
    echo -e "${YELLOW}Formatting partition...${RESET}"
    mkfs.fat -F 32 "${LOOPDEV}p1"
    
    # Create a mount point
    MOUNTDIR=$(mktemp -d)
    
    # Mount the partition
    mount "${LOOPDEV}p1" "$MOUNTDIR"
    
    # Create directory structure
    mkdir -p "$MOUNTDIR/boot"
    
    # Copy bootloader and kernel
    echo -e "${YELLOW}Copying bootloader and kernel...${RESET}"
    dd if="build/boot/bootloader" of="$LOOPDEV" bs=446 count=1 conv=notrunc
    cp "build/kern/kernel" "$MOUNTDIR/boot/kernel.bin"
    
    # Create simple boot configuration
    echo "uintOS Bootable USB - $(date)" > "$MOUNTDIR/boot/boot.txt"
    
    # Unmount and clean up
    umount "$MOUNTDIR"
    rmdir "$MOUNTDIR"
    losetup -d "$LOOPDEV"
    
    echo -e "${GREEN}Disk image 'bootable.img' created successfully!${RESET}"
}

# Function to write image to a USB drive
write_to_usb() {
    echo -e "${BLUE}${BOLD}Available storage devices:${RESET}"
    lsblk -d -o NAME,SIZE,MODEL | grep -v "loop"
    
    echo -e "\n${YELLOW}WARNING: ALL DATA ON THE SELECTED DEVICE WILL BE ERASED!${RESET}"
    echo -e "${YELLOW}Please enter the device name (e.g., sdb, sdc, NOT sdb1 or sdc1):${RESET}"
    read -r target_device
    
    # Check if device exists
    if [ ! -b "/dev/${target_device}" ]; then
        echo -e "${RED}Error: /dev/${target_device} is not a valid block device${RESET}"
        exit 1
    fi
    
    # Verify that this is a removable device
    if ! grep -q 1 "/sys/block/${target_device}/removable" 2>/dev/null; then
        echo -e "${RED}Warning: /dev/${target_device} does not appear to be a removable device${RESET}"
        echo -e "${YELLOW}Are you ABSOLUTELY SURE you want to continue? This could erase your hard drive!${RESET}"
        echo -e "Type ${BOLD}'YES I AM SURE'${RESET} to continue:"
        read -r confirmation
        
        if [ "$confirmation" != "YES I AM SURE" ]; then
            echo -e "${GREEN}Operation cancelled.${RESET}"
            exit 0
        fi
    fi
    
    # Final confirmation
    echo -e "${RED}${BOLD}WARNING: This will ERASE ALL DATA on /dev/${target_device}${RESET}"
    echo -e "${RED}Type 'yes' to confirm:${RESET}"
    read -r confirmation
    
    if [ "$confirmation" != "yes" ]; then
        echo -e "${GREEN}Operation cancelled.${RESET}"
        exit 0
    fi
    
    # Unmount any mounted partitions from the device
    echo "Unmounting any partitions on /dev/${target_device}..."
    for partition in $(ls /dev/${target_device}* 2>/dev/null); do
        if mount | grep -q "$partition"; then
            umount "$partition" || { echo "Failed to unmount $partition"; exit 1; }
        fi
    done
    
    # Write the image
    echo -e "${YELLOW}Writing image to /dev/${target_device}...${RESET}"
    dd if=bootable.img of=/dev/${target_device} bs=4M status=progress oflag=sync
    
    # Force kernel to reread partition table
    blockdev --rereadpt /dev/${target_device}
    
    echo -e "${GREEN}${BOLD}USB drive successfully created!${RESET}"
    echo -e "${BLUE}You can now boot your computer from this USB drive.${RESET}"
}

# Main function
main() {
    check_root
    check_tools
    
    echo -e "${BLUE}${BOLD}=====================================${RESET}"
    echo -e "${BLUE}${BOLD}  uintOS USB Boot Creator           ${RESET}"
    echo -e "${BLUE}${BOLD}=====================================${RESET}"
    
    # Check if build files exist
    if [ ! -f "build/boot/bootloader" ] || [ ! -f "build/kern/kernel" ]; then
        echo -e "${RED}Error: Bootloader or kernel not found!${RESET}"
        echo "Please run 'make' to build the OS first."
        exit 1
    fi
    
    echo -e "\n${YELLOW}Select an option:${RESET}"
    echo -e "1. Create bootable disk image only"
    echo -e "2. Create bootable disk image and write to USB"
    echo -e "3. Write existing bootable.img to USB"
    echo -e "4. Exit"
    
    read -r option
    
    case $option in
        1)
            create_disk_image
            ;;
        2)
            create_disk_image
            write_to_usb
            ;;
        3)
            if [ ! -f "bootable.img" ]; then
                echo -e "${RED}Error: bootable.img not found!${RESET}"
                exit 1
            fi
            write_to_usb
            ;;
        4|*)
            echo -e "${GREEN}Exiting.${RESET}"
            exit 0
            ;;
    esac
}

# Run the script
main