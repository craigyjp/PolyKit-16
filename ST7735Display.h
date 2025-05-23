

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

extern Performance currentPerformance;
extern CircularBuffer<Performance, PERFORMANCE_LIMIT> performances;

String currentPerfNum = "";
String currentPerfName = "";
int currentUpperPatchNo = 0;
String currentUpperPatchName = "";
int currentLowerPatchNo = 0;
String currentLowerPatchName = "";

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

const char *noteNames[] = {
  "C", "C#", "D", "D#", "E", "F",
  "F#", "G", "G#", "A", "A#", "B"
};

void startTimer() {
  if (state == PARAMETER) {
    timer = millis();
  }
}

String getNoteName(byte midiNote) {
  byte note = midiNote % 12;
  int octave = (midiNote / 12) - 1;  // MIDI octave 0 starts at C-1
  return String(noteNames[note]) + String(octave);
}

String getTransposeString(byte splitTrans) {
  int oct = (int)splitTrans - 2;  // center = 0
  if (oct >= 0) return "+" + String(oct);
  else return String(oct);
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

void renderCurrentPatchPage() {
  tft.fillScreen(ST7735_BLACK);
  label = "Patch Mode";
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;

  // Measure text width/height
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

  // Calculate centered position
  int textX = (160 - w) / 2;
  int textY = 15;  // vertical alignment baseline
  int boxHeight = 20;
  int boxY = textY - boxHeight + 5;  // adjust upward so text baseline fits well

  // Draw yellow rectangle
  tft.fillRect(0, boxY, 160, boxHeight, ST7735_YELLOW);

  // Draw text in black, centered
  tft.setCursor(textX, textY);
  tft.setTextColor(ST7735_BLACK);
  tft.setTextSize(1);
  tft.println(label);

  // ─── Patch Display ─────────────────────────
  if (!wholemode) {
    // Upper patch block
    tft.setFont(&FreeSansBold12pt7b);
    tft.setCursor(5, 45);
    tft.setTextColor(ST7735_YELLOW);
    tft.setTextSize(1);
    tft.println(currentPgmNumU);
    tft.setCursor(50, 43);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.println("Upper");
    tft.setCursor(1, 65);
    tft.setTextColor(ST7735_WHITE);
    tft.println(currentPatchNameU);
  }

  // Lower patch block (always shown)
  //lower whole mode patch lower ection
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(5, 99);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(currentPgmNumL);

  if (wholemode) {
    tft.setCursor(50, 97);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.println("Whole");
  } else {
    tft.setCursor(50, 97);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.println("Lower");
  }

  if (splitmode) {

    tft.setCursor(110, 43);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_WHITE);
    tft.print("S: ");
    tft.setCursor(130, 43);
    tft.setTextColor(ST7735_YELLOW);
    String splitNote = getNoteName(newsplitPoint + 36);
    tft.print(splitNote);

    tft.setCursor(110, 97);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_WHITE);
    tft.print("X: ");
    tft.setCursor(130, 97);
    tft.setTextColor(ST7735_YELLOW);
    String transStr = getTransposeString(splitTrans);
    tft.print(transStr);
  }

  tft.drawFastHLine(0, 76, tft.width(), ST7735_RED);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(1, 118);
  tft.setTextColor(ST7735_WHITE);
  tft.println(currentPatchNameL);
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

void renderPerformancePage() {
  tft.fillScreen(ST7735_BLACK);
  label = "Perf Mode";
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;

  // Measure text width/height
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

  // Calculate centered position
  int textX = (160 - w) / 2;
  int textY = 15;  // vertical alignment baseline
  int boxHeight = 20;
  int boxY = textY - boxHeight + 5;  // adjust upward so text baseline fits well

  // Draw yellow rectangle
  tft.fillRect(0, boxY, 160, boxHeight, ST7735_RED);

  // Draw text in black, centered
  tft.setCursor(textX, textY);
  tft.setTextColor(ST7735_BLACK);
  tft.setTextSize(1);
  tft.println(label);

  tft.setTextColor(ST7735_YELLOW);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(1, 42);
  tft.println("P:");

  tft.setCursor(25, 42);
  tft.setTextSize(1);
  tft.println(currentPerfNum);

  tft.setCursor(60, 40);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.println(currentPerfName);

  if (currentPerformance.mode != WHOLE) {
    tft.setCursor(1, 98);
    tft.setTextSize(1);
    tft.setTextColor(ST7735_YELLOW);
    tft.println("U:");

    tft.setCursor(25, 98);
    tft.setTextColor(ST7735_WHITE);
    tft.println(String(currentUpperPatchNo) + " " + currentUpperPatchName);
  }

  tft.setCursor(1, 118);
  tft.setTextColor(ST7735_YELLOW);
  tft.println("L:");

  tft.setCursor(25, 118);
  tft.setTextColor(ST7735_WHITE);
  tft.println(String(currentLowerPatchNo) + " " + currentLowerPatchName);

  tft.setCursor(1, 62);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ST7735_YELLOW);

  String modeLabel;
  switch (currentPerformance.mode) {
    case WHOLE:
      modeLabel = "Mode Whole";
      tft.println(modeLabel);
      break;
    case DUAL:
      modeLabel = "Mode Dual";
      tft.println(modeLabel);
      break;
    case SPLIT:
      {
        String splitNote = getNoteName(currentPerformance.newsplitPoint + 36);
        String transStr = getTransposeString(currentPerformance.splitTrans);

        modeLabel = "Split Mode: " + splitNote;
        tft.setCursor(1, 60);
        tft.print(modeLabel);

        modeLabel = "Low Xpose: " + transStr;
        tft.setCursor(1, 80);
        tft.print(modeLabel);
        break;
      }
    default:
      modeLabel = "Mode ?";
      break;
  }
}

void renderPerformanceDeletePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(10, 20);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Delete Perf?");
  tft.drawFastHLine(10, 50, tft.width() - 20, ST7735_RED);

  tft.setTextSize(2);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(10, 80);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(performances.last().performanceNo);
  tft.setCursor(100, 80);
  tft.setTextColor(ST7735_WHITE);
  tft.println(performances.last().name);

  tft.fillRect(10, 120, tft.width() - 20, 44, ST77XX_RED);

  tft.setCursor(10, 130);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(performances.first().performanceNo);
  tft.setCursor(100, 130);
  tft.setTextColor(ST7735_WHITE);
  tft.println(performances.first().name);

  tft.setCursor(10, 180);
  tft.setTextColor(ST7735_YELLOW);
  performances.size() > 1 ? tft.println(performances[1].performanceNo) : tft.println(performances.last().performanceNo);
  tft.setCursor(100, 180);
  tft.setTextColor(ST7735_WHITE);
  performances.size() > 1 ? tft.println(performances[1].name) : tft.println(performances.last().name);
}

void renderCurrentParameterPage() {
  tft.fillScreen(ST7735_BLACK);
  label = "Patch Mode";
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;

  // Measure text width/height
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

  // Calculate centered position
  int textX = (160 - w) / 2;
  int textY = 15;  // vertical alignment baseline
  int boxHeight = 20;
  int boxY = textY - boxHeight + 5;  // adjust upward so text baseline fits well

  // Draw yellow rectangle
  tft.fillRect(0, boxY, 160, boxHeight, ST7735_YELLOW);

  // Draw text in black, centered
  tft.setCursor(textX, textY);
  tft.setTextColor(ST7735_BLACK);
  tft.setTextSize(1);
  tft.println(label);

  switch (state) {
    case PARAMETER:
      if (upperSW) {

        tft.setFont(&FreeSans12pt7b);
        tft.setCursor(0, 99);
        tft.setTextColor(ST7735_YELLOW);
        tft.setTextSize(1);
        tft.println(currentParameter);
        tft.drawFastHLine(0, 76, tft.width(), ST7735_RED);
        tft.setFont(&FreeSans9pt7b);
        tft.setCursor(1, 122);
        tft.setTextColor(ST7735_WHITE);
        tft.println(currentValue);
        // upper patch
        tft.setFont(&FreeSansBold12pt7b);
        tft.setCursor(5, 45);
        tft.setTextColor(ST7735_YELLOW);
        tft.setTextSize(1);
        tft.println(currentPgmNumU);
        tft.setCursor(50, 43);
        tft.setFont(&FreeSans9pt7b);
        tft.setTextSize(1);
        tft.println("Upper");
        tft.setCursor(1, 65);
        tft.setTextColor(ST7735_WHITE);
        tft.println(currentPatchNameU);
        switch (paramType) {
          case FILTER_ENV:
            renderEnv(upperData[P_filterAttack] * 0.0001, upperData[P_filterDecay] * 0.0001, upperData[P_filterSustain], upperData[P_filterRelease] * 0.0001);
            break;
          case AMP_ENV:
            renderEnv(upperData[P_ampAttack] * 0.0001, upperData[P_ampDecay] * 0.0001, upperData[P_ampSustain], upperData[P_ampRelease] * 0.0001);
            break;
        }

        if (splitmode) {

          tft.setCursor(110, 43);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextSize(1);
          tft.setTextColor(ST7735_WHITE);
          tft.print("S: ");
          tft.setCursor(130, 43);
          tft.setTextColor(ST7735_YELLOW);
          String splitNote = getNoteName(newsplitPoint + 36);
          tft.print(splitNote);

        }

      } else {
        if (wholemode) {
          //lower whole mode patch lower ection
          tft.fillScreen(ST7735_BLACK);
          tft.fillRect(0, boxY, 160, boxHeight, ST7735_YELLOW);
          // Draw text in black, centered
          tft.setCursor(textX, textY);
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(1);
          tft.println(label);

          tft.setFont(&FreeSansBold12pt7b);
          tft.setCursor(5, 99);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentPgmNumL);

          tft.setCursor(50, 97);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextSize(1);
          tft.println("Whole");

          tft.setTextColor(ST7735_BLACK);
          tft.setFont(&Org_01);
          tft.drawFastHLine(0, 76, tft.width(), ST7735_RED);

          tft.setFont(&FreeSans9pt7b);
          tft.setCursor(1, 118);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentPatchNameL);

          // parameter in upper section
          tft.setFont(&FreeSans12pt7b);
          tft.setCursor(5, 45);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentParameter);
          tft.drawFastHLine(0, 76, tft.width(), ST7735_RED);
          tft.setFont(&FreeSans9pt7b);
          tft.setCursor(1, 65);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentValue);

        } else {
          tft.fillScreen(ST7735_BLACK);
          tft.fillRect(0, boxY, 160, boxHeight, ST7735_YELLOW);
          // Draw text in black, centered
          tft.setCursor(textX, textY);
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(1);
          tft.println(label);

          tft.setFont(&FreeSans12pt7b);
          tft.setCursor(5, 45);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentParameter);
          tft.drawFastHLine(0, 76, tft.width(), ST7735_RED);
          tft.setCursor(1, 65);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentValue);
          // upper patch
          tft.setFont(&FreeSansBold12pt7b);
          tft.setCursor(5, 99);
          tft.setTextColor(ST7735_YELLOW);
          tft.setTextSize(1);
          tft.println(currentPgmNumL);
          tft.setCursor(50, 97);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextSize(1);
          tft.println("Lower");
          tft.setCursor(1, 118);
          tft.setTextColor(ST7735_WHITE);
          tft.println(currentPatchNameL);
          switch (paramType) {
            case FILTER_ENV:
              renderEnv(lowerData[P_filterAttack] * 0.0001, lowerData[P_filterDecay] * 0.0001, lowerData[P_filterSustain], lowerData[P_filterRelease] * 0.0001);
              break;
            case AMP_ENV:
              renderEnv(lowerData[P_ampAttack] * 0.0001, lowerData[P_ampDecay] * 0.0001, lowerData[P_ampSustain], lowerData[P_ampRelease] * 0.0001);
              break;
          }
          if (splitmode) {

            tft.setCursor(110, 97);
            tft.setFont(&FreeSans9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(ST7735_WHITE);
            tft.print("X: ");
            tft.setCursor(130, 97);
            tft.setTextColor(ST7735_YELLOW);
            String transStr = getTransposeString(splitTrans);
            tft.print(transStr);
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

void renderPerformanceNamingPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(0, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println("Rename Perf");
  tft.drawFastHLine(0, 62, tft.width(), ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 90);
  tft.println(newPatchName);  // reused for performance too
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

void displayThread() {
  threads.delay(2000);  //Give bootup page chance to display
  while (1) {
    switch (state) {
      case PARAMETER:
        if ((millis() - timer) > DISPLAYTIMEOUT) {
          renderCurrentPatchPage();
        } else {
          renderCurrentParameterPage();
        }
        break;
      case RECALL:
        renderRecallPage();
        break;
      case SAVE:
        renderSavePage();
        break;
      case REINITIALISE:
        renderReinitialisePage();
        tft.updateScreen();  //update before delay
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
      case DELETEMSG:
        renderDeleteMessagePage();
        break;
      case SETTINGS:
      case SETTINGSVALUE:
        renderSettingsPage();
        break;
      case PERFORMANCE_RECALL:
      case PERFORMANCE_EDIT:
      case PERFORMANCE_SAVE:
        renderPerformancePage();
        break;
      case PERFORMANCE_NAMING:
        renderPerformanceNamingPage();  // see below
        break;
      case PERFORMANCE_DELETE:
        renderPerformanceDeletePage();
        break;
      case PERFORMANCE_DELETEMSG:
        // Handled inside checkSwitches() (already shows a message & delay)
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
