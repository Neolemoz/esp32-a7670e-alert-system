# ESP32 + A7670E alert system

Opto input (8 channels) -> **Telegram + SMS** (alert text) -> **phone call** that plays an MP3 (DFPlayer Mini) when answered.
Input N sends message N and plays `/MP3/000N.mp3`.

ระบบแจ้งเตือน: เมื่อ opto input ช่อง N ทำงาน จะส่ง Telegram + SMS แล้วโทรออก และเล่นเพลง `000N.mp3` ตอนมีคนรับสาย

## Hardware

- ESP32 Dev Module
- SIMCom **A7670E** LTE Cat-1 board (5-12 V input, pins: SLEEP, GND, 5-12V, PWR-K, UTX, URX, GND)
- **DFPlayer Mini** + microSD (FAT32) with a folder `MP3` containing `0001.mp3` ... `0008.mp3`
- **DST-1R8P-N** 8-channel optocoupler module (NPN output)
- SIM card with data + SMS + voice (VoLTE); tested with dtac

## Pin map

### A7670E modem (UART2)

| Modem pin | ESP32 |
|---|---|
| UTX | GPIO17 |
| URX | GPIO16 |
| PWR-K | GPIO4 (left as input, module is already on) |
| SLEEP | GND |
| GND | GND (common with everything) |
| 5-12V | external supply, >= 2 A |

> The UART wires are crossed relative to the silkscreen labels on the board used here:
> GPIO16 <-> URX, GPIO17 <-> UTX. That is what worked in testing.

### DFPlayer Mini (UART1)

| DFPlayer pin | ESP32 |
|---|---|
| TX | GPIO32 |
| RX | GPIO33 (through a 1 kOhm resistor) |
| BUSY | GPIO34 (LOW = playing) |
| VCC | 5 V |
| GND | GND |
| SPK_1 / SPK_2 | speaker (optional) |

### DST-1R8P-N optocoupler (outputs to ESP32)

| Input channel | ESP32 |
|---|---|
| 1 | GPIO27 |
| 2 | GPIO26 |
| 3 | GPIO25 |
| 4 | GPIO23 |
| 5 | GPIO22 |
| 6 | GPIO21 |
| 7 | GPIO19 |
| 8 | GPIO18 |
| output-side VCC | **3.3 V** (never 5 V, ESP32 GPIO is not 5 V tolerant) |
| GND | GND |

NPN outputs pull the pin to GND when the channel is active, so the sketch uses `INPUT_PULLUP` and treats **LOW as triggered** (`INPUT_ACTIVE_LEVEL`).

All grounds must be common (ESP32, modem, DFPlayer, opto output side).

GPIOs used: `4, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34`

## Setup

1. **You must create your own `alert_system/secrets.h` and put in your own Telegram bot token.** It is not in this repo (git-ignored) and the sketch will not compile without it.
   Copy `alert_system/secrets.h.example` to `alert_system/secrets.h` and fill in:
   - `TG_TOKEN`: create a bot with @BotFather (`/newbot`) and copy the token it gives you.
   - `TG_CHAT_ID`: send any message to your bot, then open `https://api.telegram.org/bot<TOKEN>/getUpdates` in a browser and read `"chat":{"id":...}`.
   - `PHONE_TO`: the number to SMS and call, in international format (e.g. `+66XXXXXXXXX`).

   ต้องสร้างไฟล์ `secrets.h` เอง แล้วใส่ **token ของบอท Telegram ของตัวเอง**, chat id และเบอร์โทร (ห้ามนำ token ไปใส่ในไฟล์ที่ commit ขึ้น GitHub)
   Never commit `secrets.h`. If a token ever leaks, send `/revoke` to @BotFather.
2. Check `APN` in `alert_system/alert_system.ino` for your carrier.
3. Edit the `MESSAGES[]` table for your own alert texts.
4. Board: *ESP32 Dev Module*. Open the **folder** `alert_system` in the Arduino IDE (so `secrets.h` is found), then upload.
5. Serial Monitor: 115200 baud, line ending *Both NL & CR*.

## Serial test commands

| Command | Effect |
|---|---|
| `T1` ... `T8` | simulate input N (sends real Telegram + SMS + call) |
| `M` | music-only test (no call) |
| any other text | sent to the modem as an AT command |

While running, the sketch prints `[INn] GPIOx -> HIGH/LOW` whenever an input changes, which helps when wiring the optocoupler.

## Behaviour and settings (top of `alert_system.ino`)

- Order per trigger: Telegram -> SMS -> call. Music starts when the call is answered (`VOICE CALL: BEGIN`); the call ends when the music finishes (BUSY goes HIGH) using `AT+CHUP`.
- `COOLDOWN_MS` (2 min): a given input will not re-trigger sooner. Inputs are handled one at a time, 1 to 8.
- `CALL_RING_TIMEOUT_MS` (40 s) and `CALL_TALK_MS` (30 s max after answer).
- DFPlayer init waits for the "initialised" frame (`7E FF 06 3F`) and playback is verified through BUSY, with retries.

## Known limitations

- Audio from the DFPlayer is **not yet wired into the modem's MIC**, so the person answering does not hear the MP3 (it plays on the DFPlayer speaker only).
- SMS is sent in GSM 7-bit text mode (English only).
- Not tested with the real optocoupler module on all 8 channels yet.

## Other folders

- `mp3_test/` - stand-alone DFPlayer diagnostic sketch (pin map above).
