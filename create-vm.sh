#!/bin/sh
# Creates a VirtualBox virtual machine for NinoOS (Linux and macOS).
# Needs VirtualBox installed. Keep this folder: it will also hold the NinoOS disk (NinoOS.vdi).
set -e
cd "$(dirname "$0")"
DIR="$(pwd)"
VBM=VBoxManage

if ! command -v "$VBM" >/dev/null 2>&1; then
    echo "VBoxManage was not found. Install VirtualBox first: https://www.virtualbox.org/"
    exit 1
fi
if [ ! -f "$DIR/ninoos.iso" ]; then
    echo "ninoos.iso is not in this folder ($DIR)."
    exit 1
fi
if "$VBM" showvminfo NinoOS >/dev/null 2>&1; then
    echo "A virtual machine called NinoOS already exists."
    echo "Start it from VirtualBox, or delete it there first (and choose 'Delete all files')."
    exit 1
fi

echo "Creating the NinoOS virtual machine..."
"$VBM" createvm --name NinoOS --ostype Other --register
"$VBM" modifyvm NinoOS --memory 256 --vram 16 --cpus 1 --graphicscontroller vboxvga \
    --firmware bios --chipset piix3 --ioapic off --pae off \
    --mouse ps2 --keyboard ps2 \
    --nic1 nat --nictype1 82540EM --cableconnected1 on \
    --boot1 dvd --boot2 disk --boot3 none --boot4 none
"$VBM" modifyvm NinoOS --audio-driver none 2>/dev/null || "$VBM" modifyvm NinoOS --audio none 2>/dev/null || true

"$VBM" storagectl NinoOS --name IDE --add ide --controller PIIX4
"$VBM" createmedium disk --filename "$DIR/NinoOS.vdi" --size 64 --format VDI
"$VBM" storageattach NinoOS --storagectl IDE --port 0 --device 0 --type hdd --medium "$DIR/NinoOS.vdi"
"$VBM" storageattach NinoOS --storagectl IDE --port 1 --device 0 --type dvddrive --medium "$DIR/ninoos.iso"

echo
echo "Done. Starting NinoOS..."
echo "  - Click inside the window to capture the mouse. Press the RIGHT Ctrl key to get it back."
"$VBM" startvm NinoOS
