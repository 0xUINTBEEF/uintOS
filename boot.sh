#!/bin/bash
# Multi-boot script for uintOS
# This script provides different options for booting the OS

# Colors for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
RESET='\033[0m'

# Check if image exists
DISK_IMG="disk.img"
if [ ! -f "$DISK_IMG" ]; then
    echo -e "${RED}Error: $DISK_IMG not found. Run 'make' first.${RESET}"
    exit 1
fi

# Function to check if a command exists
command_exists() {
    command -v "$1" &> /dev/null
}

echo -e "${BLUE}====================================${RESET}"
echo -e "${BLUE}   uintOS Boot Options Menu        ${RESET}"
echo -e "${BLUE}====================================${RESET}"
echo
echo -e "${YELLOW}1. Boot with QEMU (default)${RESET}"
echo -e "${YELLOW}2. Boot with VirtualBox${RESET}"
echo -e "${YELLOW}3. Boot with VMware${RESET}"
echo -e "${YELLOW}4. Create bootable USB${RESET}"
echo -e "${YELLOW}5. Exit${RESET}"
echo
echo -n "Select an option [1-5]: "
read option

case $option in
    1|"")
        # QEMU boot
        echo -e "${GREEN}Booting uintOS with QEMU...${RESET}"
        if command_exists qemu-system-i386; then
            qemu-system-i386 -machine q35 -fda "$DISK_IMG" -m 128M 
        elif command_exists qemu-system-x86_64; then
            qemu-system-x86_64 -machine q35 -fda "$DISK_IMG" -m 128M
        else
            echo -e "${RED}Error: QEMU is not installed. Please install QEMU first.${RESET}"
            exit 1
        fi
        ;;
    2)
        # VirtualBox boot
        echo -e "${GREEN}Creating VirtualBox VM for uintOS...${RESET}"
        if command_exists VBoxManage; then
            VM_NAME="uintOS"
            
            # Check if VM already exists
            if VBoxManage showvminfo "$VM_NAME" &>/dev/null; then
                echo -e "${YELLOW}VM '$VM_NAME' already exists. Using existing VM.${RESET}"
            else
                echo "Creating new VirtualBox VM..."
                VBoxManage createvm --name "$VM_NAME" --ostype "Other" --register
                VBoxManage modifyvm "$VM_NAME" --memory 128 --ioapic on
                VBoxManage storagectl "$VM_NAME" --name "Floppy Controller" --add floppy
                
                # Convert disk image to VDI if needed
                if [ ! -f "${DISK_IMG}.vdi" ]; then
                    VBoxManage convertfromraw "$DISK_IMG" "${DISK_IMG}.vdi" --format VDI
                fi
                
                # Attach the disk
                VBoxManage storageattach "$VM_NAME" --storagectl "Floppy Controller" --port 0 --device 0 --type fdd --medium "$DISK_IMG"
            fi
            
            echo "Starting VirtualBox VM..."
            VBoxManage startvm "$VM_NAME"
        else
            echo -e "${RED}Error: VirtualBox is not installed. Please install VirtualBox first.${RESET}"
            exit 1
        fi
        ;;
    3)
        # VMware boot
        echo -e "${GREEN}Creating VMware configuration for uintOS...${RESET}"
        if command_exists vmrun || command_exists vmplayer; then
            # Create VMware config file
            VMX_FILE="uintos.vmx"
            echo "Creating VMware configuration file..."
            cat > "$VMX_FILE" << EOF
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "18"
memsize = "128"
displayName = "uintOS"
guestOS = "other"
floppy0.present = "TRUE"
floppy0.fileType = "file"
floppy0.fileName = "$DISK_IMG"
EOF
            
            # Start the VM
            if command_exists vmrun; then
                echo "Starting VM with VMware..."
                vmrun start "$VMX_FILE" nogui
            elif command_exists vmplayer; then
                echo "Starting VM with VMware Player..."
                vmplayer "$VMX_FILE"
            fi
        else
            echo -e "${RED}Error: VMware is not installed. Please install VMware first.${RESET}"
            exit 1
        fi
        ;;
    4)
        # Create bootable USB
        echo -e "${GREEN}Creating bootable USB for uintOS...${RESET}"
        echo -e "${YELLOW}WARNING: This will erase all data on the selected USB device!${RESET}"
        
        # List available disks
        if command_exists lsblk; then
            echo "Available devices:"
            lsblk -d -o NAME,SIZE,MODEL | grep -v "^loop"
        elif command_exists diskutil; then
            echo "Available devices:"
            diskutil list
        fi
        
        echo -n "Enter USB device path (e.g., /dev/sdb or /dev/disk2): "
        read usb_dev
        
        if [ -b "$usb_dev" ] || [[ "$usb_dev" == /dev/disk* ]]; then
            echo -e "${RED}WARNING: All data on $usb_dev will be erased!${RESET}"
            echo -n "Are you sure you want to continue? (y/n): "
            read confirm
            
            if [[ $confirm == "y" || $confirm == "Y" ]]; then
                # Write image to USB
                if command_exists dd; then
                    echo "Writing disk image to USB device..."
                    if [[ "$usb_dev" == /dev/disk* ]]; then
                        # macOS requires unmounting first
                        diskutil unmountDisk "$usb_dev"
                    fi
                    sudo dd if="$DISK_IMG" of="$usb_dev" bs=4M status=progress && sync
                    echo -e "${GREEN}USB device created successfully.${RESET}"
                    echo -e "${YELLOW}You can now boot your computer from this USB device.${RESET}"
                else
                    echo -e "${RED}Error: 'dd' command not found.${RESET}"
                    exit 1
                fi
            else
                echo "Operation cancelled."
            fi
        else
            echo -e "${RED}Error: Invalid device path.${RESET}"
            exit 1
        fi
        ;;
    5)
        echo "Exiting..."
        exit 0
        ;;
    *)
        echo -e "${RED}Invalid option. Exiting.${RESET}"
        exit 1
        ;;
esac