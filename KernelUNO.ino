#include <Arduino.h>
#include <string.h>
#include <avr/pgmspace.h>
#if defined(__AVR__)
#include <avr/boot.h>
#endif

// Change these to reflect your hardware configuration! If a feature was enabled here but isn't supported for the hardware configuration in use, it will be disabled at build-time.
#define NO_MEMORY_CHECK 0      // Disable checking the amount of free memory available.
#define NO_SOFT_RESET 0        // Disable software resets.
#define NO_TONE_FUNC 0         // Disable piezo support.
#define NO_EEPROM 0            // Disable all access and usage of the EEPROM storage.
#define HW_NAME "Arduino UNO"  // The name of the board running KernelUNO.
#define BAUD_RATE 115200       // On some boards, this may need to be reconfigured to prevent garbling in the Serial Monitor.
#define VERSION_NUMBER "1.6"   // Version string for this version of KernelUNO.


#if not defined(ADAFRUIT_METRO_M0_EXPRESS) && not defined(ARDUINO_SAM_DUE) && not defined(ARDUINO_GIGA) && NO_EEPROM == 0  // EEPROM is allowed and not using Adafruit Metro M0 Express board.
#include <EEPROM.h>
#elif defined(ADAFRUIT_METRO_M0_EXPRESS) || defined(ARDUINO_SAM_DUE) || defined(ARDUINO_GIGA)
#undef NO_EEPROM
#define NO_EEPROM 1  // EEPROM cannot be used on some boards.
#endif

#ifdef ARDUINO_SAM_DUE
#undef NO_TONE_FUNC
#define NO_TONE_FUNC 1  // *sigh* Arduino Due boards are the worst.
#endif

#ifdef ARDUINO_GIGA
#undef NO_MEMORY_CHECK
#define NO_MEMORY_CHECK 1
#endif

#define MAX_FILES 8
#define NAME_LEN 12
#define CONTENT_LEN 24
#define PATH_LEN 16
#define DMESG_LINES 4
#define DMESG_LEN 32
#define EEPROM_MAGIC 0xAB
#define EEPROM_ADDR 0

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else   // __ARM__
extern char* __brkval;
#endif  // __arm__

#ifdef __arm__
#define HW_ARCH "ARM"

#elif defined(__AVR__)
#define HW_ARCH "AVR"

#else
#define HW_ARCH "Unknown"

#endif

typedef struct {
  char name[NAME_LEN];
  char content[CONTENT_LEN];
  char parentDir[PATH_LEN];
  int isDirectory;
  int active;
} RAMFile;

typedef struct {
  unsigned long timestamp;
  char message[DMESG_LEN];
} DmesgEntry;

RAMFile fs[MAX_FILES];
char currentPath[PATH_LEN] = "/";
char inputBuffer[32] = "";
int inputLen = 0;
DmesgEntry dmesg[DMESG_LINES];
int dmesgIndex = 0;
int guessNumber = 0;
int guessAttempts = 0;
bool gameActive = false;

int freeMemory() {
  char top;

#if NO_MEMORY_CHECK == 1
  return -1;
#elif defined(__arm__)
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(__AVR__)
  extern int __heap_start;
  extern void *__brkval;
  int freeValue;
  if ((int)__brkval == 0)
    freeValue = ((int)&top) - ((int)&__heap_start);
  else
    freeValue = ((int)&top) - ((int)__brkval);
  return freeValue;
#else
  return -1;
#endif
}

bool is_argstr_empty(char* args) {
  return strcmp("", args) == 0;
}

#if defined(__AVR__) && NO_SOFT_RESET == 0
void (*resetFunc)(void) = 0;

#elif defined(__arm__) && NO_SOFT_RESET == 0
void resetFunc() {
  NVIC_SystemReset();
}

#elif NO_SOFT_RESET != 0
void resetFunc() {
  Serial.println(F("This build of KernelUNO has been configured to disable software resets."));
}

#else
void resetFunc() {
  Serial.println(F("KernelUNO doesn't support software resets on this hardware."));
}

#endif

// OPT
void addDmesg(const __FlashStringHelper* msg) {
  if (dmesgIndex >= DMESG_LINES) dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy_P(dmesg[dmesgIndex].message, (PGM_P)msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

void addDmesgRam(const char* msg) {
  if (dmesgIndex >= DMESG_LINES) dmesgIndex = 0;
  dmesg[dmesgIndex].timestamp = millis() / 1000;
  strncpy(dmesg[dmesgIndex].message, msg, DMESG_LEN - 1);
  dmesg[dmesgIndex].message[DMESG_LEN - 1] = '\0';
  dmesgIndex++;
}

void saveFS() {
#if NO_EEPROM == 1
  Serial.println(F("Filesystem could not be synced because EEPROM writes are disabled."));
#else
  EEPROM.update(EEPROM_ADDR, EEPROM_MAGIC);
  int addr = EEPROM_ADDR + 1;
  for (int i = 0; i < MAX_FILES; i++) {
    const uint8_t* p = (const uint8_t*)&fs[i];
    for (int b = 0; b < (int)sizeof(RAMFile); b++) {
      EEPROM.update(addr + b, p[b]);
    }
    addr += sizeof(RAMFile);
  }
  Serial.println(F("Synced to EEPROM."));
  addDmesg(F("FS saved to EEPROM"));
#endif
}

#if NO_EEPROM == 0
void loadFS() {
  if (EEPROM.read(EEPROM_ADDR) != EEPROM_MAGIC) return;
  int addr = EEPROM_ADDR + 1;
  for (int i = 0; i < MAX_FILES; i++) {
    EEPROM.get(addr, fs[i]);
    addr += sizeof(RAMFile);
  }
  addDmesg(F("FS loaded from EEPROM"));
}
#endif

void initFS() {
#if NO_EEPROM == 0
  // If saved filesystem exists, load it instead of defaults
  if (EEPROM.read(EEPROM_ADDR) == EEPROM_MAGIC) {
    loadFS();
    return;
  }
#endif

  int d, i;
  const char* dirs[] = { "home", "dev" };
  for (d = 0; d < 2; d++) {
    for (i = 0; i < MAX_FILES; i++) {
      if (!fs[i].active) {
        strncpy(fs[i].name, dirs[d], NAME_LEN - 1);
        fs[i].name[NAME_LEN - 1] = '\0';
        strncpy(fs[i].parentDir, "/", PATH_LEN - 1);
        fs[i].parentDir[PATH_LEN - 1] = '\0';
        fs[i].isDirectory = 1;
        fs[i].active = 1;
        break;
      }
    }
  }

  char devPath[PATH_LEN] = "/dev/";
  const char* pins[] = { "pin2", "pin3", "pin4" };
  for (d = 0; d < 3; d++) {
    for (i = 0; i < MAX_FILES; i++) {
      if (!fs[i].active) {
        strncpy(fs[i].name, pins[d], NAME_LEN - 1);
        fs[i].name[NAME_LEN - 1] = '\0';
        strncpy(fs[i].parentDir, devPath, PATH_LEN - 1);
        fs[i].parentDir[PATH_LEN - 1] = '\0';
        fs[i].isDirectory = 0;
        fs[i].content[0] = '\0';
        fs[i].active = 1;
        break;
      }
    }
  }

  // OPT
  addDmesg(F("Kernel initialized"));
  addDmesg(F("Filesystem mounted"));
  addDmesg(F("Ready for commands"));
}

void printPrompt() {
  Serial.print(F("root@arduino:"));
  Serial.print(currentPath);
  Serial.print(F("# "));
}

void setup() {
  Serial.begin(BAUD_RATE);
  initFS();
  delay(1000);
  Serial.print(F("--- KernelUNO v"));
  Serial.print(VERSION_NUMBER);
  Serial.println(F(" ---"));
  Serial.println(F("Type 'help' for commands"));
  printPrompt();
}

void generate_tone(int pin, int freq) {
#if NO_TONE_FUNC == 0
  tone(pin, freq);
#elif NO_TONE_FUNC == 1
  Serial.println(F("This build of KernelUNO has been configured to disable piezo support."));
#endif
}

void stop_tone(int pin) {
#if NO_TONE_FUNC == 0
  noTone(pin);
#elif NO_TONE_FUNC == 1
  Serial.println(F("This build of KernelUNO has been configured to disable piezo support."));
#endif
}

void clear_eeprom() {
#if NO_EEPROM == 0
  for (int i = 0; i < EEPROM.length(); i++) {
    Serial.println(String("Clearing byte #") + String(i));
    EEPROM.update(i, 255);
  }
#else
  Serial.println(F("Filesystem could not be cleared because EEPROM writes are disabled."));
#endif
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (inputLen > 0) {
        inputBuffer[inputLen] = '\0';
        Serial.println();
        addToHistory(inputBuffer);
        executeCommand(inputBuffer);
        inputLen = 0;
        memset(inputBuffer, 0, 32);
        printPrompt();
      } else {

        Serial.println();
        printPrompt();
      }
    } else if (c == 8 || c == 127) {
      if (inputLen > 0) {
        inputLen--;
        inputBuffer[inputLen] = '\0';
        Serial.print(F("\b \b"));
      }
    } else if (inputLen < 31) {
      Serial.print(c);
      inputBuffer[inputLen] = c;
      inputLen++;
    }
  }
}

int indexOf(const char* str, const char* substr) {
  int i, j, slen = strlen(str), sublen = strlen(substr);
  for (i = 0; i <= slen - sublen; i++) {
    int match = 1;
    for (j = 0; j < sublen; j++) {
      if (str[i + j] != substr[j]) {
        match = 0;
        break;
      }
    }
    if (match) return i;
  }
  return -1;
}

int atoi_safe(const char* str) {
  int num = 0;
  while (*str >= '0' && *str <= '9') {
    num = num * 10 + (*str - '0');
    str++;
  }
  return num;
}

void toLowercase(char* str) {
  int i;
  for (i = 0; str[i] != '\0'; i++) {
    if (str[i] >= 'A' && str[i] <= 'Z') str[i] = str[i] - 'A' + 'a';
  }
}

int safeConcatPath(char* dest, const char* add) {
  int destLen = strlen(dest);
  int addLen = strlen(add);
  if (destLen + addLen + 2 >= PATH_LEN) return 0;
  strncat(dest, add, PATH_LEN - destLen - 1);
  strncat(dest, "/", PATH_LEN - strlen(dest) - 1);
  return 1;
}

void runScript(const char* content);

void addToHistory(const char* cmd) {
  // History removed to save RAM
}

#if defined(__AVR__)
const __FlashStringHelper* identifyChip() {
  uint8_t sig1 = boot_signature_byte_get(0x02);
  uint8_t sig2 = boot_signature_byte_get(0x04);
  if (sig1 == 0x95 && sig2 == 0x0F) return F("ATmega328P");
  if (sig1 == 0x95 && sig2 == 0x14) return F("ATmega328");
  if (sig1 == 0x94 && sig2 == 0x06) return F("ATmega168");
  if (sig1 == 0x93 && sig2 == 0x07) return F("ATmega8");
  if (sig1 == 0x97 && sig2 == 0x03) return F("ATmega1280");
  if (sig1 == 0x98 && sig2 == 0x01) return F("ATmega2560");
  return F("AVR (unknown)");
}
#else
const __FlashStringHelper* identifyChip() {
  return F("Non-AVR");
}
#endif

void executeCommand(char* line) {
  char cmd[32] = "";
  char args[32] = "";
  int space1 = -1;
  int i, sp, pin, count;
  char buf[40];

  strncpy(cmd, line, 31);
  cmd[31] = '\0';

  for (i = 0; cmd[i] != '\0'; i++) {
    if (cmd[i] == ' ') {
      space1 = i;
      strncpy(args, cmd + i + 1, 31);
      args[31] = '\0';
      cmd[i] = '\0';
      break;
    }
  }

  toLowercase(cmd);

  // OPT
  if (strcmp_P(cmd, PSTR("pinmode")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: pinmode [pin] [in/out]"));
      return;
    }
    pin = atoi_safe(args);
    char mode[8] = "";
    strncpy(mode, args + sp + 1, 7);
    mode[7] = '\0';
    toLowercase(mode);
    if (strcmp_P(mode, PSTR("out")) == 0) {
      pinMode(pin, OUTPUT);
      Serial.println(F("Pin set to OUTPUT"));
    } else if (strcmp_P(mode, PSTR("in")) == 0) {
      pinMode(pin, INPUT_PULLUP);
      Serial.println(F("Pin set to INPUT_PULLUP"));
    }
  } else if (strcmp_P(cmd, PSTR("write")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: write [pin] [high/low]"));
      return;
    }
    pin = atoi_safe(args);
    char val[8] = "";
    strncpy(val, args + sp + 1, 7);
    val[7] = '\0';
    toLowercase(val);
    digitalWrite(pin, (strcmp_P(val, PSTR("high")) == 0 ? HIGH : LOW));
    Serial.println(F("Write OK."));
  } else if (strcmp_P(cmd, PSTR("read")) == 0) {
    pin = atoi_safe(args);
    int value = digitalRead(pin);
    Serial.print(F("Pin "));
    Serial.print(pin);
    Serial.print(F(" value: "));
    Serial.println(value);
  } else if (strcmp_P(cmd, PSTR("aread")) == 0) {
    pin = atoi_safe(args);
    int value = analogRead(pin);
    Serial.print(F("Analog pin "));
    Serial.print(pin);
    Serial.print(F(" value: "));
    Serial.println(value);
  } else if (strcmp_P(cmd, PSTR("pwm")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: pwm [pin] [0-255]"));
      return;
    }
    pin = atoi_safe(args);
    int pwmVal = atoi_safe(args + sp + 1);
    if (pwmVal < 0) pwmVal = 0;
    if (pwmVal > 255) pwmVal = 255;
    pinMode(pin, OUTPUT);
    analogWrite(pin, pwmVal);
    Serial.print(F("PWM set to "));
    Serial.println(pwmVal);
  } else if (strcmp_P(cmd, PSTR("blink")) == 0) {
    // blink [pin] [ms] [count]
    char* token = args;
    sp = indexOf(token, " ");
    if (sp == -1) {
      Serial.println(F("Usage: blink [pin] [ms] [count]"));
      return;
    }
    pin = atoi_safe(token);
    token = token + sp + 1;
    sp = indexOf(token, " ");
    int delayMs = (sp == -1) ? atoi_safe(token) : atoi_safe(token);
    count = (sp == -1) ? 5 : atoi_safe(token + sp + 1);
    pinMode(pin, OUTPUT);
    Serial.print(F("Blinking pin "));
    Serial.print(pin);
    Serial.print(F(" x"));
    Serial.println(count);
    for (int b = 0; b < count; b++) {
      digitalWrite(pin, HIGH);
      delay(delayMs);
      digitalWrite(pin, LOW);
      delay(delayMs);
    }
    Serial.println(F("Done."));
    addDmesg(F("blink complete"));
  } else if (strcmp_P(cmd, PSTR("gpio")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: gpio [pin] [on/off] OR gpio vixa [count]"));
      return;
    }
    char pinStr[8] = "";
    strncpy(pinStr, args, sp);
    pinStr[sp] = '\0';
    char action[8] = "";
    strncpy(action, args + sp + 1, 7);
    action[7] = '\0';
    toLowercase(action);

    if (strcmp_P(pinStr, PSTR("vixa")) == 0) {
      count = atoi_safe(action);
      if (count <= 0) count = 10;
      addDmesg(F("LED disco mode activated"));
      Serial.println(F("LED DISCO MODE!"));
      int cycle, p;
      for (cycle = 0; cycle < count; cycle++) {
        for (p = 2; p <= 13; p++) {
          pinMode(p, OUTPUT);
          digitalWrite(p, HIGH);
          delay(50);
          digitalWrite(p, LOW);
        }
      }
      Serial.println(F("Disco finished!"));
      addDmesg(F("Disco complete"));
    } else {
      pin = atoi_safe(pinStr);
      if (strcmp_P(action, PSTR("on")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        Serial.print(F("GPIO "));
        Serial.print(pin);
        Serial.println(F(" ON"));
      } else if (strcmp_P(action, PSTR("off")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        Serial.print(F("GPIO "));
        Serial.print(pin);
        Serial.println(F(" OFF"));
      } else if (strcmp_P(action, PSTR("toggle")) == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, !digitalRead(pin));
        Serial.print(F("GPIO "));
        Serial.print(pin);
        Serial.println(F(" toggled"));
      }
    }
  } else if (strcmp_P(cmd, PSTR("ls")) == 0) {
    int empty = 1, j;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && strcmp(fs[j].parentDir, currentPath) == 0) {
        Serial.print(fs[j].name);
        if (fs[j].isDirectory) Serial.print(F("/"));
        Serial.print(F("  "));
        empty = 0;
      }
    }
    if (empty) Serial.print(F("(empty)"));
    Serial.println();
  } else if (strcmp_P(cmd, PSTR("mkdir")) == 0 || strcmp_P(cmd, PSTR("touch")) == 0) {
    int foundSlot = -1, j;
    for (j = 0; j < MAX_FILES; j++) {
      if (!fs[j].active) {
        foundSlot = j;
        break;
      }
    }
    if (foundSlot == -1) {
      Serial.println(F("No space."));
      return;
    }
    strncpy(fs[foundSlot].name, args, NAME_LEN - 1);
    fs[foundSlot].name[NAME_LEN - 1] = '\0';
    strncpy(fs[foundSlot].parentDir, currentPath, PATH_LEN - 1);
    fs[foundSlot].parentDir[PATH_LEN - 1] = '\0';
    fs[foundSlot].isDirectory = (strcmp_P(cmd, PSTR("mkdir")) == 0);
    fs[foundSlot].content[0] = '\0';
    fs[foundSlot].active = 1;
    Serial.println(F("OK."));
  } else if (strcmp_P(cmd, PSTR("cd")) == 0) {
    if (strcmp_P(args, PSTR("..")) == 0 || strcmp_P(args, PSTR("/")) == 0) {
      strncpy(currentPath, "/", PATH_LEN - 1);
      currentPath[PATH_LEN - 1] = '\0';
    } else {
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if (fs[j].active && fs[j].isDirectory && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
          if (!safeConcatPath(currentPath, fs[j].name)) {
            strncpy(currentPath, "/", PATH_LEN - 1);
            currentPath[PATH_LEN - 1] = '\0';
            Serial.println(F("Path too long."));
            return;
          }
          found = 1;
          break;
        }
      }
      if (!found) Serial.println(F("No dir."));
    }
  } else if (strcmp_P(cmd, PSTR("pwd")) == 0) {
    Serial.println(currentPath);
  } else if (strcmp_P(cmd, PSTR("echo")) == 0) {
    int arrow = indexOf(args, " > ");
    if (arrow != -1) {
      char text[40] = "";
      strncpy(text, args, arrow);
      text[arrow] = '\0';
      char filename[12] = "";
      strncpy(filename, args + arrow + 3, NAME_LEN - 1);
      filename[NAME_LEN - 1] = '\0';
      int j, found = 0;
      for (j = 0; j < MAX_FILES; j++) {
        if (fs[j].active && !fs[j].isDirectory && strcmp(filename, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
          strncpy(fs[j].content, text, CONTENT_LEN - 1);
          fs[j].content[CONTENT_LEN - 1] = '\0';
          Serial.println(F("Saved."));
          if (strcmp_P(fs[j].parentDir, PSTR("/dev/")) == 0 && strncmp_P(fs[j].name, PSTR("pin"), 3) == 0) {
            int devPin = atoi_safe(fs[j].name + 3);
            if (devPin > 0) {
              pinMode(devPin, OUTPUT);
              digitalWrite(devPin, (text[0] == '1') ? HIGH : LOW);
            }
          }
          found = 1;
          break;
        }
      }
      if (!found) Serial.println(F("File not found."));
    } else {
      Serial.println(args);
    }
  } else if (strcmp_P(cmd, PSTR("cat")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && !fs[j].isDirectory && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        Serial.println(fs[j].content);
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("File not found."));
  } else if (strcmp_P(cmd, PSTR("info")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        Serial.print(F("Name: "));
        Serial.println(fs[j].name);
        Serial.print(F("Type: "));
        Serial.println(fs[j].isDirectory ? F("Directory") : F("File"));
        Serial.print(F("Size: "));
        Serial.print(strlen(fs[j].content));
        Serial.println(F(" bytes"));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("Not found."));
  } else if (strcmp_P(cmd, PSTR("rm")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        if (fs[j].isDirectory) {
          char dirPath[PATH_LEN];
          strncpy(dirPath, currentPath, PATH_LEN - 1);
          dirPath[PATH_LEN - 1] = '\0';
          strncat(dirPath, args, PATH_LEN - strlen(dirPath) - 1);
          strncat(dirPath, "/", PATH_LEN - strlen(dirPath) - 1);
          int k;
          for (k = 0; k < MAX_FILES; k++) {
            if (fs[k].active && strncmp(fs[k].parentDir, dirPath, strlen(dirPath)) == 0) {
              fs[k].active = 0;
            }
          }
        }
        fs[j].active = 0;
        Serial.println(F("Removed."));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("Not found."));
  } else if (strcmp_P(cmd, PSTR("cp")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: cp [src] [dst]"));
      return;
    }
    char srcName[NAME_LEN];
    char dstName[NAME_LEN];
    strncpy(srcName, args, sp);
    srcName[sp] = '\0';
    strncpy(dstName, args + sp + 1, NAME_LEN - 1);
    dstName[NAME_LEN - 1] = '\0';
    
    int j, srcIdx = -1, dstSlot = -1;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && !fs[j].isDirectory && strcmp(srcName, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        srcIdx = j;
        break;
      }
    }
    if (srcIdx == -1) {
      Serial.println(F("Source not found."));
      return;
    }
    for (j = 0; j < MAX_FILES; j++) {
      if (!fs[j].active) {
        dstSlot = j;
        break;
      }
    }
    if (dstSlot == -1) {
      Serial.println(F("No space."));
      return;
    }
    strncpy(fs[dstSlot].name, dstName, NAME_LEN - 1);
    fs[dstSlot].name[NAME_LEN - 1] = '\0';
    strncpy(fs[dstSlot].content, fs[srcIdx].content, CONTENT_LEN - 1);
    fs[dstSlot].content[CONTENT_LEN - 1] = '\0';
    strncpy(fs[dstSlot].parentDir, currentPath, PATH_LEN - 1);
    fs[dstSlot].parentDir[PATH_LEN - 1] = '\0';
    fs[dstSlot].isDirectory = 0;
    fs[dstSlot].active = 1;
    Serial.println(F("Copied."));
  } else if (strcmp_P(cmd, PSTR("mv")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: mv [src] [dst]"));
      return;
    }
    char srcName[NAME_LEN];
    char dstName[NAME_LEN];
    strncpy(srcName, args, sp);
    srcName[sp] = '\0';
    strncpy(dstName, args + sp + 1, NAME_LEN - 1);
    dstName[NAME_LEN - 1] = '\0';
    
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && strcmp(srcName, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        strncpy(fs[j].name, dstName, NAME_LEN - 1);
        fs[j].name[NAME_LEN - 1] = '\0';
        Serial.println(F("Renamed."));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("Not found."));
  } else if (strcmp_P(cmd, PSTR("wc")) == 0) {
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && !fs[j].isDirectory && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        Serial.print(F("Bytes: "));
        Serial.println(strlen(fs[j].content));
        found = 1;
        break;
      }
    }
    if (!found) Serial.println(F("File not found."));
  } else if (strcmp_P(cmd, PSTR("dmesg")) == 0) {
    Serial.println(F("=== KERNEL MESSAGES ==="));
    int j;
    for (j = 0; j < DMESG_LINES; j++) {
      if (dmesg[j].message[0] != '\0') {
        Serial.print(F("["));
        Serial.print(dmesg[j].timestamp);
        Serial.print(F("] "));
        Serial.println(dmesg[j].message);
      }
    }
  } else if (strcmp_P(cmd, PSTR("uptime")) == 0) {
    unsigned long s = millis() / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;
    Serial.print(F("up "));
    Serial.print(h);
    Serial.print(F("h "));
    Serial.print(m);
    Serial.print(F("m "));
    Serial.print(sec);
    Serial.println(F("s"));
    addDmesg(F("uptime command"));
  } else if (strcmp_P(cmd, PSTR("df")) == 0 || strcmp_P(cmd, PSTR("free")) == 0) {
    // RAM
    int ramFree = freeMemory();
    Serial.print(F("RAM: "));
    Serial.print(ramFree);
    Serial.print(F("/2048B free"));
    
    // Flash
    Serial.print(F(" | Flash: 32KB"));
    
#if NO_EEPROM == 0
    // EEPROM
    Serial.print(F(" | EEPROM: "));
    if (EEPROM.read(EEPROM_ADDR) == EEPROM_MAGIC) {
      int used = 1 + (MAX_FILES * sizeof(RAMFile));
      Serial.print(1024 - used);
      Serial.print(F("/1024B"));
    } else {
      Serial.print(F("1024/1024B"));
    }
#endif

    // Files
    int filesUsed = 0;
    for (int j = 0; j < MAX_FILES; j++) {
      if (fs[j].active) filesUsed++;
    }
    Serial.print(F(" | Files: "));
    Serial.print(MAX_FILES - filesUsed);
    Serial.print(F("/"));
    Serial.print(MAX_FILES);
    Serial.println();
  } else if (strcmp_P(cmd, PSTR("whoami")) == 0) {
    Serial.println(F("root"));
  } else if (strcmp_P(cmd, PSTR("mount")) == 0) {
    Serial.println(F("/ (ramfs), /dev (devfs)"));
#if NO_EEPROM == 0
    if (EEPROM.read(EEPROM_ADDR) == EEPROM_MAGIC) {
      Serial.println(F("/persist (eeprom, synced)"));
    }
#endif
  } else if (strcmp_P(cmd, PSTR("calc")) == 0) {
    // Простой калькулятор для +, -, *, /
    int opPos = -1;
    char op = '\0';
    for (i = 0; args[i] != '\0'; i++) {
      if (args[i] == '+' || args[i] == '-' || args[i] == '*' || args[i] == '/') {
        opPos = i;
        op = args[i];
        break;
      }
    }
    if (opPos == -1) {
      Serial.println(F("Usage: calc [a+b] or [a-b] or [a*b] or [a/b]"));
      return;
    }
    int a = atoi_safe(args);
    int b = atoi_safe(args + opPos + 1);
    int result = 0;
    if (op == '+') result = a + b;
    else if (op == '-') result = a - b;
    else if (op == '*') result = a * b;
    else if (op == '/' && b != 0) result = a / b;
    else {
      Serial.println(F("Invalid operation or div by zero."));
      return;
    }
    Serial.print(a);
    Serial.print(F(" "));
    Serial.print(op);
    Serial.print(F(" "));
    Serial.print(b);
    Serial.print(F(" = "));
    Serial.println(result);
  } else if (strcmp_P(cmd, PSTR("rand")) == 0) {
    int maxVal = atoi_safe(args);
    if (maxVal <= 0) maxVal = 100;
    randomSeed(analogRead(0) ^ millis());
    int r = random(maxVal);
    Serial.println(r);
  } else if (strcmp_P(cmd, PSTR("ascii")) == 0) {
    if (args[0] == '\0') {
      Serial.println(F("Usage: ascii [char]"));
      return;
    }
    Serial.print(F("ASCII code of '"));
    Serial.print(args[0]);
    Serial.print(F("': "));
    Serial.println((int)args[0]);
  } else if (strcmp_P(cmd, PSTR("hex")) == 0) {
    int num = atoi_safe(args);
    Serial.print(F("0x"));
    Serial.println(num, HEX);
  } else if (strcmp_P(cmd, PSTR("repeat")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: repeat [n] [cmd]"));
      return;
    }
    int times = atoi_safe(args);
    char repeatCmd[32];
    strncpy(repeatCmd, args + sp + 1, 31);
    repeatCmd[31] = '\0';
    Serial.print(F("Repeating "));
    Serial.print(times);
    Serial.println(F(" times..."));
    for (int r = 0; r < times; r++) {
      Serial.print(F("["));
      Serial.print(r + 1);
      Serial.print(F("] "));
      executeCommand(repeatCmd);
    }
    Serial.println(F("Repeat done."));
  } else if (strcmp_P(cmd, PSTR("guess")) == 0) {
    if (!gameActive) {
      randomSeed(analogRead(0) ^ millis());
      guessNumber = random(1, 101);
      guessAttempts = 0;
      gameActive = true;
      Serial.println(F("=== GUESS THE NUMBER ==="));
      Serial.println(F("I'm thinking of a number between 1 and 100."));
      Serial.println(F("Use: guess [number] to make a guess"));
      Serial.println(F("Use: guess quit to exit"));
      addDmesg(F("Guess game started"));
    } else if (strcmp_P(args, PSTR("quit")) == 0) {
      Serial.print(F("Game over! The number was "));
      Serial.println(guessNumber);
      gameActive = false;
      addDmesg(F("Guess game quit"));
    } else if (args[0] == '\0') {
      Serial.print(F("Game active. Attempts: "));
      Serial.println(guessAttempts);
      Serial.println(F("Use: guess [number] to make a guess"));
    } else {
      int userGuess = atoi_safe(args);
      if (userGuess < 1 || userGuess > 100) {
        Serial.println(F("Please guess between 1 and 100."));
        return;
      }
      guessAttempts++;
      if (userGuess < guessNumber) {
        Serial.print(userGuess);
        Serial.println(F(" is too low! Try higher."));
      } else if (userGuess > guessNumber) {
        Serial.print(userGuess);
        Serial.println(F(" is too high! Try lower."));
      } else {
        Serial.println(F("*** CORRECT! ***"));
        Serial.print(F("You guessed it in "));
        Serial.print(guessAttempts);
        Serial.println(F(" attempts!"));
        gameActive = false;
        addDmesg(F("Guess game won"));
      }
    }
  } else if (strcmp_P(cmd, PSTR("ed")) == 0) {
    sp = indexOf(args, " ");
    if (sp == -1) {
      Serial.println(F("Usage: ed [file] [text]"));
      return;
    }
    char n[NAME_LEN];
    strncpy(n, args, sp);
    n[sp] = '\0';
    int j;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && !fs[j].isDirectory && strcmp(n, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        strncpy(fs[j].content, args + sp + 1, CONTENT_LEN - 1);
        fs[j].content[CONTENT_LEN - 1] = '\0';
        Serial.println(F("OK"));
        return;
      }
    }
    Serial.println(F("Not found"));
  } else if (strcmp_P(cmd, PSTR("uname")) == 0) {
    Serial.println(F("KernelUNO v1.0"));
    Serial.print(F("Architecture: "));
    Serial.println(HW_ARCH);
    Serial.print(F("Hardware: "));
    Serial.println(HW_NAME);
    Serial.print(F("RAM: "));
    Serial.print(freeMemory());
    Serial.println(F(" bytes free"));
  } else if (strcmp_P(cmd, PSTR("reboot")) == 0) {
    Serial.println(F("Rebooting..."));
    addDmesg(F("System reboot"));
    saveFS();
    delay(500);
    resetFunc();
  } else if (strcmp_P(cmd, PSTR("clear")) == 0) {
    int j;
    for (j = 0; j < 30; j++) Serial.println();
  } else if (strcmp_P(cmd, PSTR("sh")) == 0) {
    if (args[0] == '\0') {
      Serial.println(F("Usage: sh [script]"));
      return;
    }
    int j, found = 0;
    for (j = 0; j < MAX_FILES; j++) {
      if (fs[j].active && !fs[j].isDirectory && strcmp(args, fs[j].name) == 0 && strcmp(fs[j].parentDir, currentPath) == 0) {
        found = 1;
        addDmesg(F("sh: running script"));
        runScript(fs[j].content);
        break;
      }
    }
    if (!found) Serial.println(F("Script not found."));
  } else if (strcmp_P(cmd, PSTR("help")) == 0) {
    Serial.println(F("Commands: ls, cd, pwd, mkdir, touch, cat, echo, rm, info, cp, mv, wc"));
    Serial.println(F("          pinmode, write, read, aread, pwm, blink, gpio, sh"));
    Serial.println(F("          uptime, uname, dmesg, df, free, mount, whoami, clear, reboot"));
    Serial.println(F("          tone, notone, sleep, sync, build-info, fastfetch, repeat"));
    Serial.println(F("          calc, rand, ascii, hex, guess, ed"));
    Serial.println(F("GPIO: gpio [pin] on/off/toggle | gpio vixa [count]"));
    Serial.println(F("SH:   sh [file] -- run script (use ; as separator)"));
    Serial.println(F("GAME: guess -- start number guessing game (1-100)"));

  } else if (strcmp_P(cmd, PSTR("tone")) == 0) {
    int pin, freq;
    sp = indexOf(args, " ");
    if (sp != -1) {
      freq = atoi_safe(args + sp + 1);
      sp = indexOf(args + sp, " ");
      pin = atoi_safe(args + sp);
      generate_tone(pin, freq);
    }

    else {
      Serial.println(F("Usage: tone [pin] [freq]"));
    }

  }

  else if (strcmp_P(cmd, PSTR("reset-fs")) == 0) {
#if NO_EEPROM == 0
    Serial.println(F("Clearing the filesystem! This may take several minutes."));
    clear_eeprom();
    Serial.println(F("Rebooting!"));
    resetFunc();
#else
    Serial.println(F("Clearing the EEPROM data is disabled in this build."));
#endif
  }

  else if (strcmp_P(cmd, PSTR("notone")) == 0) {
    int pin;
    if (!is_argstr_empty(args)) {
      pin = atoi_safe(args);
      stop_tone(pin);
    }

    else {
      Serial.println(F("Usage: notone [pin]"));
    }
  }

  else if (strcmp_P(cmd, PSTR("sleep")) == 0) {
    int time;
    if (!is_argstr_empty(args)) {
      time = atoi_safe(args);
      delay(time * 1000);
    }

    else {
      Serial.println(F("Usage: sleep [time]"));
    }

  }

  else if (strcmp_P(cmd, PSTR("sync")) == 0) {
    saveFS();
  }

  else if (strcmp_P(cmd, PSTR("build-info")) == 0) {
    Serial.println(String("NO_MEMORY_CHECK: ") + NO_MEMORY_CHECK);
    Serial.println(String("NO_SOFT_RESET: ") + NO_SOFT_RESET);
    Serial.println(String("NO_TONE_FUNC: ") + NO_TONE_FUNC);
    Serial.println(String("NO_EEPROM: ") + NO_EEPROM);
    Serial.println(String("HW_NAME: ") + HW_NAME);
    Serial.println(String("HW_ARCH: ") + HW_ARCH);
  }

  else if (strcmp_P(cmd, PSTR("fastfetch")) == 0) {
    unsigned long s = millis() / 1000;
    unsigned long h = s / 3600;
    unsigned long m = (s % 3600) / 60;
    unsigned long sec = s % 60;

#if defined(__AVR__)
    uint8_t sig0 = boot_signature_byte_get(0x00);
    const __FlashStringHelper* manufacturer;
    if (sig0 == 0x1E) manufacturer = F("Atmel");
    else if (sig0 == 0x0C) manufacturer = F("Logic Green");
    else manufacturer = F("Unknown");
#else
    const __FlashStringHelper* manufacturer = F("Non-AVR");
#endif

    Serial.print(F("       mmMMMMMMmm           mmMMMMMMmm       "));
    Serial.println(F("OS: KernelUNO v" VERSION_NUMBER));
    Serial.print(F("    mMzzMMMMMMMzzMMm     mMzzMMMMMMMMzzMm    "));
    Serial.print(F("Manufacturer: ")); Serial.println(manufacturer);
    Serial.print(F("  mMzzM          MzzMm mMzzM         mMzzM   "));
    Serial.print(F("MCU: ")); Serial.print(identifyChip());
    Serial.print(F(" @ ")); Serial.print((unsigned long)F_CPU / 1000000UL);
    Serial.println(F("MHz"));
    Serial.print(F("  MMMm             MMMMMMM      M      mMMM  "));
    Serial.print(F("RAM: ")); Serial.print(freeMemory()); Serial.println(F(" bytes free"));
    Serial.print(F(" mMMM   mMMMMMMm    MMMMM     mMMMM    MMzm  "));
    Serial.print(F("Uptime: "));
    Serial.print(h); Serial.print(F("h "));
    Serial.print(m); Serial.print(F("m "));
    Serial.print(sec); Serial.println(F("s"));
    Serial.print(F("  zMMm             MMMMMMM      M      mMMM  "));
    Serial.print(F("Compiled: ")); Serial.print(F(__DATE__)); Serial.print(F(" ")); Serial.println(F(__TIME__));
    Serial.println(F("  mMzMm          mMzMm mMzMm          MMMMm  "));
    Serial.println(F("    MMzzMMMmmmMMzzMm     mMzzMMmmmMMMzzMm    "));
    Serial.println(F("      mmMMMMMMMMm           mMMMMMMMMmm      "));
    addDmesg(F("fastfetch command"));
  }

  else {
    Serial.println(F("Unknown command."));
  }
}

// Interpreter sh
void runScript(const char* content) {
  char line[32];
  int ci = 0, li = 0, lineNum = 0;
  int len = strlen(content);

  while (ci <= len) {
    char c = (ci < len) ? content[ci] : ';';
    ci++;
    if (c == ';' || c == '\n' || c == '\r') {
      if (li > 0) {
        line[li] = '\0';
        lineNum++;
        Serial.print(F("[sh:"));
        Serial.print(lineNum);
        Serial.print(F("] "));
        Serial.println(line);
        executeCommand(line);
        li = 0;
      }
    } else {
      if (li < 31) line[li++] = c;
    }
  }
  addDmesg(F("sh: script done"));
  Serial.println(F("[sh] done."));
}
