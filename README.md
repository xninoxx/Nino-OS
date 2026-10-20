# NinoOS 0.7

NinoOS is a small operating system with its own kernel (**NinoKernel**), written from scratch:
a graphical desktop, file manager, paint program, text editor, terminal, a network stack and a web
browser (with HTTPS). It is a hobby project, so things can break. It runs in a virtual machine and
cannot harm your computer.

This README has three parts:

1. **Try NinoOS** (for friends): Windows, macOS, Linux
2. **Using NinoOS**: the desktop, the apps and every terminal command
3. **Building NinoOS** (for the developer): the `make` commands

---

## 1. Try NinoOS

You need two things: a virtual machine program, and the file **`ninoos.iso`** (the whole operating
system). NinoOS is a 32-bit PC system (x86), so it needs a VM that emulates a PC.

What the virtual machine needs (the scripts below set this up for you):

| Setting | Value |
|---|---|
| Type | Other / Other-Unknown (32-bit) |
| Memory | **256 MB** |
| Boot | BIOS (not EFI), boot from the ISO |
| Graphics | VBoxVGA, **16 MB** video memory |
| Mouse / keyboard | **PS/2** mouse (not USB tablet) |
| Network | NAT, **Intel PRO/1000 (e1000)** or Realtek RTL8139 |
| Disk | a small **IDE** disk (64 MB is plenty). Without it NinoOS works but forgets your files at shutdown |

### Windows

**Option A: VirtualBox (easiest)**
1. Install VirtualBox (free): <https://www.virtualbox.org/> (Windows hosts)
2. Unzip the NinoOS download into a folder you will keep.
3. Double-click **`create-vm.bat`**. It creates the VM and starts NinoOS.
4. Next time, just start the machine "NinoOS" from the VirtualBox window.

**Option B: QEMU**
1. Install QEMU: <https://www.qemu.org/download/#windows>
2. Open a terminal in the folder with `ninoos.iso` and run (once, to create the disk):
   `"C:\Program Files\qemu\qemu-img.exe" create -f raw disk.img 64M`
3. Start NinoOS:
   ```
   "C:\Program Files\qemu\qemu-system-i386.exe" -cdrom ninoos.iso -m 256M -cpu max ^
     -drive file=disk.img,format=raw,if=ide,index=0 ^
     -device e1000,netdev=n0 -netdev user,id=n0
   ```

### macOS

**Intel Mac: VirtualBox**
1. Install VirtualBox (macOS / Intel hosts): <https://www.virtualbox.org/>
2. Open Terminal in the NinoOS folder and run: `./create-vm.sh`
   (if macOS blocks the script: `chmod +x create-vm.sh` first)

**Apple Silicon Mac (M1, M2, M3, M4): UTM or QEMU**
VirtualBox cannot run PC (x86) systems on Apple Silicon, so use emulation. It works but is slower.
- **UTM** (free, <https://mac.getutm.app/>): *Create a new VM* > *Emulate* > *Other* > pick
  `i386 (Standard PC)`. Memory 256 MB. Remove the extra drives, add the ISO as *CD/DVD*, add a
  64 MB *IDE* drive. Network: *Intel e1000* or *rtl8139*.
- **QEMU** from Homebrew: `brew install qemu`, then in the NinoOS folder:
  ```
  qemu-img create -f raw disk.img 64M
  qemu-system-i386 -cdrom ninoos.iso -m 256M -cpu max \
    -drive file=disk.img,format=raw,if=ide,index=0 \
    -device e1000,netdev=n0 -netdev user,id=n0
  ```

### Linux

**Option A: VirtualBox**
1. Install VirtualBox from your package manager or <https://www.virtualbox.org/>
   (Debian/Ubuntu: `sudo apt install virtualbox`, Fedora: use the VirtualBox website, Arch: `sudo pacman -S virtualbox virtualbox-host-modules-arch`).
2. In the NinoOS folder run: `./create-vm.sh`

**Option B: QEMU**
- Debian/Ubuntu: `sudo apt install qemu-system-x86`
- Fedora: `sudo dnf install qemu-system-x86`
- Arch: `sudo pacman -S qemu-full`

Then, in the NinoOS folder:
```
qemu-img create -f raw disk.img 64M
qemu-system-i386 -cdrom ninoos.iso -m 256M -cpu max \
  -drive file=disk.img,format=raw,if=ide,index=0 \
  -device e1000,netdev=n0 -netdev user,id=n0 -display gtk,grab-on-hover=on
```

### Other virtual machine programs (VMware, Parallels, Hyper-V)
Not tested. Use the table above: BIOS firmware, 256 MB, PS/2 mouse, e1000 network, IDE disk.
If one of them does not work, use VirtualBox or QEMU.

### A real computer
Not supported. NinoOS only knows the network cards of virtual machines, and the ISO is not meant
to be written to a USB stick.

### Mouse, keyboard and shutting down
- **Click inside the window** to give NinoOS your mouse.
  Get it back with the **right Ctrl** key (VirtualBox) or **Ctrl+Alt+G** (QEMU).
- Switch off with the **Off** button (bottom right) or type `shutdown`. Files are saved to the
  virtual disk automatically a few seconds after you change them.

### If something goes wrong
- **Blue screen "NinoOS has crashed":** take a photo or screenshot and tell the developer what you
  were doing. Press **R** to restart.
- **Red screen about memory or graphics:** it tells you which setting to change.
- **No mouse movement:** click inside the window; check that the pointing device is *PS/2 Mouse*.
- **No internet:** open the Terminal and type `net`, then tell the developer what it says.
- **Anything else:** type `bugreport` in the Terminal and send the text.

---

## 2. Using NinoOS

### The desktop
- **Icons** on the right: **double-click** to open. Terminal, Capybara (files), Draw, Fastfetch
  (system info), Browser, Notepad.
- **Taskbar** at the bottom: one button per app (click to show/hide a window), the clock, and
  **Off** to shut down.
- **Windows:** drag the title bar to move; the red X closes. Mouse wheel scrolls.

### Apps
| App | What it does |
|---|---|
| **Terminal** | Type commands (list below). |
| **Capybara** | File manager. Click = select, double-click = open a folder or view a file. Buttons: Up, +Dir, +File, Ren (rename: type the new name, Enter), Del, Edit (open in Notepad). |
| **Draw** | Paint program: 10 colors, brush size 1/3/6 (S:), Era = eraser, Clr = clear, Save/Load = `/home/nino/drawing.npt`. |
| **Notepad** | Text editor: arrow keys, Home/End, PgUp/PgDn, Delete, mouse click. **Ctrl+S** saves (also the Save button). |
| **Browser** | Web pages over http and https. Type a URL and press Enter. Click links. Click a form field to type, **Enter** sends the form, click a `[ button ]` to press it. **Back**, **CSS:on/off**, **Go**. Cookies are kept. Links to files (zip, pdf ...) are saved to `/downloads`. |
| **Fastfetch** | The NinoOS logo and facts about the system. |

**Browser limits:** no JavaScript, no images, no video. Simple sites work well (Wikipedia,
Hacker News, DuckDuckGo Lite, text.npr.org). Big modern sites will look broken. Passwords are only
sent over https. Use throw-away accounts: NinoOS is a hobby system, not a safe place for real passwords.

### Files
The files live in a small in-memory file system. With a disk attached they are saved to it, so they
survive a shutdown. Folders: `/home/nino` (your folder), `/docs`, `/downloads`.

### Terminal commands

Paths work like Linux: `/docs/ideas.txt`, `..` (one folder up), `/` (root).

**Files and folders**

| Command | What it does |
|---|---|
| `pwd` | Show the current folder |
| `ls [path]` | List a folder |
| `cd <path>` | Go to a folder (`cd ..` goes up, `cd /` goes to the root) |
| `cat <file>` | Show a text file |
| `mkdir <name>` | Make a folder |
| `touch <name>` | Make an empty file |
| `write <file> <text>` | Save one line of text to a file (creates it) |
| `rm <path>` | Delete a file or an empty folder |
| `mv <path> <newname>` | Rename (in the same folder) |
| `cp <file> <newname>` | Copy a file into the current folder |
| `edit [file]` (or `notepad`) | Open Notepad (creates the file if it does not exist) |
| `disk` | Show whether a disk is attached |
| `sync` | Write everything to the disk right now |

**Apps**

| Command | What it does |
|---|---|
| `capybara` | Open the file manager |
| `draw` | Open Draw |
| `browser [url]` | Open the browser (optionally on a page) |
| `fastfetch` (or `neofetch`) | Open the system info window |

**Network**

| Command | What it does |
|---|---|
| `net` | Network status: card, IP address, HTTPS and random-number status, counters |
| `ping <host>` | Send 4 pings (the internet may not answer in a VM; the gateway does) |
| `nslookup <host>` | Find the IP address of a name |
| `wget <url> [name]` | Download a file to `/downloads` (up to 6 MB) |
| `cookies` | List stored cookies (`cookies clear` deletes them all) |
| `pci` | List the hardware on the PCI bus |

**System**

| Command | What it does |
|---|---|
| `help` | List the commands |
| `clear` | Clear the terminal |
| `ver` | Show the version |
| `echo <text>` | Print text |
| `time` | Show the clock |
| `uptime` | How long NinoOS has been running |
| `mem` | RAM and memory use |
| `whoami` | Show the user name |
| `keymap [us\|de\|dvorak]` | Show or change the keyboard layout |
| `bugreport` | System information for a bug report (also saved as `/home/nino/bugreport.txt`) |
| `factoryreset yes` | Delete **all** files and cookies and start fresh |
| `reboot` | Restart |
| `shutdown` (or `poweroff`) | Switch off |

---

## 3. Building NinoOS (developer)

### What you need
- **Arch:** `sudo pacman -S --needed base-devel nasm grub xorriso mtools qemu-full git`
- **Debian/Ubuntu:** `sudo apt install build-essential gcc-multilib nasm grub-pc-bin xorriso mtools qemu-system-x86 git`
- **Fedora:** `sudo dnf install gcc make nasm grub2-tools-extra xorriso mtools qemu-system-x86 git glibc-devel.i686`

### HTTPS (once)
HTTPS needs the BearSSL library and a list of trusted certificate authorities. Follow
**`README-HTTPS.txt`** (`git clone` BearSSL, `make` it, run `brssl ta` to create `trust_anchors.h`).
Without them everything builds, but https pages show an explanation. Check with `make https-status`.

### Make commands
| Command | What it does |
|---|---|
| `make` | Build the kernel (`ninoos.elf`) |
| `make iso` | Build **`ninoos.iso`**, the bootable NinoOS image (this is what you share) |
| `make run` | Build and start NinoOS in QEMU (creates `disk.img`, 64 MB, kept between runs) |
| `make run NIC=e1000` | Same with the Intel network card (what VirtualBox uses) |
| `make dist` | Build the ISO and pack it with the VM scripts and this README into `NinoOS-0.7.zip` for friends |
| `make https-status` | Show whether HTTPS is switched on |
| `make check` | Check the Multiboot header |
| `make clean` | Delete build files (do this after changing `trust_anchors.h` or build flags) |
| `make cleandisk` | Delete `disk.img` (all saved files!) |

To enlarge an old disk without losing files: `truncate -s 64M disk.img`.

### Sharing
- Just the system: send **`ninoos.iso`**.
- With scripts and instructions for friends: send the zip made by **`make dist`**.

### More files
`README-HTTPS.txt` (HTTPS setup), `README-BROWSER.txt` (browser details), `THIRD-PARTY.txt`
(licenses of BearSSL and GRUB).
