

// This Teensy3 native optimized version requires specific pins
//#define sclk 27
//#define mosi 26
#define cs 2
#define dc 3
#define rst 9
#define DISPLAYTIMEOUT 1500

#include "TeensyThreads.h"
#include <Adafruit_GFX.h>
#include "ST7735_t3.h"  // Local copy from TD1.48 that works for 0.96" IPS 160x80 display

#include <Fonts/Org_01.h>
#include "Yeysk16pt7b.h"
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansOblique24pt7b.h>
#include <Fonts/FreeSansBoldOblique24pt7b.h>

#define PULSE 1
#define VAR_TRI 2
#define FILTER_ENV 3
#define AMP_ENV 4

ST7735_t3 tft = ST7735_t3(cs, dc, 26, 27, rst);

String currentParameter = "";
String currentValue = "";
float currentFloatValue = 0.0;
String currentPgmNumU = "";
String currentPgmNumL = "";
String currentPatchNameU = "";
String currentPatchNameL = "";
String newPatchName = "";
const char *currentSettingsOption = "";
const char *currentSettingsValue = "";
int currentSettingsPart = SETTINGS;
int paramType = PARAMETER;

//boolean voiceOn[NO_OF_VOICES] = { false };
boolean MIDIClkSignal = false;

unsigned long timer = 0;

// MIDI note 0 = C-1, so note 48 = C3, 60 = C4
static const char * NOTE_NAMES[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

void startTimer() {
  if (state == PARAMETER) {
    timer = millis();
  }
}

void renderBootUpPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.drawRect(42, 30, 46, 11, ST7735_WHITE);
  tft.fillRect(88, 30, 61, 11, ST7735_WHITE);
  tft.setCursor(45, 31);
  tft.setFont(&Org_01);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.println("HYBRID");
  tft.setTextColor(ST7735_BLACK);
  tft.setCursor(91, 37);
  tft.println("SYNTHESIZER");
  tft.setTextColor(ST7735_YELLOW);
  tft.setFont(&Yeysk16pt7b);
  tft.setCursor(0, 70);
  tft.setTextSize(1);
  tft.println("POLYKIT");
  tft.setTextColor(ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(110, 95);
  tft.println(VERSION);
}

void showPerformancePage(uint16_t performanceNo, const String &name, uint16_t upperPatchNo, uint16_t lowerPatchNo) {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setTextSize(1);

  tft.print("Performance ");
  if (performanceNo < 100) tft.print('0');
  if (performanceNo < 10) tft.print('0');
  tft.println(performanceNo);

  tft.println();

  tft.println(name);

  tft.println();

  tft.print("Upper: ");
  if (upperPatchNo < 100) tft.print('0');
  if (upperPatchNo < 10) tft.print('0');
  tft.println(upperPatchNo);

  tft.print("Lower: ");
  if (lowerPatchNo < 100) tft.print('0');
  if (lowerPatchNo < 10) tft.print('0');
  tft.println(lowerPatchNo);

  tft.updateScreen();
}

void renderCurrentPatchPage() {
  if (wholemode) {
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&FreeSansBold18pt7b);
    tft.setCursor(5, 97);
    tft.setTextColor(ST7735_YELLOW);
    tft.setTextSize(1);
    tft.println(currentPgmNumL);

    tft.setCursor(80, 87);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    tft.println("Whole");

    tft.setTextColor(ST7735_BLACK);
    tft.setFont(&Org_01);
    tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);

    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(1, 122);
    tft.setTextColor(ST7735_WHITE);
    tft.println(currentPatchNameL);

  } else {
    
    // upper patch
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&FreeSansBold18pt7b);
    tft.setCursor(5, 29);
    tft.setTextColor(ST7735_YELLOW);
    tft.setTextSize(1);
    tft.println(currentPgmNumU);

    tft.setCursor(80, 19);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    tft.println("Upper");

    tft.setTextColor(ST7735_BLACK);
    tft.setFont(&Org_01);
    tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);

    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(1, 55);
    tft.setTextColor(ST7735_WHITE);
    tft.println(currentPatchNameU);

    // lower patch
    tft.setFont(&FreeSansBold18pt7b);
    tft.setCursor(5, 97);
    tft.setTextColor(ST7735_YELLOW);
    tft.setTextSize(1);
    tft.println(currentPgmNumL);

    tft.setCursor(80, 87);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    tft.println("Lower");

    tft.setTextColor(ST7735_BLACK);
    tft.setFont(&Org_01);
    tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);

    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(1, 122);
    tft.setTextColor(ST7735_WHITE);
    tft.println(currentPatchNameL);
  }
}

void renderEnv(float att, float dec, float sus, float rel) {
  if (upperSW) {
    tft.drawLine(100, 94, 100 + (att * 60), 74, ST7735_CYAN);
    tft.drawLine(100 + (att * 60), 74.0, 100 + ((att + dec) * 60), 94 - (sus / 52), ST7735_CYAN);
    tft.drawFastHLine(100 + ((att + dec) * 60), 94 - (sus / 52), 40 - ((att + dec) * 60), ST7735_CYAN);
    tft.drawLine(139, 94 - (sus / 52), 139 + (rel * 60), 94, ST7735_CYAN);
  } else {
    tft.drawLine(100, 94, 100 + (att * 60), 74, ST7735_CYAN);
    tft.drawLine(100 + (att * 60), 74.0, 100 + ((att + dec) * 60), 94 - (sus / 52), ST7735_CYAN);
    tft.drawFastHLine(100 + ((att + dec) * 60), 94 - (sus / 52), 40 - ((att + dec) * 60), ST7735_CYAN);
    tft.drawLine(139, 94 - (sus / 52), 139 + (rel * 60), 94, ST7735_CYAN);
  }
}

void renderRecallPerformancePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.println("Recall Performance:");
  tft.println();
  if (!performances.isEmpty()) {
    tft.println(performances.first().name);
  }
}

void renderSavePerformancePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.println("Save Performance:");
  tft.println();
  if (!performances.isEmpty()) {
    tft.println(performances.last().name);
  }
}

void renderDeletePerformancePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.println("Delete Performance:");
  tft.println();
  if (!performances.isEmpty()) {
    tft.println(performances.first().name);
  }
}

void renderCurrentPerformancePage() {
  tft.fillScreen(ST7735_BLACK);

  // ─── Header ─────────────────────────────────────────
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(5, 20);
  tft.setTextColor(ST7735_CYAN);
  tft.print("P: ");
  tft.setTextColor(ST7735_YELLOW);
  tft.println(performanceIndex + 1);

  // Mode label (Whole / Dual / Split) top-right
  {
    Performance perf = performances[performanceIndex];
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(80, 20);

    switch (perf.keyboardMode) {
      case 0:
        tft.print("Whole");
        break;
      case 1:
        tft.print("Dual");
        break;
      case 2:
        {
          // Display “Split” and then the mapped note name
          tft.print("Split ");

          // 1) map 0–24 → 48–60
          int midiNote = map(perf.newsplitPoint, 0, 24, 48, 60);

          // 2) look up name + octave
          int nameIndex = midiNote % 12;
          int octave = (midiNote / 12) - 1;

          // 3) draw it
          tft.print(NOTE_NAMES[nameIndex]);
          tft.print(octave);
          break;
        }
    }
  }

  tft.drawFastHLine(0, 28, tft.width(), ST7735_RED);
  tft.drawFastHLine(0, 60, tft.width(), ST7735_RED);

  if (!performances.isEmpty()) {
    Performance perf = performances[performanceIndex];

    // ─── Performance name centered ─────────────────────
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(ST7735_YELLOW);
    String nameStr = String(perf.name);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(nameStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((tft.width() - w) / 2, 52);
    tft.println(nameStr);

    // ─── Upper patch # and name ───────────────────────
    if (perf.keyboardMode != 0) {
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(2, 88);
      tft.print("U:");

      tft.setTextColor(ST7735_YELLOW);
      tft.setCursor(20, 88);
      tft.print(perf.upperPatchNo);

      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(ST7735_WHITE);
      tft.setCursor(50, 88);
      tft.println(getPatchName(perf.upperPatchNo));
    }
    // ─── Lower patch # and name ───────────────────────

    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(2, 116);
    tft.print("L:");

    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(20, 116);
    tft.print(perf.lowerPatchNo);

    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST7735_WHITE);
    tft.setCursor(50, 116);
    tft.println(getPatchName(perf.lowerPatchNo));

  } else {
    tft.setFont(&FreeSans12pt7b);
    tft.setTextSize(1);
    tft.setCursor(5, 65);
    tft.setTextColor(ST7735_WHITE);
    tft.println("No Performances Saved");
  }

  tft.updateScreen();
}

void showCurrentPage(const String &text) {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.println(text);
  tft.updateScreen();
}

void renderCurrentParameterPage() {
  switch (state) {
    case PARAMETER:
      if (upperSW) {
        tft.fillScreen(ST7735_BLACK);
        tft.setFont(&FreeSans12pt7b);
        tft.setCursor(0, 29);
        tft.setTextColor(ST7735_YELLOW);
        tft.setTextSize(1);
        tft.println(currentParameter);
        tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);
        tft.setCursor(1, 55);
        tft.setTextColor(ST7735_WHITE);
        tft.println(currentValue);
        // lower patch
        tft.setFont(&FreeSansBold18pt7b);
        tft.setCursor(5, 97);
        tft.setTextColor(ST7735_YELLOW);
        tft.setTextSize(1);
        tft.println(currentPgmNumL);
        tft.setCursor(80, 87);
        tft.setFont(&FreeSans12pt7b);
        tft.setTextSize(1);
        tft.println("Lower");
        tft.setFont(&FreeSans12pt7b);
        tft.setTextColor(ST7735_YELLOW);
        tft.setCursor(1, 122);
        tft.setTextColor(ST7735_WHITE);
        tft.println(currentPatchNameL);
        switch (paramType) {
          case FILTER_ENV:
            renderEnv(upperData[P_filterAttack] * 0.0001, upperData[P_filterDecay] * 0.0001, upperData[P_filterSustain], upperData[P_filterRelease] * 0.0001);
            break;
          case AMP_ENV:
            renderEnv(upperData[P_ampAttack] * 0.0001, upperData[P_ampDecay] * 0.0001, upperData[P_ampSustain], upperData[P_ampRelease] * 0.0001);
            break;
        }
      } else {
        if (wholemode) {
          //lower whole mode patch lower ection
          tft.fillScreen(ST7735_BLACK);
          tft.setFont(&FreeSansBold18pt7b);
          tft.setCursor(5, 97);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentPgmNumL);

          tft.setCursor(80, 87);
          tft.setFont(&FreeSans12pt7b);
          tft.setTextSize(1);
          tft.println("Whole");

          tft.setTextColor(ST7735_BLACK);
          tft.setFont(&Org_01);
          tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);

          tft.setFont(&FreeSans12pt7b);
          tft.setTextColor(ST7735_YELLOW);
          tft.setCursor(1, 122);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentPatchNameL);

          // parameter in upper section
          tft.setFont(&FreeSans12pt7b);
          tft.setCursor(0, 29);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentParameter);
          tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);
          tft.setCursor(1, 55);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentValue);

        } else {
          tft.fillScreen(ST7735_BLACK);
          tft.setFont(&FreeSans12pt7b);
          tft.setCursor(0, 97);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentParameter);
          tft.drawFastHLine(0, 63, tft.width(), ST7735_RED);
          tft.setCursor(1, 122);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentValue);
          // upper patch
          tft.setFont(&FreeSansBold18pt7b);
          tft.setCursor(5, 29);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentPgmNumU);
          tft.setCursor(80, 19);
          tft.setFont(&FreeSans12pt7b);
          tft.setTextSize(1);
          tft.println("Upper");
          tft.setFont(&FreeSans12pt7b);
          tft.setTextColor(ST7735_YELLOW);
          tft.setCursor(1, 55);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentPatchNameU);
          switch (paramType) {
            case FILTER_ENV:
              renderEnv(lowerData[P_filterAttack] * 0.0001, lowerData[P_filterDecay] * 0.0001, lowerData[P_filterSustain], lowerData[P_filterRelease] * 0.0001);
              break;
            case AMP_ENV:
              renderEnv(lowerData[P_ampAttack] * 0.0001, lowerData[P_ampDecay] * 0.0001, lowerData[P_ampSustain], lowerData[P_ampRelease] * 0.0001);
              break;
          }
        }
      }
      break;
  }
}

void renderDeletePatchPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Delete?");
  tft.drawFastHLine(0, 60, tft.width(), ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
  tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);
}

void renderDeleteMessagePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(2, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Renumbering");
  tft.setCursor(10, 90);
  tft.println("SD Card");
}

void renderSavePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Save?");
  tft.drawFastHLine(0, 60, tft.width(), ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches[patches.size() - 2].patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches[patches.size() - 2].patchName);
  tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
}

void renderReinitialisePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(5, 53);
  tft.println("Initialise to");
  tft.setCursor(5, 90);
  tft.println("panel setting");
}

void renderPatchNamingPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 53);
  tft.println("Rename Patch");
  tft.drawFastHLine(0, 62, tft.width(), ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 90);
  tft.println(newPatchName);
}

void renderRecallPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 45);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 45);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);

  tft.fillRect(0, 56, tft.width(), 23, 0xA000);
  tft.setCursor(0, 72);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 72);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);

  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  patches.size() > 1 ? tft.println(patches[1].patchNo) : tft.println(patches.last().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  patches.size() > 1 ? tft.println(patches[1].patchName) : tft.println(patches.last().patchName);
}

void showRenamingPage(String newName) {
  newPatchName = newName;
}

void renderUpDown(uint16_t x, uint16_t y, uint16_t colour) {
  //Produces up/down indicator glyph at x,y
  tft.setCursor(x, y);
  tft.fillTriangle(x, y, x + 8, y - 8, x + 16, y, colour);
  tft.fillTriangle(x, y + 4, x + 8, y + 12, x + 16, y + 4, colour);
}


void renderSettingsPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 53);
  tft.println(currentSettingsOption);
  if (currentSettingsPart == SETTINGS) renderUpDown(140, 42, ST7735_YELLOW);
  tft.drawFastHLine(0, 62, tft.width(), ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 90);
  tft.println(currentSettingsValue);
  if (currentSettingsPart == SETTINGSVALUE) renderUpDown(140, 80, ST7735_WHITE);
}

void showCurrentParameterPage(const char *param, float val, int pType) {
  currentParameter = param;
  currentValue = String(val);
  currentFloatValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val, int pType) {
  if (state == SETTINGS || state == SETTINGSVALUE) state = PARAMETER;  //Exit settings page if showing
  currentParameter = param;
  currentValue = val;
  paramType = pType;
  startTimer();
}

void showCurrentParameterPage(const char *param, String val) {
  showCurrentParameterPage(param, val, PARAMETER);
}

void showPatchPage(String numberU, String patchNameU, String numberL, String patchNameL) {
  currentPgmNumU = numberU;
  currentPatchNameU = patchNameU;
  currentPgmNumL = numberL;
  currentPatchNameL = patchNameL;
}

void showSettingsPage(const char *option, const char *value, int settingsPart) {
  currentSettingsOption = option;
  currentSettingsValue = value;
  currentSettingsPart = settingsPart;
}

void drawModeIndicator() {
  tft.setTextSize(1);                            // Smallest text size
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);  // White text on black background (overwrites cleanly)

  tft.setCursor(128, 0);  // Near top-right, fits within 160 pixels wide
  if (inPerformanceMode) {
    tft.print("PERF");
  } else {
    tft.print("PATCH");
  }
}

void displayThread() {
  threads.delay(2000);  //Give bootup page chance to display
  while (1) {
    switch (state) {
      case PARAMETER:
        if (inPerformanceMode) {
          renderCurrentPerformancePage();
        } else {
          if ((millis() - timer) > DISPLAYTIMEOUT) {
            renderCurrentPatchPage();
          } else {
            renderCurrentParameterPage();
          }
        }
        break;

      case RECALL:
        if (inPerformanceMode) {
          renderRecallPerformancePage();
        } else {
          renderRecallPage();
        }
        break;

      case SAVE:
        renderSavePage();
        break;

      case SAVE_PERFORMANCE:
        renderSavePerformancePage();
        break;

      case PERFORMANCE:
        renderCurrentPerformancePage();
        break;

      case REINITIALISE:
        renderReinitialisePage();
        tft.updateScreen();
        threads.delay(1000);
        state = PARAMETER;
        break;

      case PATCHNAMING:
        renderPatchNamingPage();
        break;

      case PATCH:
        renderCurrentPatchPage();
        break;

      case DELETE:
        renderDeletePatchPage();
        break;

      case DELETE_PERFORMANCE:
        renderDeletePerformancePage();
        break;

      case DELETEMSG:
        renderDeleteMessagePage();
        break;

      case SETTINGS:
      case SETTINGSVALUE:
        renderSettingsPage();
        break;
    }
    tft.updateScreen();
  }
}

void setupDisplay() {
  tft.useFrameBuffer(true);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.invertDisplay(false);
  renderBootUpPage();
  tft.updateScreen();
  threads.addThread(displayThread);
}
