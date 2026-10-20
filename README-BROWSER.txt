Nino Browser 0.6 - what it can do, and what you must do once
=============================================================

UPDATE STEPS (once)
-------------------
1. Unzip over your old folder. Keep: bearssl/ and trust_anchors.h (they are NOT in the zip).
2. Your disk image is 16 MB. To get 64 MB (room for downloads) WITHOUT losing your files:
       truncate -s 64M disk.img
3. make clean
   make https-status      (HTTPS: ON)
   make run

WHAT IS NEW
-----------
- Cookies:       kept per site, sent back automatically. Terminal: "cookies" (list), "cookies clear".
                 Cookies with an expiry date survive a reboot (stored in /home/nino/.cookies, plain text).
- Forms/login:   text fields, password fields, checkboxes, radio buttons, drop-downs (first/selected
                 option), text areas, hidden fields (CSRF tokens) and buttons. Click a field, type,
                 press Enter (or click the [ button ]). Tab jumps to the next field. GET and POST.
                 Passwords are only sent over https; over plain http NinoOS refuses.
- CSS:           colors, backgrounds, bold, underline, display:none / visibility:hidden, selectors with
                 tag, .class, #id, "a b" and "a > b". External style sheets are loaded (up to 3 per page).
                 The [CSS:on] button switches this off (faster, and plain).
                 NOT supported: layout (flexbox, grid, floats, widths), fonts, sizes, images, media queries.
- Downloads:     a link to a file (zip, pdf, image ...) or a page the server marks as attachment is saved
                 to /downloads (up to 6 MB). "wget <url>" does the same from the terminal.
- Meta refresh:  pages that redirect with <meta http-equiv="refresh"> are followed.
- Speed:         1 ms network polling (was 10 ms), larger receive buffers, TLS session reuse (the second
                 page of a site skips the slow handshake), disk saves wait until you stopped changing files.

TRY THIS
--------
- https://news.ycombinator.com/login  (a plain form; use a throw-away account)
- https://lite.duckduckgo.com/lite/   (type a search, press Enter)
- https://en.wikipedia.org/wiki/Capybara

WHAT DOES NOT WORK (and why)
----------------------------
- JavaScript. Many sites (Google, GitHub's newer pages, YouTube, X, banking sites, anything that says
  "enable JavaScript") need it. This is not a small feature: it needs a JavaScript engine (a download)
  plus a DOM and web APIs. See the chat for the plan.
- Images, video, audio, fonts, tables as real grids, pop-ups, CAPTCHAs, 2-factor pages that need scripts.
- TLS 1.3-only sites (BearSSL speaks TLS 1.2).

SECURITY - please read
----------------------
What IS safe: HTTPS certificates are checked against the Arch CA list in trust_anchors.h (a snapshot made
when you ran brssl: re-run it now and then). Host names are checked. With "-cpu max" the random numbers for
TLS come from the CPU's hardware generator (the 'net' command shows which one is used).
What is NOT safe about a hobby OS: there is no memory protection between programs, no sandbox, cookies and
sessions are stored unencrypted on disk.img, and the code has not been audited. So:
  * use throw-away accounts, never your real email/bank/Google/GitHub password
  * "cookies clear" when you are done
