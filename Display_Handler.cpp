#include "Display_Handler.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "ABPM_Manager.h"

static Adafruit_SH1107 display(OLED_WIDTH, OLED_HEIGHT, &Wire);
static bool displayReady = false;

// ─── Internal Helpers ────────────────────────────────────────────────────────

static void drawBatteryIcon(int x, int y, uint8_t percent) {
    // Battery body: 20x10px
    display.drawRect(x, y, 20, 10, SH110X_WHITE);
    display.fillRect(x + 20, y + 3, 2, 4, SH110X_WHITE); // + terminal
    uint8_t fill = (uint16_t)percent * 18 / 100;
    if (fill > 0) display.fillRect(x + 1, y + 1, fill, 8, SH110X_WHITE);
}

static void drawBLEIcon(int x, int y, bool connected) {
    // Simple "B" indicator
    display.setTextSize(1);
    display.setCursor(x, y);
    if (connected) {
        display.print(F("[BT]"));
    } else {
        display.print(F("[  ]"));
    }
}

static void clearAndFlush() {
    display.clearDisplay();
}

static void drawTopBar(uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv) {
    display.setTextSize(1);
    
    // Line separator
    display.drawLine(0, 14, 127, 14, SH110X_WHITE);

    // Battery Text
    display.setCursor(85, 3);
    display.printf("%d%%", battPercent);

    // Battery Icon
    display.drawRect(110, 3, 14, 7, SH110X_WHITE);
    display.drawRect(124, 5, 2, 3, SH110X_WHITE); // Terminal
    uint8_t battFill = (battPercent * 10) / 100;
    if (battFill > 0) display.fillRect(112, 5, battFill, 3, SH110X_WHITE);
    
    if (abpmOn) {
        display.drawCircle(6, 6, 3, SH110X_WHITE);
        display.drawLine(6, 6, 6, 4, SH110X_WHITE);
        display.drawLine(6, 6, 8, 6, SH110X_WHITE);
        display.setCursor(12, 3);
        display.print(F("ABP"));
        
        if (ABPM_Manager::hasBuzzerEnabled()) {
            // Draw a small Bell icon
            int bx = 38;
            int by = 3;
            display.fillCircle(bx + 3, by + 2, 2, SH110X_WHITE); // Top dome
            display.fillRect(bx, by + 3, 7, 3, SH110X_WHITE);   // Body
            display.drawPixel(bx + 3, by + 6, SH110X_WHITE);    // Clapper
        }
    }

    if (bleConnected) {
        display.drawLine(65, 2, 65, 10, SH110X_WHITE);
        display.drawLine(65, 2, 68, 4, SH110X_WHITE);
        display.drawLine(68, 4, 65, 6, SH110X_WHITE);
        display.drawLine(65, 6, 68, 8, SH110X_WHITE);
        display.drawLine(68, 8, 65, 10, SH110X_WHITE);
        display.drawLine(65, 6, 62, 2, SH110X_WHITE);
        display.drawLine(65, 6, 62, 10, SH110X_WHITE);
    } else if (isAdv) {
        display.setCursor(62, 3);
        display.print(F("AD"));
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

namespace Display_Handler {

void init() {
    Wire.begin(PRESSURE_SDA, PRESSURE_SCL);
    
    pinMode(BATTERY_ADC_PIN, ANALOG);

    if (!display.begin(OLED_ADDRESS, true)) {
        Serial.println("[OLED] SH1107 init FAILED - check address/wiring");
        displayReady = false;
        return;
    }
    displayReady = true;
    display.setTextColor(SH110X_WHITE);
    display.cp437(true);
    // showBoot(); // Removed: Device will start with screen OFF until long-pressed
    Serial.println("[OLED] Display initialized OK (Screen OFF)");
}

void off() {
    if (!displayReady) return;
    display.clearDisplay();
    display.display();
    // SH1107 command for off
    display.oled_command(SH110X_DISPLAYOFF);
}

void on() {
    if (!displayReady) return;
    display.oled_command(SH110X_DISPLAYON);
}

void flipScreen(bool flipped) {
    if (!displayReady) return;
    // Use Adafruit GFX software rotation — transforms drawing coordinates BEFORE
    // content is written to the framebuffer. Hardware register commands (segment
    // remap / COM scan) only affect the hardware scan direction AFTER the fact,
    // which produces a mirror image on 128x32. setRotation(2) is the correct 180° fix.
    display.setRotation(flipped ? 2 : 0);
    // Clear display so current stale content is wiped.
    // The next loop render (idle/results/inflating) will redraw in new orientation.
    display.clearDisplay();
    display.display();
    Serial.printf("[Display] Screen rotation: %s\n", flipped ? "180deg (flipped)" : "0deg (normal)");
}

// ── Splash / Boot ────────────────────────────────────────────────────────────
void showBoot() {
    if (!displayReady) return;
    clearAndFlush();
    display.setTextSize(2);
    display.setCursor(5, 40);
    display.print(F("BP MONITOR"));
    display.setTextSize(1);
    display.setCursor(20, 70);
    display.print(F("IF MED TECH"));
    display.setCursor(35, 110);
    display.print(F("v1.0.0"));
    display.display();
}

// ── Wait For Start (Locked screen) ──────────────────────────────────────────
void showWaitForStart(uint8_t battPercent, bool bleActive) {
    if (!displayReady) return;
    clearAndFlush();

    // Top Bar
    drawTopBar(battPercent, false, false, bleActive);

    // Center "LOCKED"
    display.setTextSize(2);
    display.setCursor(25, 45);
    display.print(F("LOCKED"));

    display.setTextSize(1);
    display.setCursor(15, 80);
    display.print(F("Hold button to"));
    display.setCursor(35, 92);
    display.print(F("UNLOCK"));

    display.display();
}

// ── Charging Screen ──────────────────────────────────────────────────────────
void showCharging(uint8_t battPercent, float battVoltage) {
    if (!displayReady) return;
    clearAndFlush();

    display.setTextSize(1);
    display.setCursor(35, 10);
    display.print(F("CHARGING"));

    // Large Battery Frame
    display.drawRect(24, 30, 80, 40, SH110X_WHITE);
    display.fillRect(104, 45, 4, 10, SH110X_WHITE); // Terminal

    // Fill logic
    uint8_t fill = (battPercent * 76) / 100;
    if (fill > 0) {
        display.fillRect(26, 32, fill, 36, SH110X_WHITE);
    }

    display.setTextSize(2);
    display.setCursor(45, 80);
    display.printf("%d%%", battPercent);

    display.setTextSize(1);
    display.setCursor(45, 110);
    display.printf("%.2fV", battVoltage);

    display.display();
}

// ── Brief wake while charging (button press) ─────────────────────────────────
void showChargingIdle(uint8_t battPercent, int minutesToFull) {
    if (!displayReady) return;
    clearAndFlush();
    
    display.setTextSize(3);
    display.setCursor(35, 40);
    display.printf("%d%%", battPercent);
    
    display.setTextSize(1);
    display.setCursor(20, 85);
    if (battPercent >= 100) {
        display.print(F("Battery Full"));
    } else if (minutesToFull > 0) {
        int h = minutesToFull / 60;
        int m = minutesToFull % 60;
        if (h > 0) {
            display.printf("Full in %dh %dm", h, m);
        } else {
            display.printf("Full in %d mins", m);
        }
    } else {
        display.print(F("Charging..."));
    }
    
    display.display();
}

// ── Idle / Device ON ─────────────────────────────────────────────────────────
void showIdle(uint8_t battPercent, bool bleConnected, bool isAdv, bool abpmOn, float pressure, uint8_t sbp, uint8_t dbp, uint8_t hr) {
    if (!displayReady) return;
    display.clearDisplay();

    drawTopBar(battPercent, abpmOn, bleConnected, isAdv);

    display.setTextSize(2);
    display.setCursor(34, 30);
    display.print(F("READY"));

    display.setTextSize(1);
    display.drawLine(0, 55, 127, 55, SH110X_WHITE);

    if (sbp > 0) {
        display.setCursor(13, 65);
        display.print(F("Last Measurement:"));
        
        char buf[16];
        sprintf(buf, "%d/%d", sbp, dbp);
        int len = strlen(buf);
        int x = (128 - (len * 12)) / 2;
        display.setTextSize(2);
        display.setCursor(x, 80);
        display.print(buf);
        
        display.setTextSize(1);
        display.setCursor(x + (len * 12) + 4, 85);
        display.printf("(%d)", hr);
    } else {
        display.setCursor(13, 75);
        display.print(F("No data available"));
    }

    display.drawLine(0, 105, 127, 105, SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 115);
    if (abpmOn) {
        uint32_t nextT = ABPM_Manager::getNextScheduledTime();
        char nBuf[32];
        if (nextT > 0) {
            time_t nt = (time_t)nextT;
            struct tm *nt_tm = localtime(&nt);
            sprintf(nBuf, "Next ABPM: %02d:%02d Hrs", nt_tm->tm_hour, nt_tm->tm_min);
        } else {
            strcpy(nBuf, "Next ABPM: --:-- Hrs");
        }
        int nLen = strlen(nBuf);
        int nx = (128 - (nLen * 6)) / 2;
        display.setCursor(nx, 115);
        display.print(nBuf);
    } else {
        display.setCursor(31, 115);
        display.print(F("Manual Mode"));
    }

    display.display();
}

// ── Inflating ────────────────────────────────────────────────────────────────
void showInflating(bool showText, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv) {
    if (!displayReady) return;
    clearAndFlush();

    drawTopBar(battPercent, abpmOn, bleConnected, isAdv);

    if (showText) {
        display.setTextSize(2);
        display.setCursor(10, 50);
        display.print(F("INFLATING"));
        
        // Dynamic dots
        static uint8_t dots = 0;
        dots = (dots + 1) % 4;
        display.setCursor(45, 80);
        for(int i=0; i<dots; i++) display.print(".");
    }

    display.display();
}

// ── Measuring / Deflating ────────────────────────────────────────────────────
// ── Measuring / Deflating ────────────────────────────────────────────────────
void showMeasuring(bool alternateText, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv) {
    if (!displayReady) return;
    clearAndFlush();

    drawTopBar(battPercent, abpmOn, bleConnected, isAdv);

    display.setTextSize(2);
    display.setCursor(10, 45);
    display.print(F("ANALYZING"));
    
    display.setTextSize(1);
    display.setCursor(20, 75);
    display.print(F("Please stay still"));
    
    if (alternateText) {
        display.setCursor(5, 100);
        display.print(F("Wait for results..."));
    }

    display.display();
}

// ── Results ──────────────────────────────────────────────────────────────────
void showResults(uint8_t sbp, uint8_t dbp, uint8_t hr, uint8_t battPercent, bool abpmOn, bool bleConnected, bool isAdv) {
    if (!displayReady) return;
    clearAndFlush();

    drawTopBar(battPercent, abpmOn, bleConnected, isAdv);

    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print(F("SBP / DBP"));

    // Centered SBP/DBP (Size 2)
    char buf[16];
    sprintf(buf, "%d/%d", sbp, dbp);
    int len = strlen(buf);
    int x = (128 - (len * 12)) / 2;
    display.setTextSize(2);
    display.setCursor(x < 0 ? 0 : x, 42);
    display.print(buf);

    // Centered Pulse (Size 2)
    char pBuf[16];
    sprintf(pBuf, "Pulse: %d", hr);
    int pLen = strlen(pBuf);
    int px = (128 - (pLen * 12)) / 2;
    display.setTextSize(2);
    display.setCursor(px, 68);
    display.print(pBuf);

    display.drawLine(0, 105, 127, 105, SH110X_WHITE);
    if (abpmOn) {
        uint32_t nextT = ABPM_Manager::getNextScheduledTime();
        char nBuf[32];
        if (nextT > 0) {
            time_t nt = (time_t)nextT;
            struct tm *nt_tm = localtime(&nt);
            sprintf(nBuf, "Next: %02d:%02d Hrs", nt_tm->tm_hour, nt_tm->tm_min);
        } else {
            strcpy(nBuf, "Next: --:-- Hrs");
        }
        int nLen = strlen(nBuf);
        int nx = (128 - (nLen * 6)) / 2;
        display.setTextSize(1);
        display.setCursor(nx, 115);
        display.print(nBuf);
    }

    display.display();
}

// ── ABPM Pre-alarm ───────────────────────────────────────────────────────────
void showAbpmAlarm(uint8_t secondsLeft) {
    if (!displayReady) return;
    clearAndFlush();

    display.setTextSize(1);
    display.setCursor(20, 25);
    display.print(F("ABPM STARTING IN"));
    
    display.setTextSize(4);
    display.setCursor(35, 55);
    display.printf("%02d", secondsLeft);
    
    display.setTextSize(1);
    display.setCursor(90, 75);
    display.print(F("s"));

    display.display();
}

// ── Saving to memory ─────────────────────────────────────────────────────────
void showAbpmSaving() {
    if (!displayReady) return;
    clearAndFlush();
    display.setTextSize(1);
    display.setCursor(25, 45);
    display.print(F("Saving Data..."));
    display.setCursor(25, 65);
    display.print(F("Going to Sleep"));
    display.display();
}

// ── Sleep Transition ─────────────────────────────────────────────────────────
void showSleep() {
    if (!displayReady) return;
    clearAndFlush();
    display.setTextSize(1);
    display.setCursor(25, 60);
    display.print(F("SLEEP MODE ON"));
    display.display();
}

void showPowerOff() {
    if (!displayReady) return;
    clearAndFlush();
    display.setTextSize(1);
    display.setCursor(35, 60);
    display.print(F("POWER OFF"));
    display.display();
}

// ── Maintenance Menu ─────────────────────────────────────────────────────────
void showEventMarked() {
    if (!displayReady) return;
    clearAndFlush();
    
    // Draw outer box
    display.drawRect(0, 0, 128, 128, SH110X_WHITE);
    
    // Draw title
    display.setTextSize(2);
    display.setCursor(35, 40);
    display.print(F("EVENT"));
    display.setCursor(25, 60);
    display.print(F("MARKED"));
    
    // Draw subtext
    display.setTextSize(1);
    display.setCursor(14, 95);
    display.print(F("Recorded in Memory"));
    
    display.display();
}

// ── Error ────────────────────────────────────────────────────────────────────
void showError(const char* msg) {
    if (!displayReady) return;
    clearAndFlush();
    display.setTextSize(2);
    display.setCursor(30, 30);
    display.print(F("ERROR!"));
    
    display.setTextSize(1);
    display.setCursor(10, 65);
    display.print(msg);
    
    display.setCursor(10, 100);
    display.print(F("Press Reset"));
    display.display();
}

// ─── Battery Utilities ────────────────────────────────────────────────────────

float readBatteryVoltage() {
    int raw = analogRead(BATTERY_ADC_PIN);
    float adcV = (raw / 4095.0f) * BATTERY_ADC_VREF;
    return adcV * BATTERY_DIVIDER_RATIO;
}

uint8_t getBatteryPercent() {
    float v = readBatteryVoltage();
    if (v >= BATTERY_MAX_V) return 100;
    if (v <= BATTERY_MIN_V) return 0;
    return (uint8_t)((v - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f);
}

bool isCharging() {
    return false; // VBUS detection not available on this PCB
}

bool isBatteryCritical() {
    return (readBatteryVoltage() <= BATTERY_MIN_V);
}

} // namespace Display_Handler
