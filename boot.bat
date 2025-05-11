@echo off
REM Multi-boot script for uintOS (Windows version)
REM This script provides different options for booting the OS

REM Check if image exists
set DISK_IMG=disk.img
if not exist "%DISK_IMG%" (
    echo Error: %DISK_IMG% not found. Run 'make' first.
    exit /b 1
)

echo ====================================
echo    uintOS Boot Options Menu        
echo ====================================
echo.
echo 1. Boot with QEMU (default)
echo 2. Boot with VirtualBox
echo 3. Boot with VMware
echo 4. Create bootable USB
echo 5. Exit
echo.
set /p option=Select an option [1-5]: 

if "%option%"=="" set option=1

if "%option%"=="1" (
    echo Booting uintOS with QEMU...
    where qemu-system-i386 >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        qemu-system-i386 -machine q35 -fda "%DISK_IMG%" -m 128M
    ) else (
        where qemu-system-x86_64 >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            qemu-system-x86_64 -machine q35 -fda "%DISK_IMG%" -m 128M
        ) else (
            echo Error: QEMU is not installed. Please install QEMU first.
            exit /b 1
        )
    )
) else if "%option%"=="2" (
    echo Creating VirtualBox VM for uintOS...
    where VBoxManage >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        set VM_NAME=uintOS
        
        REM Check if VM already exists
        VBoxManage showvminfo "%VM_NAME%" >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            echo VM '%VM_NAME%' already exists. Using existing VM.
        ) else (
            echo Creating new VirtualBox VM...
            VBoxManage createvm --name "%VM_NAME%" --ostype "Other" --register
            VBoxManage modifyvm "%VM_NAME%" --memory 128 --ioapic on
            VBoxManage storagectl "%VM_NAME%" --name "Floppy Controller" --add floppy
            
            REM Convert disk image to VDI if needed
            if not exist "%DISK_IMG%.vdi" (
                VBoxManage convertfromraw "%DISK_IMG%" "%DISK_IMG%.vdi" --format VDI
            )
            
            REM Attach the disk
            VBoxManage storageattach "%VM_NAME%" --storagectl "Floppy Controller" --port 0 --device 0 --type fdd --medium "%DISK_IMG%"
        )
        
        echo Starting VirtualBox VM...
        VBoxManage startvm "%VM_NAME%"
    ) else (
        echo Error: VirtualBox is not installed. Please install VirtualBox first.
        exit /b 1
    )
) else if "%option%"=="3" (
    echo Creating VMware configuration for uintOS...
    where vmrun >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        set HAS_VMWARE=1
    ) else (
        where vmplayer >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            set HAS_VMWARE=1
        ) else (
            set HAS_VMWARE=0
        )
    )
    
    if "%HAS_VMWARE%"=="1" (
        REM Create VMware config file
        set VMX_FILE=uintos.vmx
        echo Creating VMware configuration file...
        
        echo .encoding = "UTF-8" > "%VMX_FILE%"
        echo config.version = "8" >> "%VMX_FILE%"
        echo virtualHW.version = "18" >> "%VMX_FILE%"
        echo memsize = "128" >> "%VMX_FILE%"
        echo displayName = "uintOS" >> "%VMX_FILE%"
        echo guestOS = "other" >> "%VMX_FILE%"
        echo floppy0.present = "TRUE" >> "%VMX_FILE%"
        echo floppy0.fileType = "file" >> "%VMX_FILE%"
        echo floppy0.fileName = "%DISK_IMG%" >> "%VMX_FILE%"
        
        REM Start the VM
        where vmrun >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            echo Starting VM with VMware...
            vmrun start "%VMX_FILE%" nogui
        ) else (
            echo Starting VM with VMware Player...
            vmplayer "%VMX_FILE%"
        )
    ) else (
        echo Error: VMware is not installed. Please install VMware first.
        exit /b 1
    )
) else if "%option%"=="4" (
    echo Creating bootable USB for uintOS...
    echo WARNING: This will erase all data on the selected USB device!
    echo.
    
    REM List available disks
    echo Available drives:
    wmic diskdrive list brief
    
    set /p usb_drive=Enter USB drive letter (e.g., E:): 
    
    if exist "%usb_drive%\" (
        echo WARNING: All data on %usb_drive% will be erased!
        set /p confirm=Are you sure you want to continue? (y/n): 
        
        if /i "%confirm%"=="y" (
            REM Write image to USB using Windows tools
            echo Writing disk image to USB device...
            
            where dd >nul 2>&1
            if %ERRORLEVEL% EQU 0 (
                dd if=%DISK_IMG% of=\\.\%usb_drive% bs=4M
                echo USB device created successfully.
                echo You can now boot your computer from this USB device.
            ) else (
                echo Error: 'dd' command not found. Please install dd for Windows.
                echo You can get it from http://www.chrysocome.net/dd
                exit /b 1
            )
        ) else (
            echo Operation cancelled.
        )
    ) else (
        echo Error: Invalid drive letter.
        exit /b 1
    )
) else if "%option%"=="5" (
    echo Exiting...
    exit /b 0
) else (
    echo Invalid option. Exiting.
    exit /b 1
)