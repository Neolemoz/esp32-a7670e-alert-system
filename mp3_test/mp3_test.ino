// DFPlayer test แบบเว้นจังหวะนาน (สำหรับการ์ดใหญ่/DFPlayer ตอบช้า)
// สาย: DFPlayer TX -> GPIO32, DFPlayer RX -> GPIO33 (ผ่าน 1k), DFPlayer BUSY -> GPIO34
// ไฟล์: /MP3/0001.mp3 ... /MP3/0008.mp3
// Serial Monitor: 115200, Both NL & CR. พิมพ์ 1..8 เล่น, s หยุด, +/- เสียง, t ทดสอบใหม่

#define MP3_RX   32
#define MP3_TX   33
#define BUSY_PIN 34

HardwareSerial Mp3(1);
int volume = 20;

void mp3Cmd(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x01, (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0, 0, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i <= 6; i++) sum += f[i];
  sum = 0 - sum;
  f[7] = sum >> 8;
  f[8] = sum & 0xFF;
  Mp3.write(f, 10);
}

int dumpRaw(unsigned long ms) {
  unsigned long t0 = millis();
  int n = 0;
  while (millis() - t0 < ms) {
    while (Mp3.available()) {
      if (n == 0) Serial.print("    <- ");
      Serial.printf("%02X ", Mp3.read());
      n++;
    }
  }
  if (n) Serial.println();
  return n;
}

bool waitBusyLow(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (digitalRead(BUSY_PIN) == LOW) return true;
    delay(10);
  }
  return false;
}

void runTests() {
  Serial.println("\n=== รอ DFPlayer เริ่มต้น 10 วินาที (การ์ดใหญ่อาจช้า) ===");
  Serial.printf("BUSY = %d\n", digitalRead(BUSY_PIN));
  while (Mp3.available()) Mp3.read();
  mp3Cmd(0x0C, 0);                       // reset
  Serial.printf("    reply %d bytes\n", dumpRaw(10000));

  Serial.printf("=== ตั้งเสียง %d แล้วรอ 2 วินาที ===\n", volume);
  mp3Cmd(0x06, volume);
  Serial.printf("    reply %d bytes\n", dumpRaw(2000));

  Serial.println("=== สั่งเล่น /MP3/0001.mp3 (รอ BUSY สูงสุด 6 วินาที) ===");
  mp3Cmd(0x12, 1);
  bool low = waitBusyLow(6000);
  dumpRaw(500);
  Serial.println(low ? "    => BUSY LOW : เล่นได้ ✔" : "    => BUSY ยัง HIGH : ไม่เล่น ✘");
  delay(2000);
  mp3Cmd(0x16, 0);
  dumpRaw(1000);

  if (!low) {
    Serial.println("=== ลองสั่งเล่นตามลำดับไฟล์ (index 1) ===");
    mp3Cmd(0x03, 1);
    low = waitBusyLow(6000);
    dumpRaw(500);
    Serial.println(low ? "    => BUSY LOW : เล่นได้ ✔ (ใช้ 0x03)" : "    => BUSY ยัง HIGH : ไม่เล่น ✘");
    mp3Cmd(0x16, 0);
  }
  Serial.println(low ? ">>> DFPlayer เล่นได้ <<<" : ">>> ยังไม่เล่น: ลองการ์ดเล็กลง (4-16GB FAT32) หรือเช็คไฟ 5V <<<");
  Serial.println("พิมพ์ 1..8 เล่น, s หยุด, +/- เสียง, t ทดสอบใหม่");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n== DFPlayer slow-paced test ==");
  pinMode(BUSY_PIN, INPUT);
  Mp3.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  delay(500);
  runTests();
}

bool lastBusy = true;

void loop() {
  bool busy = digitalRead(BUSY_PIN);
  if (busy != lastBusy) {
    lastBusy = busy;
    Serial.println(busy ? "[BUSY HIGH] หยุด/ว่าง" : "[BUSY LOW] กำลังเล่น");
  }
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() == 1 && s[0] >= '1' && s[0] <= '8') { Serial.println("play /MP3/000" + s + ".mp3"); mp3Cmd(0x12, s[0] - '0'); }
    else if (s == "s") { Serial.println("stop"); mp3Cmd(0x16, 0); }
    else if (s == "+") { volume = min(30, volume + 3); Serial.printf("volume %d\n", volume); mp3Cmd(0x06, volume); }
    else if (s == "-") { volume = max(0, volume - 3); Serial.printf("volume %d\n", volume); mp3Cmd(0x06, volume); }
    else if (s == "t") runTests();
    dumpRaw(1000);
  }
  dumpRaw(20);
}
