NinoOS - HTTPS aanzetten (BearSSL)
==================================

Wat dit doet: NinoOS kan dan https:// sites openen (browser en wget).
Alles hieronder doe je op je Arch-computer, in de map van NinoOS (waar de Makefile staat).
Het kost niets: BearSSL is gratis open source (MIT-licentie).

Eenmalig: programma's installeren
---------------------------------
    sudo pacman -S --needed git base-devel nasm grub xorriso mtools qemu-full

(Als het linken later klaagt over "__udivdi3" of "libgcc": installeer ook  sudo pacman -S lib32-gcc-libs)

Stap 1. BearSSL downloaden
--------------------------
    git clone https://www.bearssl.org/git/BearSSL bearssl

Daarna bestaat de map  bearssl/  naast je Makefile (met daarin src/ en inc/).

Stap 2. BearSSL's hulpprogramma bouwen (alleen voor stap 3)
-----------------------------------------------------------
    cd bearssl
    make
    cd ..

Dit maakt het bestand  bearssl/build/brssl .

Stap 3. De lijst met vertrouwde websites (CA-certificaten) maken
----------------------------------------------------------------
    ./bearssl/build/brssl ta /etc/ca-certificates/extracted/tls-ca-bundle.pem > trust_anchors.h

Als dat bestand niet bestaat:  sudo pacman -S ca-certificates  en probeer opnieuw.
Het bestand trust_anchors.h moet naast de Makefile staan.

Stap 4. Opnieuw bouwen en starten
---------------------------------
    make clean
    make https-status      (moet tonen: HTTPS: ON)
    make run

Het eerste bouwen duurt nu langer (BearSSL heeft ongeveer 200 bestanden).
Heb je (niet-gewijzigde) bestanden van BearSSL erin gezet en geeft make een foutmelding?
Stuur me de EERSTE foutmelding (de bovenste), dan los ik die op.

Testen in NinoOS
----------------
In de terminal:   net
                  nslookup example.com
                  wget https://example.com
In de browser:    typ  https://en.wikipedia.org/wiki/Capybara  en druk op Enter.

Veelvoorkomende meldingen
-------------------------
- "HTTPS is not built in"                -> stap 1-4 nog niet gedaan (make https-status geeft OFF)
- "certificate expired or not yet valid" -> de klok klopt niet. Controleer de datum van je computer;
                                            NinoOS leest de datum van QEMU (die volgt je computer).
- "certificate not trusted"              -> trust_anchors.h mist de CA van die site, of is leeg
- "no random numbers"                    -> melden aan mij

Eerlijk over de beveiliging
---------------------------
De controle van certificaten werkt, maar de willekeurige getallen van NinoOS komen uit timing van de CPU
en klok. Dat is genoeg voor een hobby-OS maar niet sterk genoeg voor echte geheimen.
Log dus nergens in met echte wachtwoorden.
