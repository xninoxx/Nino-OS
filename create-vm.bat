@echo off
rem Creates a VirtualBox virtual machine for NinoOS (Windows).
rem Needs VirtualBox installed. Keep this folder: it will also hold the NinoOS disk (NinoOS.vdi).
setlocal
cd /d "%~dp0"
set "VBM=%ProgramFiles%\Oracle\VirtualBox\VBoxManage.exe"
if not exist "%VBM%" (
    echo VBoxManage was not found. Install VirtualBox first: https://www.virtualbox.org/
    pause
    exit /b 1
)
if not exist "%~dp0ninoos.iso" (
    echo ninoos.iso is not in this folder.
    pause
    exit /b 1
)
"%VBM%" showvminfo NinoOS >nul 2>&1
if %errorlevel%==0 (
    echo A virtual machine called NinoOS already exists.
    echo Start it from VirtualBox, or delete it there first ^(choose "Delete all files"^).
    pause
    exit /b 1
)

echo Creating the NinoOS virtual machine...
"%VBM%" createvm --name NinoOS --ostype Other --register || goto :error
"%VBM%" modifyvm NinoOS --memory 256 --vram 16 --cpus 1 --graphicscontroller vboxvga --firmware bios --chipset piix3 --ioapic off --pae off --mouse ps2 --keyboard ps2 --nic1 nat --nictype1 82540EM --cableconnected1 on --boot1 dvd --boot2 disk --boot3 none --boot4 none || goto :error
"%VBM%" modifyvm NinoOS --audio-driver none >nul 2>&1 || "%VBM%" modifyvm NinoOS --audio none >nul 2>&1
"%VBM%" storagectl NinoOS --name IDE --add ide --controller PIIX4 || goto :error
"%VBM%" createmedium disk --filename "%~dp0NinoOS.vdi" --size 64 --format VDI || goto :error
"%VBM%" storageattach NinoOS --storagectl IDE --port 0 --device 0 --type hdd --medium "%~dp0NinoOS.vdi" || goto :error
"%VBM%" storageattach NinoOS --storagectl IDE --port 1 --device 0 --type dvddrive --medium "%~dp0ninoos.iso" || goto :error

echo.
echo Done. Starting NinoOS...
echo   - Click inside the window to capture the mouse. Press the RIGHT Ctrl key to get it back.
"%VBM%" startvm NinoOS
exit /b 0

:error
echo Something went wrong. Please send a screenshot of this window to the person who gave you NinoOS.
pause
exit /b 1
