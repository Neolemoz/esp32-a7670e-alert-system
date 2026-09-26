// Alert system: opto input 1-8 -> Telegram + SMS (alert only) -> call + play MP3 (DFPlayer)
// A7670E on UART2 (GPIO16/17), DFPlayer Mini on UART1 (GPIO32/33), BUSY on GPIO34

#include "secrets.h"   // TG_TOKEN, TG_CHAT_ID (ไฟล์นี้ห้ามแชร์)

// ---------------- Modem ----------------
#define MODEM_RX      16
#define MODEM_TX      17
#define MODEM_PWRKEY  4
#define MODEM_BAUD    115200
#define APN           "www.dtac.co.th"
// PHONE_TO (เบอร์ปลายทางของ SMS และการโทร) อยู่ใน secrets.h

// ---------------- DFPlayer Mini ----------------
#define MP3_RX        32   // ESP32 RX1 <- DFPlayer TX
#define MP3_TX        33   // ESP32 TX1 -> DFPlayer RX (ผ่านตัวต้านทาน 1k)
#define MP3_BUSY      34   // DFPlayer BUSY -> GPIO34 (LOW = กำลังเล่น)
#define MP3_VOLUME    25   // 0-30

// ---------------- Opto inputs (input 1..8) ----------------
#define NUM_INPUTS 8
const uint8_t inputPins[NUM_INPUTS] = {27, 26, 25, 23, 22, 21, 19, 18};
#define INPUT_ACTIVE_LEVEL LOW    // opto ดึงขาลง GND เมื่อมีไฟเข้า (ใช้ INPUT_PULLUP ภายใน)
#define DEBOUNCE_MS        50
#define COOLDOWN_MS        120000UL   // ห้ามซ้ำ input เดิมภายใน 2 นาที

// ---------------- Call ----------------
#define CALL_RING_TIMEOUT_MS 40000UL  // ไม่มีคนรับภายใน 40 วิ วางสาย
#define CALL_TALK_MS         30000UL  // เพดานสูงสุดหลังรับสาย (ปกติวางเมื่อเพลงจบ)

// ข้อความแจ้งเตือน (ภาษาอังกฤษ ใช้ทั้ง SMS และ Telegram) แก้ได้เอง
const char *MESSAGES[NUM_INPUTS] = {
  "ALERT case 1: input 1 triggered",
  "ALERT case 2: input 2 triggered",
  "ALERT case 3: input 3 triggered",
  "ALERT case 4: input 4 triggered",
  "ALERT case 5: input 5 triggered",
  "ALERT case 6: input 6 triggered",
  "ALERT case 7: input 7 triggered",
  "ALERT case 8: input 8 triggered",
};

HardwareSerial SerialAT(2);
HardwareSerial Mp3(1);
unsigned long lastTrigger[NUM_INPUTS] = {0};

// ======================= DFPlayer =======================
void mp3Cmd(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x01, (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0, 0, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i <= 6; i++) sum += f[i];
  sum = 0 - sum;
  f[7] = sum >> 8;
  f[8] = sum & 0xFF;
  Mp3.write(f, 10);
}
void mp3Play(int track) { mp3Cmd(0x12, track); }   // เล่นไฟล์ /MP3/000N.mp3
void mp3Stop()          { mp3Cmd(0x16, 0); }

// รีเซ็ต DFPlayer แล้วรอเฟรม "เริ่มต้นเสร็จ" (7E FF 06 3F ...) สูงสุด timeoutMs
bool mp3Init(unsigned long timeoutMs) {
  while (Mp3.available()) Mp3.read();
  mp3Cmd(0x0C, 0);
  uint8_t win[10] = {0};
  unsigned long t0 = millis();
  bool ready = false;
  while (millis() - t0 < timeoutMs && !ready) {
    while (Mp3.available()) {
      memmove(win, win + 1, 9);
      win[9] = Mp3.read();
      if (win[0] == 0x7E && win[3] == 0x3F && win[9] == 0xEF) ready = true;
    }
    delay(5);
  }
  if (ready) {
    delay(500);
    mp3Cmd(0x06, MP3_VOLUME);
    delay(500);
    while (Mp3.available()) Mp3.read();
  }
  return ready;
}

// ======================= Modem helpers =======================
String readUntil(const char *a, const char *b, unsigned long timeoutMs) {
  String resp = "";
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (SerialAT.available()) resp += (char)SerialAT.read();
    if (a && resp.indexOf(a) >= 0) break;
    if (b && resp.indexOf(b) >= 0) break;
  }
  return resp;
}

String sendCmd(const String &cmd, unsigned long timeoutMs = 2000, bool show = true) {
  while (SerialAT.available()) SerialAT.read();
  SerialAT.print(cmd + "\r\n");
  String r = readUntil("\r\nOK\r\n", "ERROR", timeoutMs);
  r.trim();
  if (show) {
    Serial.println(">> " + cmd);
    Serial.println(r.length() ? r : "(no response)");
  }
  return r;
}

bool moduleAlive() {
  for (int i = 0; i < 5; i++) {
    if (sendCmd("AT", 1000, false).indexOf("OK") >= 0) return true;
  }
  return false;
}

bool waitRegistered(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    String r = sendCmd("AT+CEREG?", 1000, false);
    int c = r.indexOf("+CEREG: ");
    if (c >= 0) {
      int comma = r.indexOf(',', c);
      int stat = r.substring(comma + 1).toInt();
      if (stat == 1 || stat == 5) return true;
    }
    delay(1000);
  }
  return false;
}

// ======================= Telegram =======================
String urlEncode(const String &s) {
  String out = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

bool sendTelegram(const String &text) {
  Serial.println("--- Telegram: " + text);
  sendCmd("AT+CGDCONT=1,\"IP\",\"" APN "\"", 2000, false);
  sendCmd("AT+CGATT=1", 10000, false);
  sendCmd("AT+CSOCKSETPN=1", 2000, false);
  sendCmd("AT+NETOPEN", 15000, false);
  sendCmd("AT+HTTPTERM", 1000, false);
  if (sendCmd("AT+HTTPINIT", 2000, false).indexOf("OK") < 0) { Serial.println("!! HTTPINIT failed"); return false; }
  String url = "https://api.telegram.org/bot" TG_TOKEN "/sendMessage?chat_id=" TG_CHAT_ID "&text=" + urlEncode(text);
  sendCmd("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000, false);
  while (SerialAT.available()) SerialAT.read();
  SerialAT.print("AT+HTTPACTION=0\r\n");
  String r = readUntil("+HTTPACTION:", "ERROR", 30000);
  r += readUntil("\r\n", nullptr, 1500);
  sendCmd("AT+HTTPTERM", 1000, false);
  bool ok = r.indexOf("+HTTPACTION: 0,200") >= 0;
  Serial.println(ok ? "Telegram: SENT" : "Telegram: FAILED");
  return ok;
}

// ======================= SMS =======================
bool sendSMS(const String &text) {
  Serial.println("--- SMS to " PHONE_TO ": " + text);
  sendCmd("AT+CMGF=1", 2000, false);
  sendCmd("AT+CSCS=\"GSM\"", 2000, false);
  while (SerialAT.available()) SerialAT.read();
  SerialAT.print("AT+CMGS=\"" PHONE_TO "\"\r\n");
  if (readUntil(">", "ERROR", 5000).indexOf(">") < 0) { Serial.println("SMS: FAILED (no prompt)"); return false; }
  SerialAT.print(text);
  SerialAT.write(0x1A);
  String r = readUntil("+CMGS:", "ERROR", 60000);
  readUntil("OK", "ERROR", 3000);
  bool ok = r.indexOf("+CMGS:") >= 0;
  Serial.println(ok ? "SMS: SENT" : "SMS: FAILED");
  return ok;
}

// ======================= Call + music =======================
// สั่งเล่นแล้วเช็คว่า BUSY ลง LOW จริง ถ้าไม่ ลองซ้ำ (ครั้งสุดท้ายรีเซ็ต DFPlayer ก่อน)
bool playVerified(int track) {
  for (int attempt = 1; attempt <= 3; attempt++) {
    while (Mp3.available()) Mp3.read();
    mp3Play(track);
    unsigned long t0 = millis();
    while (millis() - t0 < 1500) {
      if (digitalRead(MP3_BUSY) == LOW) {
        Serial.printf("Music started (attempt %d)\n", attempt);
        return true;
      }
      delay(10);
    }
    Serial.printf("BUSY still HIGH after play (attempt %d), DFPlayer replies:", attempt);
    while (Mp3.available()) Serial.printf(" %02X", Mp3.read());
    Serial.println();
    if (attempt == 2) {
      Serial.println("re-init DFPlayer before last try");
      Serial.println(mp3Init(8000) ? "  DFPlayer ready" : "  DFPlayer did not report ready");
    }
  }
  return false;
}

// วางสายให้แน่นอน: ใช้ AT+CHUP (ATH ไม่ตัดสาย VoLTE) แล้วเช็คด้วย AT+CLCC ว่าไม่มีสายค้าง
void hangUp() {
  for (int attempt = 0; attempt < 4; attempt++) {
    while (SerialAT.available()) SerialAT.read();
    sendCmd(attempt % 2 == 0 ? "AT+CHUP" : "ATH", 3000, false);
    delay(700);
    String r = sendCmd("AT+CLCC", 2000, false);
    if (r.indexOf("+CLCC:") < 0) {
      Serial.println("Hang up: call cleared");
      return;
    }
    Serial.println("Hang up: call still active, retry...");
  }
  Serial.println("!! Hang up failed after retries");
}

// โทรออก พอมีคนรับสาย (VOICE CALL: BEGIN) ค่อยเริ่มเล่นเพลง แล้ววางเมื่อเพลงจบ (หรือครบ CALL_TALK_MS)
void callAndPlay(int track) {
  Serial.printf("--- Calling " PHONE_TO " (music %04d)\n", track);
  sendCmd("AT+CLIP=1", 2000, false);
  while (SerialAT.available()) SerialAT.read();
  SerialAT.print("ATD" PHONE_TO ";\r\n");

  unsigned long t0 = millis(), activeAt = 0;
  bool active = false, musicStarted = false;
  String buf = "";
  while (true) {
    while (SerialAT.available()) {
      buf += (char)SerialAT.read();
      if (buf.length() > 400) buf.remove(0, 200);
    }
    if (!active && buf.indexOf("VOICE CALL: BEGIN") >= 0) {
      active = true;
      activeAt = millis();
      Serial.println("Call answered -> play music");
      if (playVerified(track)) musicStarted = true;
      else Serial.println("!! music did not start");
    }
    if (buf.indexOf("NO CARRIER") >= 0 || buf.indexOf("BUSY") >= 0 || buf.indexOf("NO ANSWER") >= 0 ||
        buf.indexOf("VOICE CALL: END") >= 0 || buf.indexOf("ERROR") >= 0) {
      Serial.println("Call ended by network/remote");
      break;
    }
    if (active) {
      bool playing = (digitalRead(MP3_BUSY) == LOW);
      if (playing) musicStarted = true;
      if (musicStarted && !playing && millis() - activeAt > 1000) {
        Serial.println("Music finished, hanging up");
        delay(1000);   // เว้นหางเสียงสั้นๆ ก่อนวางสาย
        break;
      }
      if (!musicStarted && millis() - activeAt > 8000) {
        Serial.println("No music, hanging up");
        break;
      }
      if (millis() - activeAt > CALL_TALK_MS) { Serial.println("Talk time over, hanging up"); break; }
    }
    if (!active && millis() - t0 > CALL_RING_TIMEOUT_MS) { Serial.println("No answer, hanging up"); break; }
    delay(20);
  }
  mp3Stop();
  hangUp();
}

// ======================= Trigger =======================
void handleTrigger(int idx) {
  Serial.printf("\n##### INPUT %d TRIGGERED #####\n", idx + 1);
  if (!moduleAlive()) { Serial.println("!! modem not responding, abort"); return; }
  String msg = MESSAGES[idx];
  sendTelegram(msg);       // 1) Telegram
  sendSMS(msg);            // 2) SMS
  callAndPlay(idx + 1);    // 3) โทร + เพลง 000N.mp3
  Serial.println("##### DONE #####\n");
}

bool inputActive(int idx) {
  if (digitalRead(inputPins[idx]) != INPUT_ACTIVE_LEVEL) return false;
  delay(DEBOUNCE_MS);
  return digitalRead(inputPins[idx]) == INPUT_ACTIVE_LEVEL;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n== Alert system ==");

  for (int i = 0; i < NUM_INPUTS; i++) pinMode(inputPins[i], INPUT_PULLUP);
  pinMode(MODEM_PWRKEY, INPUT);   // ไม่กด PWR-K (โมดูลเปิดอยู่แล้ว)
  pinMode(MP3_BUSY, INPUT);       // GPIO34 input-only ไม่มี pull-up ภายใน (DFPlayer ขับเอง)

  Mp3.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  delay(500);
  Serial.println(mp3Init(12000) ? "DFPlayer: ready" : "DFPlayer: NO ready frame (จะลอง re-init ตอนเล่น)");

  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  Serial.println(moduleAlive() ? "Modem: OK" : "Modem: NOT RESPONDING");
  sendCmd("ATE0", 1000, false);
  Serial.println(waitRegistered(60000) ? "Network: REGISTERED" : "Network: NOT registered yet");
  Serial.println("Ready. T1..T8 = simulate input, M = music-only test.");
}

void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if ((s.length() == 2) && (s[0] == 'T' || s[0] == 't') && s[1] >= '1' && s[1] <= '8') {
      handleTrigger(s[1] - '1');
    } else if (s.equalsIgnoreCase("M")) {
      Serial.println("--- music-only test (no call)");
      Serial.println(playVerified(1) ? "music OK" : "music FAILED");
      delay(3000);
      mp3Stop();
    } else if (s.length()) {
      SerialAT.print(s + "\r\n");   // ส่งคำสั่ง AT ตรงๆ
    }
  }
  while (SerialAT.available()) Serial.write(SerialAT.read());

  // มอนิเตอร์: พิมพ์เมื่อระดับขา input เปลี่ยน (ไว้เช็คสาย opto)
  static int lastLevel[NUM_INPUTS] = {-1, -1, -1, -1, -1, -1, -1, -1};
  for (int i = 0; i < NUM_INPUTS; i++) {
    int l = digitalRead(inputPins[i]);
    if (l != lastLevel[i]) {
      if (lastLevel[i] != -1) Serial.printf("[IN%d] GPIO%d -> %s\n", i + 1, inputPins[i], l ? "HIGH" : "LOW");
      lastLevel[i] = l;
    }
  }

  for (int i = 0; i < NUM_INPUTS; i++) {
    if (inputActive(i) && (lastTrigger[i] == 0 || millis() - lastTrigger[i] > COOLDOWN_MS)) {
      lastTrigger[i] = millis();
      handleTrigger(i);
      lastTrigger[i] = millis();   // เริ่มนับ cooldown หลังทำงานเสร็จ
    }
  }
}
