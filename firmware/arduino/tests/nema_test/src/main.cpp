/*
 * AutoRack — Firmware Rack (Arduino Uno) — ТЕСТ NEMA 17 / DRV8825
 * ==================================================================
 * ИСПРАВЛЕНИЕ "работает только первая команда":
 *   - увеличен RX-буфер Serial (256 байт вместо 64);
 *   - приём и печать РАЗНЕСЕНЫ: сначала вычитываем все байты,
 *     только потом разбираем строки и отвечаем;
 *   - handleCommand() НЕ печатает внутри цикла приёма.
 *
 * ДРАЙВЕР: DRV8825.
 *   - RST и SLP соединить вместе (или оба на 5В), иначе "спит".
 *   - Ток: I = Vref * 2 (Rsense=0.1). Для теста Vref 0.3..0.4 В.
 *   - EN = LOW (вкл). STEP ширина >= 1.9 мкс (здесь 3 мкс).
 *
 * РАСПИНОВКА (Uno):
 *   STEP -> A1 , DIR -> A2 , EN -> A3 (LOW = вкл)
 *   VDD  -> 5V , GND -> GND Uno + GND БП
 *   VMOT -> +12В + конденсатор 100..220 мкФ
 *   Endstop NO -> A4 , COM -> GND
 *
 * КОМАНДЫ (9600, LF):
 *   <число> | delay=<мкс> | test | pin | home | stop | hb=1
 * Монитор: pio device monitor -b 9600 --eol LF
 */

#include <Arduino.h>

// ВНИМАНИЕ: RX-буфер 256 байт задаётся в platformio.ini:
//   build_flags = -DSERIAL_RX_BUFFER_SIZE=256 -DSERIAL_TX_BUFFER_SIZE=256
// (в коде переопределять бесполезно — макрос уже зафиксирован в ядре).

// ================== ПИНЫ ==================
#define homeSwitch  A4
#define ena         A3      // LOW = вкл
#define stepPin     A1
#define dirPin      A2

// ================== СОСТОЯНИЕ ==================
long  steps       = 0;
long  steps2go    = 0;
boolean moving    = false;
boolean homing    = false;
boolean moveCw    = true;

unsigned int  currentDelay = 8000;   // мкс
unsigned long lastStepUs   = 0;
unsigned long lastHbMs     = 0;
boolean       hbOn         = false;

// "pin" выполняется в loop() (медленные шаги вне приёма Serial)
boolean       pinRunning   = false;
long          pinLeft      = 0;
unsigned long pinLastUs    = 0;

// ================== ШАГ ==================
void stepOnce() {
  digitalWrite(dirPin, moveCw ? HIGH : LOW);
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(3);        // DRV8825: >= 1.9 мкс
  digitalWrite(stepPin, LOW);
}

// ================== РАЗБОР КОМАНДЫ (БЕЗ печати!) ==================
// Возвращает true, если нужно напечатать ответ — но печать делает loop().
enum CmdType { CMD_NONE, CMD_TARGET, CMD_DELAY, CMD_TEST, CMD_PIN,
               CMD_HOME, CMD_STOP, CMD_HB };
CmdType    lastCmdType = CMD_NONE;
long       lastCmdArg  = 0;

void parseCommand(char *cmd) {
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  uint8_t n = strlen(cmd);
  while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t')) cmd[--n] = '\0';
  if (n == 0) return;

  if (strncmp(cmd, "delay=", 6) == 0) {
    long v = atol(cmd + 6);
    if (v < 20)    v = 20;
    if (v > 16000) v = 16000;
    currentDelay = (unsigned int)v;
    lastCmdType = CMD_DELAY;
    lastCmdArg  = currentDelay;
  } else if (strcmp(cmd, "home") == 0) {
    homing = true; moving = false;
    lastCmdType = CMD_HOME;
  } else if (strcmp(cmd, "stop") == 0) {
    homing = false; moving = false; steps2go = steps;
    lastCmdType = CMD_STOP;
  } else if (strcmp(cmd, "test") == 0) {
    homing = false; moving = true; steps2go = steps + 200;
    lastCmdType = CMD_TEST;
    lastCmdArg  = steps2go;
  } else if (strcmp(cmd, "pin") == 0) {
    homing = false; moving = false;
    pinRunning = true;          // запускаем медленные шаги в loop()
    pinLeft    = 50;
    moveCw     = true;
    pinLastUs  = micros();
    lastCmdType = CMD_PIN;
    lastCmdArg  = 50;
  } else if (strncmp(cmd, "hb=", 3) == 0) {
    hbOn = atoi(cmd + 3) != 0;
    lastCmdType = CMD_HB;
    lastCmdArg  = hbOn ? 1 : 0;
  } else {
    long target = atol(cmd);
    homing = false; moving = true; steps2go = target;
    lastCmdType = CMD_TARGET;
    lastCmdArg  = target;
  }
}

// ================== ПРИЁМ (только сборка строк) ==================
char    line[24];
uint8_t lineLen = 0;

// Читаем ВСЁ из RX, собираем завершённые строки в очередь и
// разбираем их. Печати здесь НЕТ -> символы не теряются.
void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;                 // конец строки — по '\n'
    if (c == '\n') {
      line[lineLen] = '\0';
      if (lineLen > 0) parseCommand(line);   // без печати
      lineLen = 0;
      continue;
    }
    if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
    else lineLen = 0;                        // переполнение -> сброс
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(9600);

  pinMode(ena, OUTPUT);
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(homeSwitch, INPUT_PULLUP);

  digitalWrite(ena, LOW);         // DRV8825: LOW = вкл
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, HIGH);

  delay(200);                     // дать Serial стабилизироваться

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — тест NEMA 17 / DRV8825 (Uno)"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" STEP=A1  DIR=A2  EN=A3  Endstop=A4"));
  Serial.println(F(" Команды: <число>|delay=|test|pin|home|stop|hb=1"));
  Serial.println(F(" !! DRV8825: соедини RST и SLP!"));
  Serial.println(F("========================================="));
  Serial.print(F("delay="));
  Serial.print(currentDelay);
  Serial.print(F("  EN="));
  Serial.print(digitalRead(ena));
  Serial.print(F("  home="));
  Serial.println(digitalRead(homeSwitch));
  Serial.println(F("Готов. Введи: test   или   pin"));
}

// ================== LOOP ==================
void loop() {
  unsigned long now = micros();

  // 1) СНАЧАЛА приём (без печати).
  pollSerial();

  // 2) Печать ответа на только что разобранную команду — ВНЕ приёма.
  if (lastCmdType != CMD_NONE) {
    switch (lastCmdType) {
      case CMD_TARGET:
        Serial.print(F("Target: ")); Serial.println(lastCmdArg); break;
      case CMD_DELAY:
        Serial.print(F("Delay: ")); Serial.println(lastCmdArg); break;
      case CMD_TEST:
        Serial.print(F("TEST -> target ")); Serial.println(lastCmdArg); break;
      case CMD_PIN:
        Serial.println(F("PIN test: 50 slow steps...")); break;
      case CMD_HOME:
        Serial.println(F("Homing...")); break;
      case CMD_STOP:
        Serial.println(F("Stopped.")); break;
      case CMD_HB:
        Serial.print(F("heartbeat ")); Serial.println(lastCmdArg ? F("ON") : F("OFF")); break;
      default: break;
    }
    lastCmdType = CMD_NONE;
  }

  // 3) Выполнение "pin" (медленные шаги) — здесь, вне приёма.
  if (pinRunning) {
    if (micros() - pinLastUs >= 50000UL) {   // 50 мс на шаг
      pinLastUs = micros();
      stepOnce();
      steps++;
      if (--pinLeft <= 0) {
        pinRunning = false;
        Serial.print(F("PIN done, step="));
      Serial.println(steps);
      }
    }
  }

  // 4) heartbeat
  if (hbOn && millis() - lastHbMs >= 2000) {
    lastHbMs = millis();
    Serial.print(F("alive EN="));
    Serial.print(digitalRead(ena));
    Serial.print(F(" home="));
    Serial.print(digitalRead(homeSwitch));
    Serial.print(F(" step="));
    Serial.println(steps);
  }

  // 5) homing
  if (homing) {
    if (digitalRead(homeSwitch) == HIGH) {
      if (now - lastStepUs >= (unsigned long)currentDelay) {
        lastStepUs = now;
        moveCw = false;
        stepOnce();
        steps--;
      }
    } else {
      homing = false; steps = 0; steps2go = 0;
      Serial.println(F("system is in home"));
    }
    return;
  }

  // 6) обычное движение
  if (moving) {
    if (steps != steps2go) {
      if (now - lastStepUs >= (unsigned long)currentDelay) {
        lastStepUs = now;
        moveCw = (steps2go > steps);
        stepOnce();
        steps += moveCw ? 1 : -1;
      }
    } else {
      moving = false;
      Serial.print(F("Reached step "));
      Serial.println(steps);
    }
  }
}

