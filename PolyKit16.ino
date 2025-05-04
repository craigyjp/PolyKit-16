/*
  PolyKit 16 MUX - Firmware Rev 1.9

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html

  Arduino IDE
  Tools Settings:
  Board: "Teensy4.1"
  USB Type: "Serial + MIDI"
  CPU Speed: "600"
  Optimize: "Fastest"

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
    Replacement files are in the Modified Libraries folder and need to be placed in the teensy Audio folder.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include "MidiCC.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include "Settings.h"
#include <cstring>
#include <RoxMux.h>
#include <map>  // Include the map library

String getPatchName(uint16_t patchNo);

#define PARAMETER 0
#define RECALL 1
#define SAVE 2
#define REINITIALISE 3
#define PATCH 4
#define PATCHNAMING 5
#define DELETE 6
#define DELETEMSG 7
#define SETTINGS 8
#define SETTINGSVALUE 9
#define PERFORMANCE_RECALL 10
#define PERFORMANCE_SAVE 11
#define PERFORMANCE_EDIT 12
#define PERFORMANCE_NAMING 13
#define PERFORMANCE_DELETE 14
#define PERFORMANCE_DELETEMSG 15


unsigned int state = PARAMETER;

uint32_t int_ref_on_flexible_mode = 0b00001001000010100000000000000000;  // { 0000 , 1001 , 0000 , 1010000000000000 , 0000 }

uint32_t sample_data1 = 0b00000000000000000000000000000000;
uint32_t sample_data2 = 0b00000000000000000000000000000000;
uint32_t sample_data3 = 0b00000000000000000000000000000000;
uint32_t sample_data4 = 0b00000000000000000000000000000000;

uint32_t channel_a = 0b00000010000000000000000000000000;
uint32_t channel_b = 0b00000010000100000000000000000000;
uint32_t channel_c = 0b00000010001000000000000000000000;
uint32_t channel_d = 0b00000010001100000000000000000000;
uint32_t channel_e = 0b00000010010000000000000000000000;
uint32_t channel_f = 0b00000010010100000000000000000000;
uint32_t channel_g = 0b00000010011000000000000000000000;
uint32_t channel_h = 0b00000010011100000000000000000000;

enum PlayMode {
  WHOLE = 0,
  DUAL = 1,
  SPLIT = 2
};

struct Performance {
  int performanceNo;
  int upperPatchNo;
  int lowerPatchNo;
  String name;
  PlayMode mode;       // ← Back to enum type!
  byte newsplitPoint;  // 👈 add this line
  byte splitTrans;     // 👈 add this line
};

CircularBuffer<Performance, PERFORMANCE_LIMIT> performances;
Performance currentPerformance;

#include "ST7735Display.h"

boolean cardStatus = false;

struct VoiceAndNote {
  int note;
  int velocity;
  long timeOn;
};

struct VoiceAndNote voices[NO_OF_VOICES] = {
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 },
  { -1, -1, 0 }
};

boolean voiceOn[NO_OF_VOICES] = { false, false, false, false, false, false, false, false };

int prevNote = 0;  //Initialised to middle value
bool notes[88] = { 0 }, initial_loop = 1;
int8_t noteOrder[40] = { 0 }, orderIndx = { 0 };

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);


//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);   //RX - Pin 0
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI2);  //TX - Pin 8
MIDI_CREATE_INSTANCE(HardwareSerial, Serial5, MIDI5);  //TX - Pin 24

int count = 0;  //For MIDI Clk Sync
int DelayForSH3 = 50;
int midioutfrig = 8;

int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now
unsigned long buttonDebounce = 0;

#define OCTO_TOTAL 2
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;

#define SRP_TOTAL 6
Rox74HC595<SRP_TOTAL> srp;

// pins for 74HC595
#define SRP_DATA 18   // pin 14 on 74HC595 (DATA)
#define SRP_CLK 19    // pin 11 on 74HC595 (CLK)
#define SRP_LATCH 41  // pin 12 on 74HC595 (LATCH)
#define SRP_PWM -1    // pin 13 on 74HC595

#define SR_TOTAL 3
Rox74HC595<SR_TOTAL> sr;

// pins for 74HC595
#define SR_DATA 37   // pin 14 on 74HC595 (DATA)
#define SR_CLK 48    // pin 11 on 74HC595 (CLK)
#define SR_LATCH 52  // pin 12 on 74HC595 (LATCH)
#define SR_PWM -1    // pin 13 on 74HC595

// pins for 74HC165
#define PIN_DATA 50  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 49  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 51   // pin 2 on 74HC165 (CLK))

void setup() {

  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);
  octoswitch.setIgnoreAfterHold(UPPER_SW, true);
  octoswitch.setIgnoreAfterHold(WHOLE_SW, true);

  srp.begin(SRP_DATA, SRP_LATCH, SRP_CLK, SRP_PWM);
  sr.begin(SR_DATA, SR_LATCH, SR_CLK, SR_PWM);

  delay(100);

  setupDisplay();
  setUpSettings();
  setupHardware();

  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_CS1, LOW);
  delayMicroseconds(1);
  SPI.transfer32(int_ref_on_flexible_mode);
  digitalWrite(DAC_CS1, HIGH);
  SPI.endTransaction();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus) {
    //Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0) {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
    loadPerformances();
  } else {
    //Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable", "", "");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  //Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myConvertControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleAfterTouchChannel(myAfterTouch);
  usbMIDI.setHandlePitchChange(DinHandlePitchBend);
  usbMIDI.setHandleNoteOn(DinHandleNoteOn);
  usbMIDI.setHandleNoteOff(DinHandleNoteOff);
  //Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myConvertControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  MIDI.setHandlePitchBend(DinHandlePitchBend);
  MIDI.setHandleNoteOn(DinHandleNoteOn);
  MIDI.setHandleNoteOff(DinHandleNoteOff);
  MIDI.turnThruOn(midi::Thru::Mode::Off);
  //Serial.println("MIDI In DIN Listening");

  MIDI2.begin();
  MIDI2.turnThruOn(midi::Thru::Mode::Off);
  MIDI5.begin();
  MIDI5.turnThruOn(midi::Thru::Mode::Off);

  //Read Aftertouch from EEPROM, this can be set individually by each patch.
  upperData[P_AfterTouchDest] = getAfterTouchU();
  oldAfterTouchDestU = upperData[P_AfterTouchDest];
  lowerData[P_AfterTouchDest] = getAfterTouchL();
  oldAfterTouchDestL = lowerData[P_AfterTouchDest];

  newsplitPoint = getSplitPoint();

  splitTrans = getSplitTrans();
  setTranspose(splitTrans);

  //Read Pitch Bend Range from EEPROM
  pitchBendRange = getPitchBendRange();

  //Read Mod Wheel Depth from EEPROM
  modWheelDepth = getModWheelDepth();

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  lowerData[P_monoMulti] = getMonoMultiL();
  oldmonoMultiL = lowerData[P_monoMulti];
  upperData[P_monoMulti] = getMonoMultiU();
  oldmonoMultiU = upperData[P_monoMulti];
  upperData[P_filterLogLin] = getFilterEnvU();
  oldfilterLogLinU = upperData[P_filterLogLin];
  lowerData[P_filterLogLin] = getFilterEnvL();
  oldfilterLogLinL = lowerData[P_filterLogLin];
  upperData[P_ampLogLin] = getAmpEnvU();
  oldampLogLinU = upperData[P_ampLogLin];
  lowerData[P_ampLogLin] = getAmpEnvL();
  oldampLogLinL = lowerData[P_ampLogLin];
  upperData[P_keyTrackSW] = getKeyTrackU();
  oldkeyTrackSWU = upperData[P_keyTrackSW];
  upperData[P_keyTrackSW] = getKeyTrackL();
  oldkeyTrackSWL = upperData[P_keyTrackSW];

  for (int i = 0; i < 4; i++) {
    int noteon = 60;
    MIDI5.sendNoteOn(noteon, 64, 1);
    delayMicroseconds(DelayForSH3);
    MIDI2.sendNoteOn(noteon, 64, 1);
    delay(1);
    MIDI5.sendNoteOff(noteon, 64, 1);
    delayMicroseconds(DelayForSH3);
    MIDI2.sendNoteOff(noteon, 64, 1);
    noteon++;
  }

  upperSW = 0;
  wholemode = true;
  recallPatch(patchNoL);  //Load first patch
  updateplayMode(0);
}

void refreshPatchDisplayFromState() {
  showPatchPage(
    currentPgmNumU,
    currentPatchNameU,
    currentPgmNumL,
    currentPatchNameL);
}

void savePerformance(const char *fileName, const Performance &perf) {
  String path = "/perf/" + String(fileName);

  if (SD.exists(path.c_str())) {
    SD.remove(path.c_str());
  }

  File file = SD.open(path.c_str(), FILE_WRITE);
  if (file) {
    file.print(perf.performanceNo);  // 👈 Save perf number first
    file.print(",");
    file.print(perf.upperPatchNo);
    file.print(",");
    file.print(perf.lowerPatchNo);
    file.print(",");
    file.print(perf.name);
    file.print(",");
    file.print((int)perf.mode);  // Save playMode as an integer (0, 1, 2)
    file.print(",");
    file.print(perf.newsplitPoint);
    file.print(",");
    file.println(perf.splitTrans);
    file.close();
  } else {
    Serial.print("Failed to save performance: ");
    Serial.println(path);
  }
}

void recallPerformance(const Performance &perf) {
  currentPerformance = perf;
  playMode = perf.mode;

  switch (playMode) {
    case WHOLE:
      recallPatch(perf.lowerPatchNo);
      patchNo = perf.lowerPatchNo;
      refreshPatchDisplayFromState();
      break;
    case DUAL:
    case SPLIT:
      recallPatch(perf.upperPatchNo);
      recallPatch(perf.lowerPatchNo);
      patchNo = perf.lowerPatchNo;
      refreshPatchDisplayFromState();
      break;
  }
}

void deletePerformance(int perfNo) {
  char filename[32];
  snprintf(filename, sizeof(filename), "/performances/perf%03d", perfNo);
  if (SD.exists(filename)) {
    SD.remove(filename);
    Serial.print("[DELETE] Removed performance: ");
    Serial.println(filename);
  }
}

String getModeName(PlayMode mode) {
  switch (mode) {
    case WHOLE: return "Whole";
    case DUAL: return "Dual";
    case SPLIT: return "Split";
    default: return "-";
  }
}

void loadPerformances() {
  performances.clear();
  File dir = SD.open("/perf");

  if (!dir || !dir.isDirectory()) {
    Serial.println("/perf not found or is not a directory");
    return;
  }

  while (true) {
    File file = dir.openNextFile();
    if (!file) break;

    if (file.isDirectory()) {
      file.close();
      continue;
    }

    String dataLine = file.readStringUntil('\n');
    file.close();

    if (dataLine.length() > 0) {
      int comma0 = dataLine.indexOf(',');
      int comma1 = dataLine.indexOf(',', comma0 + 1);
      int comma2 = dataLine.indexOf(',', comma1 + 1);
      int comma3 = dataLine.indexOf(',', comma2 + 1);
      int comma4 = dataLine.indexOf(',', comma3 + 1);
      int comma5 = dataLine.indexOf(',', comma4 + 1);

      if (comma0 == -1 || comma1 == -1 || comma2 == -1 || comma3 == -1 || comma4 == -1 || comma5 == -1) continue;

      int perfNo = dataLine.substring(0, comma0).toInt();
      int upper = dataLine.substring(comma0 + 1, comma1).toInt();
      int lower = dataLine.substring(comma1 + 1, comma2).toInt();
      String name = dataLine.substring(comma2 + 1, comma3);
      int mode = dataLine.substring(comma3 + 1, comma4).toInt();
      int newsplitPoint = dataLine.substring(comma4 + 1, comma5).toInt();
      int splitTrans = dataLine.substring(comma5 + 1).toInt();

      performances.push({ perfNo, upper, lower, name, (PlayMode)mode, (byte)newsplitPoint, (byte)splitTrans });
    }
  }



  // ✅ Only run once after loop
  if (performances.size() == 0) {
    Performance defaultPerf = { 1, 1, 1, "Default", WHOLE, 60, 2 };
    savePerformance("perf001", defaultPerf);
    loadPerformances();  // try again
  }
}

// ✅ This must be outside `loadPerformances()`
String getPatchName(uint16_t patchNo) {
  for (uint8_t i = 0; i < patches.size(); i++) {
    auto p = patches[i];
    if (p.patchNo == patchNo) {
      return p.patchName;
    }
  }
  return String("Unknown");
}

void renumberPerformancesOnSD() {
  File perfFolder = SD.open("/perf");
  if (!perfFolder) {
    return;  // No perf folder
  }

  // Temporary list to hold file names
  String filenames[PERFORMANCE_LIMIT];
  int count = 0;

  // Read all .perf files into filenames array
  while (true) {
    File entry = perfFolder.openNextFile();
    if (!entry) {
      break;
    }
    String name = entry.name();
    if (name.endsWith(".perf")) {
      filenames[count++] = name;
    }
    entry.close();
  }
  perfFolder.close();

  // Now renumber them sequentially
  for (int i = 0; i < count; i++) {
    String oldName = filenames[i];
    char newName[32];
    sprintf(newName, "/perf/%d.perf", i + 1);  // 001.perf, 002.perf, etc.

    // If the name is already correct, skip
    if (oldName != String(newName)) {
      // First rename to a temporary name to avoid conflicts
      String tempName = String("/perf/temp_") + String(i + 1) + ".perf";
      SD.rename(oldName.c_str(), tempName.c_str());
    }
  }
  // Second pass to rename temp names to final names
  for (int i = 0; i < count; i++) {
    String tempName = String("/perf/temp_") + String(i + 1) + ".perf";
    char newName[32];
    sprintf(newName, "/perf/%d.perf", i + 1);

    if (SD.exists(tempName.c_str())) {
      SD.rename(tempName.c_str(), newName);
    }
  }
}

void setKeyboardMode(uint8_t mode) {
  wholemode = (mode == 0);
  dualmode = (mode == 1);
  splitmode = (mode == 2);
}

void setSplitPoint(uint8_t note) {
  newsplitPoint = note;
}

void setTranspose(int splitTrans) {
  switch (splitTrans) {
    case 0:
      lowerTranspose = -24;
      oldsplitTrans = splitTrans;
      break;

    case 1:
      lowerTranspose = -12;
      oldsplitTrans = splitTrans;
      break;

    case 2:
      lowerTranspose = 0;
      oldsplitTrans = splitTrans;
      break;

    case 3:
      lowerTranspose = 12;
      oldsplitTrans = splitTrans;
      break;

    case 4:
      lowerTranspose = 24;
      oldsplitTrans = splitTrans;
      break;
  }
}

void LFODelayHandle() {
  // LFO Delay code
  getDelayTime();

  unsigned long currentMillisU = millis();
  if (upperData[P_monoMulti] && !upperData[P_LFODelayGo]) {
    if (oldnumberOfNotesU < numberOfNotesU) {
      previousMillisU = currentMillisU;
      oldnumberOfNotesU = numberOfNotesU;
    }
  }
  if (numberOfNotesU > 0) {
    if (currentMillisU - previousMillisU >= intervalU) {
      upperData[P_LFODelayGo] = 1;
    } else {
      upperData[P_LFODelayGo] = 0;
    }
  } else {
    upperData[P_LFODelayGo] = 1;
    previousMillisU = currentMillisU;  //reset timer so its ready for the next time
  }

  unsigned long currentMillisL = millis();
  if (lowerData[P_monoMulti] && !lowerData[P_LFODelayGo]) {
    if (oldnumberOfNotesL < numberOfNotesL) {
      previousMillisL = currentMillisL;
      oldnumberOfNotesL = numberOfNotesL;
    }
  }
  if (numberOfNotesL > 0) {
    if (currentMillisL - previousMillisL >= intervalL) {
      lowerData[P_LFODelayGo] = 1;
    } else {
      lowerData[P_LFODelayGo] = 0;
    }
  } else {
    lowerData[P_LFODelayGo] = 1;
    previousMillisL = currentMillisL;  //reset timer so its ready for the next time
  }
}

void DinHandleNoteOn(byte channel, byte note, byte velocity) {
  numberOfNotesU = numberOfNotesU + 1;
  numberOfNotesL = numberOfNotesL + 1;

  if (wholemode) {
    if (note < 0 || note > 127) return;
    switch (getVoiceNo(-1)) {
      case 1:
        voices[0].note = note;
        voices[0].velocity = velocity;
        voices[0].timeOn = millis();
        MIDI2.sendNoteOn(note, velocity, 1);
        voiceOn[0] = true;
        break;
      case 2:
        voices[1].note = note;
        voices[1].velocity = velocity;
        voices[1].timeOn = millis();
        MIDI5.sendNoteOn(note, velocity, 1);
        voiceOn[1] = true;
        break;
      case 3:
        voices[2].note = note;
        voices[2].velocity = velocity;
        voices[2].timeOn = millis();
        MIDI2.sendNoteOn(note, velocity, 1);
        voiceOn[2] = true;
        break;
      case 4:
        voices[3].note = note;
        voices[3].velocity = velocity;
        voices[3].timeOn = millis();
        MIDI5.sendNoteOn(note, velocity, 1);
        voiceOn[3] = true;
        break;
      case 5:
        voices[4].note = note;
        voices[4].velocity = velocity;
        voices[4].timeOn = millis();
        MIDI2.sendNoteOn(note, velocity, 1);
        voiceOn[4] = true;
        break;
      case 6:
        voices[5].note = note;
        voices[5].velocity = velocity;
        voices[5].timeOn = millis();
        MIDI5.sendNoteOn(note, velocity, 1);
        voiceOn[5] = true;
        break;
      case 7:
        voices[6].note = note;
        voices[6].velocity = velocity;
        voices[6].timeOn = millis();
        MIDI2.sendNoteOn(note, velocity, 1);
        voiceOn[6] = true;
        break;
      case 8:
        voices[7].note = note;
        voices[7].velocity = velocity;
        voices[7].timeOn = millis();
        MIDI5.sendNoteOn(note, velocity, 1);
        voiceOn[7] = true;
        break;
    }
  }
  if (dualmode) {
    MIDI5.sendNoteOn(note, velocity, 1);
    MIDI2.sendNoteOn(note, velocity, 1);
  }
  if (splitmode) {
    if (note < (newsplitPoint + 36)) {
      MIDI2.sendNoteOn((note + lowerTranspose), velocity, 1);
    } else {
      MIDI5.sendNoteOn(note, velocity, 1);
    }
  }
}

void DinHandleNoteOff(byte channel, byte note, byte velocity) {
  numberOfNotesU = numberOfNotesU - 1;
  oldnumberOfNotesU = oldnumberOfNotesU - 1;
  numberOfNotesL = numberOfNotesL - 1;
  oldnumberOfNotesL = oldnumberOfNotesL - 1;

  if (wholemode) {
    switch (getVoiceNo(note)) {
      case 1:
        MIDI2.sendNoteOff(note, velocity, 1);
        voices[0].note = -1;
        voiceOn[0] = false;
        break;
      case 2:
        MIDI5.sendNoteOff(note, velocity, 1);
        voices[1].note = -1;
        voiceOn[1] = false;
        break;
      case 3:
        MIDI2.sendNoteOff(note, velocity, 1);
        voices[2].note = -1;
        voiceOn[2] = false;
        break;
      case 4:
        MIDI5.sendNoteOff(note, velocity, 1);
        voices[3].note = -1;
        voiceOn[3] = false;
        break;
      case 5:
        MIDI2.sendNoteOff(note, velocity, 1);
        voices[4].note = -1;
        voiceOn[4] = false;
        break;
      case 6:
        MIDI5.sendNoteOff(note, velocity, 1);
        voices[5].note = -1;
        voiceOn[5] = false;
        break;
      case 7:
        MIDI2.sendNoteOff(note, velocity, 1);
        voices[6].note = -1;
        voiceOn[6] = false;
        break;
      case 8:
        MIDI5.sendNoteOff(note, velocity, 1);
        voices[7].note = -1;
        voiceOn[7] = false;
        break;
    }
  }
  if (dualmode) {
    MIDI2.sendNoteOff(note, velocity, 1);
    MIDI5.sendNoteOff(note, velocity, 1);
  }
  if (splitmode) {
    if (note < (newsplitPoint + 36)) {
      MIDI2.sendNoteOff((note + lowerTranspose), velocity, 1);
    } else {
      MIDI5.sendNoteOff(note, velocity, 1);
    }
  }
}

int getVoiceNo(int note) {
  voiceToReturn = -1;       //Initialise to 'null'
  earliestTime = millis();  //Initialise to now
  if (note == -1) {
    //NoteOn() - Get the oldest free voice (recent voices may be still on release stage)
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == -1) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    if (voiceToReturn == -1) {
      //No free voices, need to steal oldest sounding voice
      earliestTime = millis();  //Reinitialise
      for (int i = 0; i < NO_OF_VOICES; i++) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    return voiceToReturn + 1;
  } else {
    //NoteOff() - Get voice number from note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }
  //Shouldn't get here, return voice 1
  return 1;
}

int getVoiceNoPoly2(int note) {
  voiceToReturn = -1;       // Initialize to 'null'
  earliestTime = millis();  // Initialize to now

  if (note == -1) {
    // NoteOn() - Get the oldest free voice (recent voices may still be on the release stage)
    if (voices[lastUsedVoice].note == -1) {
      return lastUsedVoice + 1;
    }

    // If the last used voice is not free or doesn't exist, check if the first voice is free
    if (voices[0].note == -1) {
      return 1;
    }

    // Find the lowest available voice for the new note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == -1) {
        return i + 1;
      }
    }

    // If no voice is available, release the oldest note
    int oldestVoice = 0;
    for (int i = 1; i < NO_OF_VOICES; i++) {
      if (voices[i].timeOn < voices[oldestVoice].timeOn) {
        oldestVoice = i;
      }
    }
    return oldestVoice + 1;
  } else {
    // NoteOff() - Get the voice number from the note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }

  // Shouldn't get here, return voice 1
  return 1;
}

void DinHandlePitchBend(byte channel, int pitch) {
  MIDI2.sendPitchBend(pitch, 1);
  MIDI5.sendPitchBend(pitch, 1);
}

void getDelayTime() {
  delaytimeL = lowerData[P_LFODelay];
  if (delaytimeL <= 0) {
    delaytimeL = 0.1;
  }
  intervalL = (delaytimeL * 10);

  delaytimeU = upperData[P_LFODelay];
  if (delaytimeU <= 0) {
    delaytimeU = 0.1;
  }
  intervalU = (delaytimeU * 10);
}

void allNotesOff() {
  midiCCOutCPU2(CCallnotesoff, 0, 1);
  midiCCOutCPU5(CCallnotesoff, 0, 1);
  for (int i = 0; i < NO_OF_VOICES; i++) {
    voices[i].note = -1;
    voiceOn[i] = false;
  }
}

void updatepwLFO(boolean announce) {

  if (announce) {
    showCurrentParameterPage("PWM Rate", int(pwLFOstr));
    if (upperSW) {
      midiCCOut(CCpwLFO, upperData[P_pwLFO] / midioutfrig);
    } else {
      midiCCOut(CCpwLFO, lowerData[P_pwLFO] / midioutfrig);
    }
  }
}

void updatefmDepth(boolean announce) {
  if (announce) {
    showCurrentParameterPage("FM Depth", int(fmDepthstr));
    if (upperSW) {
      midiCCOut(CCfmDepth, (upperData[P_fmDepth] / midioutfrig));
    } else {
      midiCCOut(CCfmDepth, (lowerData[P_fmDepth] / midioutfrig));
    }
  }
}

void updateOsc1PW(boolean announce) {

  if (announce) {
    showCurrentParameterPage("OSC1 PW", String(osc1PWstr) + " %");
    if (upperSW) {
      midiCCOut(CCosc1PW, (upperData[P_osc1PW] / midioutfrig));
    } else {
      midiCCOut(CCosc1PW, (lowerData[P_osc1PW] / midioutfrig));
    }
  }
}

void updateOsc2PW(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 PW", String(osc2PWstr) + " %");
    if (upperSW) {
      midiCCOut(CCosc2PW, (upperData[P_osc2PW] / midioutfrig));
    } else {
      midiCCOut(CCosc2PW, (lowerData[P_osc2PW] / midioutfrig));
    }
  }
}

void updateOsc2PWM(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 PWM", int(osc2PWMstr));
    if (upperSW) {
      midiCCOut(CCosc2PWM, (upperData[P_osc2PWM] / midioutfrig));
    } else {
      midiCCOut(CCosc2PWM, (lowerData[P_osc2PWM] / midioutfrig));
    }
  }
}

void updateOsc1PWM(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC1 PWM", int(osc1PWMstr));
    if (upperSW) {
      midiCCOut(CCosc1PWM, (upperData[P_osc1PWM] / midioutfrig));
    } else {
      midiCCOut(CCosc1PWM, (lowerData[P_osc1PWM] / midioutfrig));
    }
  }
}

void updateOsc1Range(boolean announce) {
  if (upperSW) {
    if (upperData[P_osc1Range] > 600) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("8"));
      }
      midiCCOutCPU5(CCosc1Range, 127, 1);
    } else if (upperData[P_osc1Range] < 600 && upperData[P_osc1Range] > 400) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("16"));
      }
      midiCCOutCPU5(CCosc1Range, 63, 1);
    } else {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("32"));
      }
      midiCCOutCPU5(CCosc1Range, 0, 1);
    }
  } else {
    if (lowerData[P_osc1Range] > 600) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("8"));
      }
      midiCCOutCPU2(CCosc1Range, 127, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc1Range, 127, 1);
      }
    } else if (lowerData[P_osc1Range] < 600 && lowerData[P_osc1Range] > 400) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("16"));
      }
      midiCCOutCPU2(CCosc1Range, 63, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc1Range, 63, 1);
      }
    } else {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("32"));
      }
      midiCCOutCPU2(CCosc1Range, 0, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc1Range, 0, 1);
      }
    }
  }
}

void updateOsc2Range(boolean announce) {

  if (upperSW) {
    if (upperData[P_osc2Range] > 600) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("8"));
      }
      midiCCOutCPU5(CCosc2Range, 127, 1);
    } else if (upperData[P_osc2Range] < 600 && upperData[P_osc2Range] > 400) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("16"));
      }
      midiCCOutCPU5(CCosc2Range, 63, 1);
    } else {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("32"));
      }
      midiCCOutCPU5(CCosc2Range, 0, 1);
    }
  } else {
    if (lowerData[P_osc2Range] > 600) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("8"));
      }
      midiCCOutCPU2(CCosc2Range, 127, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc2Range, 127, 1);
      }
    } else if (lowerData[P_osc2Range] < 600 && lowerData[P_osc2Range] > 400) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("16"));
      }
      midiCCOutCPU2(CCosc2Range, 63, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc2Range, 63, 1);
      }
    } else {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("32"));
      }
      midiCCOutCPU2(CCosc2Range, 0, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc2Range, 0, 1);
      }
    }
  }
}

void updatestack() {
  switch (stackstr) {
    case 7:
      showCurrentParameterPage("Voice Stack", String("8 Note"));
      break;

    case 6:
      showCurrentParameterPage("Voice Stack", String("7 Note"));
      break;

    case 5:
      showCurrentParameterPage("Voice Stack", String("6 Note"));
      break;

    case 4:
      showCurrentParameterPage("Voice Stack", String("5 Note"));
      break;

    case 3:
      showCurrentParameterPage("Voice Stack", String("4 Note"));
      break;

    case 2:
      showCurrentParameterPage("Voice Stack", String("3 Note"));
      break;

    case 1:
      showCurrentParameterPage("Voice Stack", String("2 Note"));
      break;

    case 0:
      showCurrentParameterPage("Voice Stack", String("Poly"));
      break;
  }
}

void updateglideTime(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Glide Time", String(glideTimestr * 10) + " Seconds");
    if (upperSW) {
      midiCCOut(CCglideTime, upperData[P_glideTime] / midioutfrig);
    } else {
      midiCCOut(CCglideTime, lowerData[P_glideTime] / midioutfrig);
    }
  }
  if (upperSW) {
    midiCCOutCPU5(CCglideTime, (upperData[P_glideTime] / midioutfrig), 1);
  } else {
    midiCCOutCPU2(CCglideTime, (lowerData[P_glideTime] / midioutfrig), 1);
    if (wholemode) {
      midiCCOutCPU5(CCglideTime, (upperData[P_glideTime] / midioutfrig), 1);
    }
  }
}

void updateOsc2Detune(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 Detune", String(osc2Detunestr));
    if (upperSW) {
      midiCCOut(CCosc2Detune, (upperData[P_osc2Detune] / midioutfrig));
    } else {
      midiCCOut(CCosc2Detune, (lowerData[P_osc2Detune] / midioutfrig));
    }
  }
}

void updatenoiseLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Noise Level", String(noiseLevelstr));
    if (upperSW) {
      midiCCOut(CCnoiseLevel, upperData[P_noiseLevel] / midioutfrig);
    } else {
      midiCCOut(CCnoiseLevel, lowerData[P_noiseLevel] / midioutfrig);
    }
  }
}

void updateOsc2SawLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 Saw", int(osc2SawLevelstr));
    if (upperSW) {
      midiCCOut(CCosc2SawLevel, (upperData[P_osc2SawLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc2SawLevel, (lowerData[P_osc2SawLevel] / midioutfrig));
    }
  }
}

void updateOsc1SawLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC1 Saw", int(osc1SawLevelstr));
    if (upperSW) {
      midiCCOut(CCosc1SawLevel, (upperData[P_osc1SawLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc1SawLevel, (lowerData[P_osc1SawLevel] / midioutfrig));
    }
  }
}

void updateOsc2PulseLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 Pulse", int(osc2PulseLevelstr));
    if (upperSW) {
      midiCCOut(CCosc2PulseLevel, (upperData[P_osc2PulseLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc2PulseLevel, (lowerData[P_osc2PulseLevel] / midioutfrig));
    }
  }
}

void updateOsc1PulseLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC1 Pulse", int(osc1PulseLevelstr));
    if (upperSW) {
      midiCCOut(CCosc1PulseLevel, (upperData[P_osc1PulseLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc1PulseLevel, (lowerData[P_osc1PulseLevel] / midioutfrig));
    }
  }
}

void updateOsc2TriangleLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC2 Triangle", int(osc2TriangleLevelstr));
    if (upperSW) {
      midiCCOut(CCosc2TriangleLevel, (upperData[P_osc2TriangleLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc2TriangleLevel, (lowerData[P_osc2TriangleLevel] / midioutfrig));
    }
  }
}

void updateOsc1SubLevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("OSC1 Sub", int(osc1SubLevelstr));
    if (upperSW) {
      midiCCOut(CCosc1SubLevel, (upperData[P_osc1SubLevel] / midioutfrig));
    } else {
      midiCCOut(CCosc1SubLevel, (lowerData[P_osc1SubLevel] / midioutfrig));
    }
  }
}

void updateamDepth(boolean announce) {
  if (announce) {
    showCurrentParameterPage("AM Depth", int(amDepthstr));
    if (upperSW) {
      midiCCOut(CCamDepth, upperData[P_amDepth] / midioutfrig);
    } else {
      midiCCOut(CCamDepth, lowerData[P_amDepth] / midioutfrig);
    }
  }
}

void updateFilterCutoff(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Cutoff", String(filterCutoffstr) + " Hz");
    if (upperSW) {
      midiCCOut(CCfilterCutoff, (upperData[P_filterCutoff] / midioutfrig));
    } else {
      midiCCOut(CCfilterCutoff, (lowerData[P_filterCutoff] / midioutfrig));
    }
  }
}

void updatefilterLFO(boolean announce) {
  if (announce) {
    showCurrentParameterPage("TM depth", int(filterLFOstr));
    if (upperSW) {
      midiCCOut(CCfilterLFO, upperData[P_filterLFO] / midioutfrig);
    } else {
      midiCCOut(CCfilterLFO, lowerData[P_filterLFO] / midioutfrig);
    }
  }
}

void updatefilterRes(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Resonance", int(filterResstr));
    if (upperSW) {
      midiCCOut(CCfilterRes, upperData[P_filterRes] / midioutfrig);
    } else {
      midiCCOut(CCfilterRes, lowerData[P_filterRes] / midioutfrig);
    }
  }
}

void updateFilterType(boolean announce) {
  if (upperSW) {
    switch (upperData[P_filterType]) {
      case 0:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P LowPass"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P LowPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, LOW);
        srp.writePin(FILTERB_UPPER, LOW);
        srp.writePin(FILTERC_UPPER, LOW);
        break;

      case 1:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("1P LowPass"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P LowPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, HIGH);
        srp.writePin(FILTERB_UPPER, LOW);
        srp.writePin(FILTERC_UPPER, LOW);
        break;

      case 2:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P HighPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, LOW);
        srp.writePin(FILTERB_UPPER, HIGH);
        srp.writePin(FILTERC_UPPER, LOW);
        break;

      case 3:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("1P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P HighPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, HIGH);
        srp.writePin(FILTERB_UPPER, HIGH);
        srp.writePin(FILTERC_UPPER, LOW);
        break;

      case 4:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P BandPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, LOW);
        srp.writePin(FILTERB_UPPER, LOW);
        srp.writePin(FILTERC_UPPER, HIGH);
        break;

      case 5:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P BP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P BandPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, HIGH);
        srp.writePin(FILTERB_UPPER, LOW);
        srp.writePin(FILTERC_UPPER, HIGH);
        break;

      case 6:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P AP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P AllPass"));
          }
        }
        srp.writePin(FILTERA_UPPER, LOW);
        srp.writePin(FILTERB_UPPER, HIGH);
        srp.writePin(FILTERC_UPPER, HIGH);
        break;

      case 7:
        if (upperData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P Notch + LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("Notch"));
          }
        }
        srp.writePin(FILTERA_UPPER, HIGH);
        srp.writePin(FILTERB_UPPER, HIGH);
        srp.writePin(FILTERC_UPPER, HIGH);
        break;
    }
  } else {
    switch (lowerData[P_filterType]) {
      case 0:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P LowPass"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P LowPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, LOW);
        srp.writePin(FILTERB_LOWER, LOW);
        srp.writePin(FILTERC_LOWER, LOW);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, LOW);
          srp.writePin(FILTERB_UPPER, LOW);
          srp.writePin(FILTERC_UPPER, LOW);
        }
        break;

      case 1:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("1P LowPass"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P LowPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, HIGH);
        srp.writePin(FILTERB_LOWER, LOW);
        srp.writePin(FILTERC_LOWER, LOW);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, HIGH);
          srp.writePin(FILTERB_UPPER, LOW);
          srp.writePin(FILTERC_UPPER, LOW);
        }
        break;

      case 2:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P HighPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, LOW);
        srp.writePin(FILTERB_LOWER, HIGH);
        srp.writePin(FILTERC_LOWER, LOW);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, LOW);
          srp.writePin(FILTERB_UPPER, HIGH);
          srp.writePin(FILTERC_UPPER, LOW);
        }
        break;

      case 3:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("1P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P HighPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, HIGH);
        srp.writePin(FILTERB_LOWER, HIGH);
        srp.writePin(FILTERC_LOWER, LOW);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, HIGH);
          srp.writePin(FILTERB_UPPER, HIGH);
          srp.writePin(FILTERC_UPPER, LOW);
        }
        break;

      case 4:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P HP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("4P BandPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, LOW);
        srp.writePin(FILTERB_LOWER, LOW);
        srp.writePin(FILTERC_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, LOW);
          srp.writePin(FILTERB_UPPER, LOW);
          srp.writePin(FILTERC_UPPER, HIGH);
        }
        break;

      case 5:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P BP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P BandPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, HIGH);
        srp.writePin(FILTERB_LOWER, LOW);
        srp.writePin(FILTERC_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, HIGH);
          srp.writePin(FILTERB_UPPER, LOW);
          srp.writePin(FILTERC_UPPER, HIGH);
        }
        break;


      case 6:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P AP + 1P LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("3P AllPass"));
          }
        }
        srp.writePin(FILTERA_LOWER, LOW);
        srp.writePin(FILTERB_LOWER, HIGH);
        srp.writePin(FILTERC_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, LOW);
          srp.writePin(FILTERB_UPPER, HIGH);
          srp.writePin(FILTERC_UPPER, HIGH);
        }
        break;

      case 7:
        if (lowerData[P_filterPoleSW] == 1) {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("2P Notch + LP"));
          }
        } else {
          if (announce) {
            showCurrentParameterPage("Filter Type", String("Notch"));
          }
        }
        srp.writePin(FILTERA_LOWER, HIGH);
        srp.writePin(FILTERB_LOWER, HIGH);
        srp.writePin(FILTERC_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTERA_UPPER, HIGH);
          srp.writePin(FILTERB_UPPER, HIGH);
          srp.writePin(FILTERC_UPPER, HIGH);
        }
        break;
    }
  }
}

void updatefilterEGlevel(boolean announce) {
  if (announce) {
    showCurrentParameterPage("EG Depth", int(filterEGlevelstr));
    if (upperSW) {
      midiCCOut(CCfilterEGlevel, upperData[P_filterEGlevel] / midioutfrig);
    } else {
      midiCCOut(CCfilterEGlevel, lowerData[P_filterEGlevel] / midioutfrig);
    }
  }
}

void updatekeytrack(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Keytrack", int(keytrackstr));
  }
  if (upperSW) {
    midiCCOut(CCkeyTrack, upperData[P_keytrack] / midioutfrig);
    midiCCOutCPU5(CCkeyTrack, (upperData[P_keytrack] / 16), 1);
  } else {
    midiCCOut(CCkeyTrack, lowerData[P_keytrack] / midioutfrig);
    midiCCOutCPU2(CCkeyTrack, (lowerData[P_keytrack] / 16), 1);
    if (wholemode) {
      midiCCOutCPU5(CCkeyTrack, (upperData[P_keytrack] / 16), 1);
    }
  }
}

void updateLFORate(boolean announce) {

  if (announce) {
    showCurrentParameterPage("LFO Rate", String(LFORatestr) + " Hz");
    if (upperSW) {
      midiCCOut(CCLFORate, upperData[P_LFORate] / midioutfrig);
    } else {
      midiCCOut(CCLFORate, lowerData[P_LFORate] / midioutfrig);
    }
  }
}

void updateLFODelay(boolean announce) {
  if (announce) {
    showCurrentParameterPage("LFO Delay", String(LFODelaystr));
    if (upperSW) {
      midiCCOut(CCLFODelay, upperData[P_LFODelay] / midioutfrig);
    } else {
      midiCCOut(CCLFODelay, lowerData[P_LFODelay] / midioutfrig);
    }
  }
}

void updateStratusLFOWaveform(boolean announce) {

  if (upperSW) {
    panelData[P_LFOWaveform] = map(upperData[P_LFOWaveform], 0, 1023, 0, 7);
    panelData[P_lfoAlt] = upperData[P_lfoAlt];
  } else {
    panelData[P_LFOWaveform] = map(lowerData[P_LFOWaveform], 0, 1023, 0, 7);
    panelData[P_lfoAlt] = lowerData[P_lfoAlt];
  }

  if (panelData[P_lfoAlt]) {
    switch (panelData[P_LFOWaveform]) {
      case 0:
        StratusLFOWaveform = "Sawtooth Up";
        LFOWaveCV = 10;
        break;

      case 1:
        StratusLFOWaveform = "Sawtooth Down";
        LFOWaveCV = 160;
        break;

      case 2:
        StratusLFOWaveform = "Squarewave";
        LFOWaveCV = 280;
        break;

      case 3:
        StratusLFOWaveform = "Triangle";
        LFOWaveCV = 400;
        break;

      case 4:
        StratusLFOWaveform = "Sinewave";
        LFOWaveCV = 592;
        break;

      case 5:
        StratusLFOWaveform = "Sweeps";
        LFOWaveCV = 720;
        break;

      case 6:
        StratusLFOWaveform = "Lumps";
        LFOWaveCV = 840;
        break;

      case 7:
        StratusLFOWaveform = "Sample & Hold";
        LFOWaveCV = 968;
        break;
    }
  } else {
    switch (panelData[P_LFOWaveform]) {
      case 0:
        StratusLFOWaveform = "Saw +Oct";
        LFOWaveCV = 10;
        break;

      case 1:
        StratusLFOWaveform = "Quad Saw";
        LFOWaveCV = 160;
        break;

      case 2:
        StratusLFOWaveform = "Quad Pulse";
        LFOWaveCV = 280;
        break;

      case 3:
        StratusLFOWaveform = "Tri Step";
        LFOWaveCV = 400;
        break;

      case 4:
        StratusLFOWaveform = "Sine +Oct";
        LFOWaveCV = 592;
        break;

      case 5:
        StratusLFOWaveform = "Sine +3rd";
        LFOWaveCV = 720;
        break;

      case 6:
        StratusLFOWaveform = "Sine +4th";
        LFOWaveCV = 840;
        break;

      case 7:
        StratusLFOWaveform = "Rand Slopes";
        LFOWaveCV = 968;
        break;
    }
  }
  if (announce) {
    showCurrentParameterPage("LFO Wave", StratusLFOWaveform);
  }
  if (upperSW) {
    LFOWaveCVupper = LFOWaveCV;
  } else {
    LFOWaveCVlower = LFOWaveCV;
    if (wholemode) {
      LFOWaveCVupper = LFOWaveCV;
    }
  }
}

void updatefilterAttack(boolean announce) {
  if (announce) {
    if (filterAttackstr < 1000) {
      showCurrentParameterPage("VCF Attack", String(int(filterAttackstr)) + " ms", FILTER_ENV);
    } else {
      showCurrentParameterPage("VCF Attack", String(filterAttackstr * 0.001) + " s", FILTER_ENV);
    }
    if (upperSW) {
      midiCCOut(CCfilterAttack, upperData[P_filterAttack] / midioutfrig);
    } else {
      midiCCOut(CCfilterAttack, lowerData[P_filterAttack] / midioutfrig);
    }
  }
}

void updatefilterDecay(boolean announce) {
  if (announce) {
    if (filterDecaystr < 1000) {
      showCurrentParameterPage("VCF Decay", String(int(filterDecaystr)) + " ms", FILTER_ENV);
    } else {
      showCurrentParameterPage("VCF Decay", String(filterDecaystr * 0.001) + " s", FILTER_ENV);
    }
    if (upperSW) {
      midiCCOut(CCfilterDecay, upperData[P_filterDecay] / midioutfrig);
    } else {
      midiCCOut(CCfilterDecay, lowerData[P_filterDecay] / midioutfrig);
    }
  }
}

void updatefilterSustain(boolean announce) {
  if (announce) {
    showCurrentParameterPage("VCF Sustain", String(filterSustainstr), FILTER_ENV);
    if (upperSW) {
      midiCCOut(CCfilterSustain, upperData[P_filterSustain] / midioutfrig);
    } else {
      midiCCOut(CCfilterSustain, lowerData[P_filterSustain] / midioutfrig);
    }
  }
}

void updatefilterRelease(boolean announce) {
  if (announce) {
    if (filterReleasestr < 1000) {
      showCurrentParameterPage("VCF Release", String(int(filterReleasestr)) + " ms", FILTER_ENV);
    } else {
      showCurrentParameterPage("VCF Release", String(filterReleasestr * 0.001) + " s", FILTER_ENV);
    }
    if (upperSW) {
      midiCCOut(CCfilterRelease, upperData[P_filterRelease] / midioutfrig);
    } else {
      midiCCOut(CCfilterRelease, lowerData[P_filterRelease] / midioutfrig);
    }
  }
}

void updateampAttack(boolean announce) {
  if (announce) {
    if (ampAttackstr < 1000) {
      showCurrentParameterPage("VCA Attack", String(int(ampAttackstr)) + " ms", AMP_ENV);
    } else {
      showCurrentParameterPage("VCA Attack", String(ampAttackstr * 0.001) + " s", AMP_ENV);
    }
    if (upperSW) {
      midiCCOut(CCampAttack, upperData[P_ampAttack] / midioutfrig);
    } else {
      midiCCOut(CCampAttack, lowerData[P_ampAttack] / midioutfrig);
    }
  }
}

void updateampDecay(boolean announce) {
  if (announce) {
    if (ampDecaystr < 1000) {
      showCurrentParameterPage("VCA Decay", String(int(ampDecaystr)) + " ms", AMP_ENV);
    } else {
      showCurrentParameterPage("VCA Decay", String(ampDecaystr * 0.001) + " s", AMP_ENV);
    }
    if (upperSW) {
      midiCCOut(CCampDecay, upperData[P_ampDecay] / midioutfrig);
    } else {
      midiCCOut(CCampDecay, lowerData[P_ampDecay] / midioutfrig);
    }
  }
}

void updateampSustain(boolean announce) {
  if (announce) {
    showCurrentParameterPage("VCA Sustain", String(ampSustainstr), AMP_ENV);
    if (upperSW) {
      midiCCOut(CCampSustain, upperData[P_ampSustain] / midioutfrig);
    } else {
      midiCCOut(CCampSustain, lowerData[P_ampSustain] / midioutfrig);
    }
  }
}

void updateampRelease(boolean announce) {
  if (announce) {
    if (ampReleasestr < 1000) {
      showCurrentParameterPage("VCA Release", String(int(ampReleasestr)) + " ms", AMP_ENV);
    } else {
      showCurrentParameterPage("VCA Release", String(ampReleasestr * 0.001) + " s", AMP_ENV);
    }
    if (upperSW) {
      midiCCOut(CCampRelease, upperData[P_ampRelease] / midioutfrig);
    } else {
      midiCCOut(CCampRelease, lowerData[P_ampRelease] / midioutfrig);
    }
  }
}

void updatevolumeControl(boolean announce) {
  if (announce) {
    showCurrentParameterPage("Volume", int(volumeControlstr));
    if (upperSW) {
      midiCCOut(CCvolumeControl, upperData[P_volumeControl] / midioutfrig);
    } else {
      midiCCOut(CCvolumeControl, lowerData[P_volumeControl] / midioutfrig);
    }
  }
}

////////////////////////////////////////////////////////////////

void updateglideSW(boolean announce) {
  if (upperSW) {
    if (upperData[P_glideSW] == 0) {
      if (announce) {
        showCurrentParameterPage("Glide", "Off");
      }
      midiCCOutCPU5(CCglideSW, 1, 1);
      delay(1);
      midiCCOutCPU5(CCglideTime, 0, 1);
      sr.writePin(GLIDE_LED, LOW);  // LED off
    } else {
      if (announce) {
        showCurrentParameterPage("Glide", "On");
      }
      midiCCOutCPU5(CCglideTime, int(upperData[P_glideTime] / 8), 1);
      delay(1);
      midiCCOutCPU5(CCglideSW, 127, 1);
      sr.writePin(GLIDE_LED, HIGH);  // LED on
    }
  } else {
    if (lowerData[P_glideSW] == 0) {
      if (announce) {
        showCurrentParameterPage("Glide", "Off");
      }
      midiCCOutCPU2(CCglideSW, 1, 1);
      delay(1);
      midiCCOutCPU2(CCglideTime, 0, 1);
      if (wholemode) {
        midiCCOutCPU5(CCglideSW, 1, 1);
        delay(1);
        midiCCOutCPU5(CCglideTime, 0, 1);
      }
      sr.writePin(GLIDE_LED, LOW);  // LED off
    } else {
      if (announce) {
        showCurrentParameterPage("Glide", "On");
      }
      midiCCOutCPU2(CCglideTime, int(lowerData[P_glideTime] / 8), 1);
      delay(1);
      midiCCOutCPU2(CCglideSW, 127, 1);
      if (wholemode) {
        midiCCOutCPU5(CCglideSW, 127, 1);
        delay(1);
        midiCCOutCPU5(CCglideTime, int(upperData[P_glideTime] / 8), 1);
      }
      sr.writePin(GLIDE_LED, HIGH);  // LED on
    }
  }
}

void updatefilterPoleSwitch(boolean announce) {
  if (upperSW) {
    if (upperData[P_filterPoleSW] == 1) {
      if (announce) {
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 127);
      }
      updateFilterType(0);
      sr.writePin(FILTERPOLE_LED, HIGH);
      srp.writePin(FILTER_POLE_UPPER, HIGH);
    } else {
      if (announce)
        midiCCOut(CCfilterPoleSW, 1);
      updateFilterType(1);
    }
    updateFilterType(0);
    sr.writePin(FILTERPOLE_LED, LOW);
    srp.writePin(FILTER_POLE_UPPER, LOW);
  } else {
    if (lowerData[P_filterPoleSW] == 1) {
      if (announce) {
        midiCCOut(CCfilterPoleSW, 127);
        updateFilterType(1);
      }
      updateFilterType(0);
      sr.writePin(FILTERPOLE_LED, HIGH);
      srp.writePin(FILTER_POLE_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(FILTER_POLE_UPPER, HIGH);
      }
    } else {
      if (announce) {
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 1);
      }
      updateFilterType(0);
      sr.writePin(FILTERPOLE_LED, LOW);
      srp.writePin(FILTER_POLE_LOWER, LOW);
      if (wholemode) {
        srp.writePin(FILTER_POLE_UPPER, LOW);
      }
    }
  }
}

void updatefilterLoop(boolean announce) {
  if (upperSW) {
    switch (upperData[P_statefilterLoop]) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCF Key Loop", "On");
          midiCCOut(CCfilterLoop, 127);
        }
        sr.writePin(FILTERLOOP_LED, HIGH);        // LED on
        sr.writePin(FILTERLOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
        srp.writePin(FILTER_MODE_BIT1_UPPER, HIGH);
        oldfilterLoop = upperData[P_statefilterLoop];
        break;

      case 2:
        if (announce) {
          showCurrentParameterPage("VCF LFO Loop", "On");
          midiCCOut(CCfilterDoubleLoop, 127);
        }
        sr.writePin(FILTERLOOP_DOUBLE_LED, HIGH);  // LED on
        sr.writePin(FILTERLOOP_LED, LOW);
        srp.writePin(FILTER_MODE_BIT0_UPPER, HIGH);
        srp.writePin(FILTER_MODE_BIT1_UPPER, HIGH);
        oldfilterLoop = upperData[P_statefilterLoop];
        break;

      default:
        if (announce) {
          showCurrentParameterPage("VCF Looping", "Off");
          midiCCOut(CCfilterLoop, 1);
        }
        sr.writePin(FILTERLOOP_LED, LOW);         // LED off
        sr.writePin(FILTERLOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
        srp.writePin(FILTER_MODE_BIT1_UPPER, LOW);
        oldfilterLoop = 0;
        break;
    }
  } else {
    switch (lowerData[P_statefilterLoop]) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCF Key Loop", "On");
          midiCCOut(CCfilterLoop, 127);
        }
        sr.writePin(FILTERLOOP_LED, HIGH);        // LED on
        sr.writePin(FILTERLOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(FILTER_MODE_BIT0_LOWER, LOW);
        srp.writePin(FILTER_MODE_BIT1_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
          srp.writePin(FILTER_MODE_BIT1_UPPER, HIGH);
        }
        oldfilterLoop = lowerData[P_statefilterLoop];
        break;

      case 2:
        if (announce) {
          showCurrentParameterPage("VCF LFO Loop", "On");
          midiCCOut(CCfilterDoubleLoop, 127);
        }
        sr.writePin(FILTERLOOP_DOUBLE_LED, HIGH);  // LED on
        sr.writePin(FILTERLOOP_LED, LOW);
        srp.writePin(FILTER_MODE_BIT0_LOWER, HIGH);
        srp.writePin(FILTER_MODE_BIT1_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
          srp.writePin(FILTER_MODE_BIT1_UPPER, LOW);
        }
        oldfilterLoop = lowerData[P_statefilterLoop];
        break;

      default:
        if (announce) {
          showCurrentParameterPage("VCF Looping", "Off");
          midiCCOut(CCfilterLoop, 1);
        }
        sr.writePin(FILTERLOOP_LED, LOW);         // LED off
        sr.writePin(FILTERLOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(FILTER_MODE_BIT0_LOWER, LOW);
        srp.writePin(FILTER_MODE_BIT1_LOWER, LOW);
        if (wholemode) {
          srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
          srp.writePin(FILTER_MODE_BIT1_UPPER, LOW);
        }
        oldfilterLoop = 0;
        break;
    }
  }
}

void updatefilterEGinv(boolean announce) {
  if (upperSW) {
    if (upperData[P_filterEGinv] == 0) {
      if (announce) {
        showCurrentParameterPage("Filter Env", "Positive");
        midiCCOut(CCfilterEGinv, 1);
      }
      sr.writePin(FILTERINV_LED, LOW);  // LED off
      srp.writePin(FILTER_EG_INV_UPPER, LOW);
    } else {
      if (announce) {
        showCurrentParameterPage("Filter Env", "Negative");
        midiCCOut(CCfilterEGinv, 127);
      }
      sr.writePin(FILTERINV_LED, HIGH);  // LED on
      srp.writePin(FILTER_EG_INV_UPPER, HIGH);
    }
  } else {
    if (lowerData[P_filterEGinv] == 0) {
      if (announce) {
        showCurrentParameterPage("Filter Env", "Positive");
        midiCCOut(CCfilterEGinv, 1);
      }
      sr.writePin(FILTERINV_LED, LOW);  // LED off
      srp.writePin(FILTER_EG_INV_LOWER, LOW);
      if (wholemode) {
        srp.writePin(FILTER_EG_INV_UPPER, LOW);
      }
    } else {
      if (announce) {
        showCurrentParameterPage("Filter Env", "Negative");
        midiCCOut(CCfilterEGinv, 127);
      }
      sr.writePin(FILTERINV_LED, HIGH);  // LED on
      srp.writePin(FILTER_EG_INV_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(FILTER_EG_INV_UPPER, HIGH);
      }
    }
  }
}

void updatefilterVel(boolean announce) {
  if (upperSW) {
    if (upperData[P_filterVel] == 0) {
      if (announce) {
        showCurrentParameterPage("VCF Velocity", "Off");
        midiCCOut(CCfilterVel, 1);
      }
      sr.writePin(FILTERVEL_LED, LOW);  // LED off
      srp.writePin(FILTER_VELOCITY_UPPER, LOW);
    } else {
      if (announce) {
        showCurrentParameterPage("VCF Velocity", "On");
        midiCCOut(CCfilterVel, 127);
      }
      sr.writePin(FILTERVEL_LED, HIGH);  // LED on
      srp.writePin(FILTER_VELOCITY_UPPER, HIGH);
    }
  } else {
    if (lowerData[P_filterVel] == 0) {
      if (announce) {
        showCurrentParameterPage("VCF Velocity", "Off");
        midiCCOut(CCfilterVel, 1);
      }
      sr.writePin(FILTERVEL_LED, LOW);  // LED off
      srp.writePin(FILTER_VELOCITY_LOWER, LOW);
      if (wholemode) {
        srp.writePin(FILTER_VELOCITY_UPPER, LOW);
      }
    } else {
      if (announce) {
        showCurrentParameterPage("VCF Velocity", "On");
        midiCCOut(CCfilterVel, 127);
      }
      sr.writePin(FILTERVEL_LED, HIGH);  // LED on
      srp.writePin(FILTER_VELOCITY_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(FILTER_VELOCITY_UPPER, HIGH);
      }
    }
  }
}

void updatevcaLoop(boolean announce) {
  if (upperSW) {
    switch (upperData[P_statevcaLoop]) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCA Key Loop", "On");
          midiCCOut(CCvcaLoop, 127);
        }
        sr.writePin(VCALOOP_LED, HIGH);        // LED on
        sr.writePin(VCALOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
        srp.writePin(AMP_MODE_BIT1_UPPER, HIGH);
        oldvcaLoop = upperData[P_statevcaLoop];
        break;

      case 2:
        if (announce) {
          showCurrentParameterPage("VCA LFO Loop", "On");
          midiCCOut(CCvcaDoubleLoop, 127);
        }
        sr.writePin(VCALOOP_DOUBLE_LED, HIGH);  // LED on
        sr.writePin(VCALOOP_LED, LOW);
        srp.writePin(AMP_MODE_BIT0_UPPER, HIGH);
        srp.writePin(AMP_MODE_BIT1_UPPER, HIGH);
        oldvcaLoop = upperData[P_statevcaLoop];
        break;

      default:
        if (announce) {
          showCurrentParameterPage("VCA Looping", "Off");
          midiCCOut(CCvcaLoop, 1);
        }
        sr.writePin(VCALOOP_LED, LOW);         // LED off
        sr.writePin(VCALOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
        srp.writePin(AMP_MODE_BIT1_UPPER, LOW);
        oldvcaLoop = 0;
        break;
    }
  } else {
    switch (lowerData[P_statevcaLoop]) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCA Key Loop", "On");
          midiCCOut(CCvcaLoop, 127);
        }
        sr.writePin(VCALOOP_LED, HIGH);        // LED on
        sr.writePin(VCALOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(AMP_MODE_BIT0_LOWER, LOW);
        srp.writePin(AMP_MODE_BIT1_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
          srp.writePin(AMP_MODE_BIT1_UPPER, HIGH);
        }
        oldvcaLoop = lowerData[P_statevcaLoop];
        break;

      case 2:
        if (announce) {
          showCurrentParameterPage("VCA LFO Loop", "On");
          midiCCOut(CCvcaDoubleLoop, 127);
        }
        sr.writePin(VCALOOP_DOUBLE_LED, HIGH);  // LED on
        sr.writePin(VCALOOP_LED, LOW);
        srp.writePin(AMP_MODE_BIT0_LOWER, HIGH);
        srp.writePin(AMP_MODE_BIT1_LOWER, HIGH);
        if (wholemode) {
          srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
          srp.writePin(AMP_MODE_BIT1_UPPER, LOW);
        }
        oldvcaLoop = lowerData[P_statevcaLoop];
        break;

      default:
        if (announce) {
          showCurrentParameterPage("VCA Looping", "Off");
          midiCCOut(CCvcaLoop, 1);
        }
        sr.writePin(VCALOOP_LED, LOW);         // LED off
        sr.writePin(VCALOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(AMP_MODE_BIT0_LOWER, LOW);
        srp.writePin(AMP_MODE_BIT1_LOWER, LOW);
        if (wholemode) {
          srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
          srp.writePin(AMP_MODE_BIT1_UPPER, LOW);
        }
        oldvcaLoop = 0;
        break;
    }
  }
}

void updatevcaVel(boolean announce) {
  if (upperSW) {
    if (upperData[P_vcaVel] == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Velocity", "Off");
        midiCCOut(CCvcaVel, 1);
      }
      sr.writePin(VCAVEL_LED, LOW);  // LED off
      srp.writePin(AMP_VELOCITY_UPPER, LOW);
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Velocity", "On");
        midiCCOut(CCvcaVel, 127);
      }
      sr.writePin(VCAVEL_LED, HIGH);  // LED on
      srp.writePin(AMP_VELOCITY_UPPER, HIGH);
    }
  } else {
    if (lowerData[P_vcaVel] == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Velocity", "Off");
        midiCCOut(CCvcaVel, 1);
      }
      sr.writePin(VCAVEL_LED, LOW);  // LED off
      srp.writePin(AMP_VELOCITY_LOWER, LOW);
      if (wholemode) {
        srp.writePin(AMP_VELOCITY_UPPER, LOW);
      }
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Velocity", "On");
        midiCCOut(CCvcaVel, 127);
      }
      sr.writePin(VCAVEL_LED, HIGH);  // LED on
      srp.writePin(AMP_VELOCITY_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(AMP_VELOCITY_UPPER, HIGH);
      }
    }
  }
}


void updatevcaGate(boolean announce) {
  if (upperSW) {
    if (upperData[P_vcaGate] == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "Off");
        midiCCOut(CCvcaGate, 1);
      }
      sr.writePin(VCAGATE_LED, LOW);  // LED off
      upperData[P_ampAttack] = upperData[P_oldampAttack];
      upperData[P_ampDecay] = upperData[P_oldampDecay];
      upperData[P_ampSustain] = upperData[P_oldampSustain];
      upperData[P_ampRelease] = upperData[P_oldampRelease];
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "On");
        midiCCOut(CCvcaGate, 127);
      }
      sr.writePin(VCAGATE_LED, HIGH);  // LED on
      upperData[P_ampAttack] = 0;
      upperData[P_ampDecay] = 0;
      upperData[P_ampSustain] = 1023;
      upperData[P_ampRelease] = 0;
    }
  } else {
    if (lowerData[P_vcaGate] == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "Off");
        midiCCOut(CCvcaGate, 1);
      }
      sr.writePin(VCAGATE_LED, LOW);  // LED off
      lowerData[P_ampAttack] = lowerData[P_oldampAttack];
      lowerData[P_ampDecay] = lowerData[P_oldampDecay];
      lowerData[P_ampSustain] = lowerData[P_oldampSustain];
      lowerData[P_ampRelease] = lowerData[P_oldampRelease];
      if (wholemode) {
        upperData[P_ampAttack] = upperData[P_oldampAttack];
        upperData[P_ampDecay] = upperData[P_oldampDecay];
        upperData[P_ampSustain] = upperData[P_oldampSustain];
        upperData[P_ampRelease] = upperData[P_oldampRelease];
      }
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "On");
        midiCCOut(CCvcaGate, 127);
      }
      sr.writePin(VCAGATE_LED, HIGH);  // LED on
      lowerData[P_ampAttack] = 0;
      lowerData[P_ampDecay] = 0;
      lowerData[P_ampSustain] = 1023;
      lowerData[P_ampRelease] = 0;
      if (wholemode) {
        upperData[P_ampAttack] = 0;
        upperData[P_ampDecay] = 0;
        upperData[P_ampSustain] = 1023;
        upperData[P_ampRelease] = 0;
      }
    }
  }
}

void updatelfoAlt(boolean announce) {
  bool isUpper = upperSW;
  bool lfoAltState = isUpper ? upperData[P_lfoAlt] : lowerData[P_lfoAlt];

  // Send MIDI CC messages
  midiCCOut(CClfoAlt, lfoAltState ? 127 : 0);

  // Update LFO waveform
  if (announce) {
    updateStratusLFOWaveform(1);
  } else {
    updateStratusLFOWaveform(0);
  }

  // Set pin states
  if (isUpper) {
    srp.writePin(LFO_ALT_UPPER, lfoAltState ? HIGH : LOW);
    sr.writePin(LFO_ALT_LED, lfoAltState ? LOW : HIGH);
  } else {
    srp.writePin(LFO_ALT_LOWER, lfoAltState ? HIGH : LOW);
    sr.writePin(LFO_ALT_LED, lfoAltState ? LOW : HIGH);
    if (wholemode) {
      srp.writePin(LFO_ALT_UPPER, lfoAltState ? HIGH : LOW);
      sr.writePin(LFO_ALT_LED, lfoAltState ? LOW : HIGH);
    }
  }
}

void updatekeyTrackSW(boolean announce) {
  if (upperSW) {
    if (upperData[P_keyTrackSW] == 0) {
      srp.writePin(FILTER_KEYTRACK_UPPER, LOW);
      midiCCOutCPU5(CCkeyTrackSW, 0, 1);
      delay(1);
      midiCCOutCPU5(CCkeyTrack, 1, 1);
    } else {
      srp.writePin(FILTER_KEYTRACK_UPPER, HIGH);
      midiCCOutCPU5(CCkeyTrackSW, 127, 1);
      delay(1);
      midiCCOutCPU5(CCkeyTrack, upperData[P_keytrack] / 16, 1);
    }
  } else {
    if (lowerData[P_keyTrackSW] == 0) {
      srp.writePin(FILTER_KEYTRACK_LOWER, LOW);
      midiCCOutCPU2(CCkeyTrackSW, 0, 1);
      delay(1);
      midiCCOutCPU2(CCkeyTrack, 1, 1);
      if (wholemode) {
        srp.writePin(FILTER_KEYTRACK_UPPER, LOW);
        midiCCOutCPU5(CCkeyTrackSW, 0, 1);
        delay(1);
        midiCCOutCPU5(CCkeyTrack, 1, 1);
      }
    } else {
      srp.writePin(FILTER_KEYTRACK_LOWER, HIGH);
      midiCCOutCPU2(CCkeyTrackSW, 127, 1);
      delay(1);
      midiCCOutCPU2(CCkeyTrack, lowerData[P_keytrack] / 16, 1);
      if (wholemode) {
        srp.writePin(FILTER_KEYTRACK_UPPER, HIGH);
        midiCCOutCPU5(CCkeyTrackSW, 127, 1);
        delay(1);
        midiCCOutCPU5(CCkeyTrack, upperData[P_keytrack] / 16, 1);
      }
    }
  }
}

void updateupperLower() {
  if (!wholemode) {
    if (upperSW) {
      // upper mode upperSW will be true
      sr.writePin(UPPER_LED, HIGH);  // upper LED on
      sr.writePin(LOWER_LED, LOW);   // LED off
      //srp.writePin(UPPER2, LOW);
      setAllButtons();
    } else {
      // lower mode upperSW will be false
      sr.writePin(UPPER_LED, LOW);   // LED off
      sr.writePin(LOWER_LED, HIGH);  // lower LED on
      setAllButtons();
    }
  } else {
    upperSW = 0;
  }
}

void updateplayMode(boolean announce) {
  if (playMode == 0) {
    updatewholemode(0);
  } else if (playMode == 1) {
    updatedualmode(0);
  } else if (playMode == 2) {
    updatesplitmode(0);
  }
}

void updatewholemode(boolean announce) {
  //allNotesOff();
  if (announce) {
    showCurrentParameterPage("Mode", String("Whole"));
  }
  sr.writePin(WHOLE_LED, HIGH);  // LED off
  sr.writePin(DUAL_LED, LOW);    // LED off
  sr.writePin(SPLIT_LED, LOW);   // LED off
  sr.writePin(UPPER_LED, LOW);   // LED off
  sr.writePin(LOWER_LED, HIGH);  // LED on
  srp.writePin(UPPER2, HIGH);
  upperSW = 0;
  wholemode = true;

  patchNoU = patchNoL;
  patchNameU = patchNameL;
  currentPatchNameU = currentPatchNameL;
  currentPgmNumU = currentPgmNumL;
  memcpy(upperData, lowerData, sizeof(upperData));

  lowerParamsToDisplay();
  setAllButtons();
  dualmode = false;
  splitmode = false;
}

void updatedualmode(boolean announce) {
  //allNotesOff();
  if (announce) {
    showCurrentParameterPage("Mode", String("Dual"));
  }
  sr.writePin(DUAL_LED, HIGH);  // LED on
  sr.writePin(WHOLE_LED, LOW);  // LED off
  sr.writePin(SPLIT_LED, LOW);  // LED off
  srp.writePin(UPPER2, LOW);
  dualmode = true;
  wholemode = false;
  splitmode = false;
}

void updatesplitmode(boolean announce) {
  //allNotesOff();
  if (announce) {
    showCurrentParameterPage("Mode", String("Split"));
  }
  sr.writePin(SPLIT_LED, HIGH);  // LED on
  sr.writePin(WHOLE_LED, LOW);   // LED off
  sr.writePin(DUAL_LED, LOW);    // LED off
  srp.writePin(UPPER2, LOW);
  srp.writePin(UPPER1, LOW);
  splitmode = true;
  wholemode = false;
  dualmode = false;
}

void updatechorus1(boolean announce) {
  if (upperSW) {
    if (!upperData[P_chorus1]) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("Off"));
        midiCCOut(CCchorus1, 1);
      }
      sr.writePin(CHORUS1_LED, LOW);  // LED off
      srp.writePin(CHORUS1_OUT_UPPER, LOW);
    }
    if (upperData[P_chorus1]) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("On"));
        midiCCOut(CCchorus1, 127);
      }
      sr.writePin(CHORUS1_LED, HIGH);  // LED on
      srp.writePin(CHORUS1_OUT_UPPER, HIGH);
    }
  }
  if (!upperSW) {
    if (!lowerData[P_chorus1]) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("Off"));
        midiCCOut(CCchorus1, 1);
      }
      sr.writePin(CHORUS1_LED, LOW);  // LED off
      srp.writePin(CHORUS1_OUT_LOWER, LOW);
      if (wholemode) {
        srp.writePin(CHORUS1_OUT_UPPER, LOW);
      }
    }
    if (lowerData[P_chorus1]) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("On"));
        midiCCOut(CCchorus1, 127);
      }
      sr.writePin(CHORUS1_LED, HIGH);  // LED on
      srp.writePin(CHORUS1_OUT_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(CHORUS1_OUT_UPPER, HIGH);
      }
    }
  }
}

void updatechorus2(boolean announce) {
  if (upperSW) {
    if (!upperData[P_chorus2]) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("Off"));
        midiCCOut(CCchorus2, 1);
      }
      sr.writePin(CHORUS2_LED, LOW);  // LED off
      srp.writePin(CHORUS2_OUT_UPPER, LOW);
    }
    if (upperData[P_chorus2]) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("On"));
        midiCCOut(CCchorus2, 127);
      }
      sr.writePin(CHORUS2_LED, HIGH);  // LED on
      srp.writePin(CHORUS2_OUT_UPPER, HIGH);
    }
  }
  if (!upperSW) {
    if (!lowerData[P_chorus2]) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("Off"));
        midiCCOut(CCchorus2, 1);
      }
      sr.writePin(CHORUS2_LED, LOW);  // LED off
      srp.writePin(CHORUS2_OUT_LOWER, LOW);
      if (wholemode) {
        srp.writePin(CHORUS2_OUT_UPPER, LOW);
      }
    }
    if (lowerData[P_chorus2]) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("On"));
        midiCCOut(CCchorus2, 127);
      }
      sr.writePin(CHORUS2_LED, HIGH);  // LED on
      srp.writePin(CHORUS2_OUT_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(CHORUS2_OUT_UPPER, HIGH);
      }
    }
  }
}

void updateFilterEnv(boolean announce) {
  if (upperData[P_filterLogLin] == 0) {
    srp.writePin(FILTER_LIN_LOG_UPPER, LOW);
  } else {
    srp.writePin(FILTER_LIN_LOG_UPPER, HIGH);
  }
  if (lowerData[P_filterLogLin] == 0) {
    srp.writePin(FILTER_LIN_LOG_LOWER, LOW);
    if (wholemode) {
      srp.writePin(FILTER_LIN_LOG_UPPER, LOW);
    }
  } else {
    srp.writePin(FILTER_LIN_LOG_LOWER, HIGH);
    if (wholemode) {
      srp.writePin(FILTER_LIN_LOG_UPPER, HIGH);
    }
  }
}

void updateAmpEnv(boolean announce) {
  if (upperData[P_ampLogLin] == 0) {
    srp.writePin(AMP_LIN_LOG_UPPER, LOW);
  } else {
    srp.writePin(AMP_LIN_LOG_UPPER, HIGH);
  }
  if (lowerData[P_ampLogLin] == 0) {
    srp.writePin(AMP_LIN_LOG_LOWER, LOW);
    if (wholemode) {
      srp.writePin(AMP_LIN_LOG_UPPER, LOW);
    }
  } else {
    srp.writePin(AMP_LIN_LOG_LOWER, HIGH);
    if (wholemode) {
      srp.writePin(AMP_LIN_LOG_UPPER, HIGH);
    }
  }
}

void updateMonoMulti(boolean announce) {
  if (upperSW) {
    if (upperData[P_monoMulti] == 0) {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "Off");
      }
    } else {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "On");
      }
    }
  } else {
    if (lowerData[P_monoMulti] == 0) {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "Off");
      }
    } else {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "On");
      }
    }
  }
}

void updatePitchBend() {
  showCurrentParameterPage("Bender Range", int(PitchBendLevelstr));
}

void updatemodWheel() {
  showCurrentParameterPage("Mod Range", int(modWheelLevelstr));
}

void updatePatchname() {
  refreshPatchDisplayFromState();
}

void myConvertControlChange(byte channel, byte number, byte value) {
  newvalue = value;
  myControlChange(channel, number, newvalue);
}

void myControlChange(byte channel, byte control, int value) {

  switch (control) {
    case CCpwLFO:
      if (upperSW) {
        upperData[P_pwLFO] = value;
      } else {
        lowerData[P_pwLFO] = value;
        if (wholemode) {
          upperData[P_pwLFO] = value;
        }
      }
      pwLFOstr = value / midioutfrig;  // for display
      updatepwLFO(1);
      break;

    case CCfmDepth:
      if (upperSW) {
        upperData[P_fmDepth] = value;
      } else {
        lowerData[P_fmDepth] = value;
        if (wholemode) {
          upperData[P_fmDepth] = value;
        }
      }
      fmDepthstr = value / midioutfrig;
      updatefmDepth(1);
      break;

    case CCosc2PW:
      if (upperSW) {
        upperData[P_osc2PW] = value;
      } else {
        lowerData[P_osc2PW] = value;
        if (wholemode) {
          upperData[P_osc2PW] = value;
        }
      }
      osc2PWstr = PULSEWIDTH[value / midioutfrig];
      updateOsc2PW(1);
      break;

    case CCosc2PWM:
      if (upperSW) {
        upperData[P_osc2PWM] = value;
      } else {
        lowerData[P_osc2PWM] = value;
        if (wholemode) {
          upperData[P_osc2PWM] = value;
        }
      }
      osc2PWMstr = value / midioutfrig;
      updateOsc2PWM(1);
      break;

    case CCosc1PW:
      if (upperSW) {
        upperData[P_osc1PW] = value;
      } else {
        lowerData[P_osc1PW] = value;
        if (wholemode) {
          upperData[P_osc1PW] = value;
        }
      }
      osc1PWstr = PULSEWIDTH[value / midioutfrig];
      updateOsc1PW(1);
      break;

    case CCosc1PWM:
      if (upperSW) {
        upperData[P_osc1PWM] = value;
      } else {
        lowerData[P_osc1PWM] = value;
        if (wholemode) {
          upperData[P_osc1PWM] = value;
        }
      }
      osc1PWMstr = value / midioutfrig;
      updateOsc1PWM(1);
      break;

    case CCosc1Range:
      if (upperSW) {
        upperData[P_osc1Range] = value;
      } else {
        lowerData[P_osc1Range] = value;
        if (wholemode) {
          upperData[P_osc1Range] = value;
        }
      }
      updateOsc1Range(1);
      break;

    case CCosc2Range:
      if (upperSW) {
        upperData[P_osc2Range] = value;
      } else {
        lowerData[P_osc2Range] = value;
        if (wholemode) {
          upperData[P_osc2Range] = value;
        }
      }
      updateOsc2Range(1);
      break;

    case CCstack:
      if (upperSW) {
        upperData[P_stack] = value;
      } else {
        lowerData[P_stack] = value;
        if (wholemode) {
          upperData[P_stack] = value;
        }
      }
      stackstr = value >> 7;
      updatestack();
      break;

    case CCglideTime:
      if (upperSW) {
        upperData[P_glideTime] = value;
      } else {
        lowerData[P_glideTime] = value;
        if (wholemode) {
          upperData[P_glideTime] = value;
        }
      }
      glideTimestr = LINEAR[value / midioutfrig];
      updateglideTime(1);
      break;

    case CCosc2Detune:
      if (upperSW) {
        upperData[P_osc2Detune] = value;
      } else {
        lowerData[P_osc2Detune] = value;
        if (wholemode) {
          upperData[P_osc2Detune] = value;
        }
      }
      osc2Detunestr = PULSEWIDTH[value / midioutfrig];
      updateOsc2Detune(1);
      break;

    case CCnoiseLevel:
      if (upperSW) {
        upperData[P_noiseLevel] = value;
      } else {
        lowerData[P_noiseLevel] = value;
        if (wholemode) {
          upperData[P_noiseLevel] = value;
        }
      }
      noiseLevelstr = LINEARCENTREZERO[value / midioutfrig];
      updatenoiseLevel(1);
      break;

    case CCosc2SawLevel:
      if (upperSW) {
        upperData[P_osc2SawLevel] = value;
      } else {
        lowerData[P_osc2SawLevel] = value;
        if (wholemode) {
          upperData[P_osc2SawLevel] = value;
        }
      }
      osc2SawLevelstr = value / midioutfrig;  // for display
      updateOsc2SawLevel(1);
      break;

    case CCosc1SawLevel:
      if (upperSW) {
        upperData[P_osc1SawLevel] = value;
      } else {
        lowerData[P_osc1SawLevel] = value;
        if (wholemode) {
          upperData[P_osc1SawLevel] = value;
        }
      }
      osc1SawLevelstr = value / midioutfrig;  // for display
      updateOsc1SawLevel(1);
      break;

    case CCosc2PulseLevel:
      if (upperSW) {
        upperData[P_osc2PulseLevel] = value;
      } else {
        lowerData[P_osc2PulseLevel] = value;
        if (wholemode) {
          upperData[P_osc2PulseLevel] = value;
        }
      }
      osc2PulseLevelstr = value / midioutfrig;  // for display
      updateOsc2PulseLevel(1);
      break;

    case CCosc1PulseLevel:
      if (upperSW) {
        upperData[P_osc1PulseLevel] = value;
      } else {
        lowerData[P_osc1PulseLevel] = value;
        if (wholemode) {
          upperData[P_osc1PulseLevel] = value;
        }
      }
      osc1PulseLevelstr = value / midioutfrig;  // for display
      updateOsc1PulseLevel(1);
      break;

    case CCosc2TriangleLevel:
      if (upperSW) {
        upperData[P_osc2TriangleLevel] = value;
      } else {
        lowerData[P_osc2TriangleLevel] = value;
        if (wholemode) {
          upperData[P_osc2TriangleLevel] = value;
        }
      }
      osc2TriangleLevelstr = value / midioutfrig;  // for display
      updateOsc2TriangleLevel(1);
      break;

    case CCosc1SubLevel:
      if (upperSW) {
        upperData[P_osc1SubLevel] = value;
      } else {
        lowerData[P_osc1SubLevel] = value;
        if (wholemode) {
          upperData[P_osc1SubLevel] = value;
        }
      }
      osc1SubLevelstr = value / midioutfrig;  // for display
      updateOsc1SubLevel(1);
      break;

    case CCLFODelay:
      if (upperSW) {
        upperData[P_LFODelay] = value;
      } else {
        lowerData[P_LFODelay] = value;
        if (wholemode) {
          upperData[P_LFODelay] = value;
        }
      }
      LFODelaystr = value / midioutfrig;  // for display
      updateLFODelay(1);
      break;

    case CCfilterCutoff:
      if (upperSW) {
        upperData[P_filterCutoff] = value;
        oldfilterCutoffU = value;
      } else {
        lowerData[P_filterCutoff] = value;
        oldfilterCutoffL = value;
        if (wholemode) {
          upperData[P_filterCutoff] = value;
          oldfilterCutoffU = value;
        }
      }
      filterCutoffstr = FILTERCUTOFF[value / midioutfrig];
      updateFilterCutoff(1);
      break;

    case CCfilterLFO:
      if (upperSW) {
        upperData[P_filterLFO] = value;
      } else {
        lowerData[P_filterLFO] = value;
        if (wholemode) {
          upperData[P_filterLFO] = value;
        }
      }
      filterLFOstr = value / midioutfrig;
      updatefilterLFO(1);
      break;

    case CCfilterRes:
      if (upperSW) {
        upperData[P_filterRes] = value;
      } else {
        lowerData[P_filterRes] = value;
        if (wholemode) {
          upperData[P_filterRes] = value;
        }
      }
      filterResstr = int(value / midioutfrig);
      updatefilterRes(1);
      break;

    case CCfilterType:
      if (upperSW) {
        upperData[P_filterType] = value >> 7;
      } else {
        lowerData[P_filterType] = value >> 7;
        if (wholemode) {
          upperData[P_filterType] = value >> 7;
        }
      }
      updateFilterType(1);
      break;

    case CCfilterEGlevel:
      if (upperSW) {
        upperData[P_filterEGlevel] = value;
      } else {
        lowerData[P_filterEGlevel] = value;
        if (wholemode) {
          upperData[P_filterEGlevel] = value;
        }
      }
      filterEGlevelstr = int(value / midioutfrig);
      updatefilterEGlevel(1);
      break;

    case CCLFORate:
      if (upperSW) {
        upperData[P_LFORate] = value;
      } else {
        lowerData[P_LFORate] = value;
        if (wholemode) {
          upperData[P_LFORate] = value;
        }
      }
      LFORatestr = LFOTEMPO[value / midioutfrig];  // for display
      updateLFORate(1);
      break;

    case CCLFOWaveform:
      if (upperSW) {
        upperData[P_LFOWaveform] = value;
      } else {
        lowerData[P_LFOWaveform] = value;
        if (wholemode) {
          upperData[P_LFOWaveform] = value;
        }
      }
      updateStratusLFOWaveform(1);
      break;

    case CCfilterAttack:
      if (upperSW) {
        upperData[P_filterAttack] = value;
      } else {
        lowerData[P_filterAttack] = value;
        if (wholemode) {
          upperData[P_filterAttack] = value;
        }
      }
      filterAttackstr = ENVTIMES[value / midioutfrig];
      updatefilterAttack(1);
      break;

    case CCfilterDecay:
      if (upperSW) {
        upperData[P_filterDecay] = value;
      } else {
        lowerData[P_filterDecay] = value;
        if (wholemode) {
          upperData[P_filterDecay] = value;
        }
      }
      filterDecaystr = ENVTIMES[value / midioutfrig];
      updatefilterDecay(1);
      break;

    case CCfilterSustain:
      if (upperSW) {
        upperData[P_filterSustain] = value;
      } else {
        lowerData[P_filterSustain] = value;
        if (wholemode) {
          upperData[P_filterSustain] = value;
        }
      }
      filterSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updatefilterSustain(1);
      break;

    case CCfilterRelease:
      if (upperSW) {
        upperData[P_filterRelease] = value;
      } else {
        lowerData[P_filterRelease] = value;
        if (wholemode) {
          upperData[P_filterRelease] = value;
        }
      }
      filterReleasestr = ENVTIMES[value / midioutfrig];
      updatefilterRelease(1);
      break;

    case CCampAttack:
      if (upperSW) {
        upperData[P_ampAttack] = value;
        upperData[P_oldampAttack] = value;
      } else {
        lowerData[P_ampAttack] = value;
        lowerData[P_oldampAttack] = value;
        if (wholemode) {
          upperData[P_ampAttack] = value;
          upperData[P_oldampAttack] = value;
        }
      }
      ampAttackstr = ENVTIMES[value / midioutfrig];
      updateampAttack(1);
      break;

    case CCampDecay:
      if (upperSW) {
        upperData[P_ampDecay] = value;
        upperData[P_oldampDecay] = value;
      } else {
        lowerData[P_ampDecay] = value;
        lowerData[P_oldampDecay] = value;
        if (wholemode) {
          upperData[P_ampDecay] = value;
          upperData[P_oldampDecay] = value;
        }
      }
      ampDecaystr = ENVTIMES[value / midioutfrig];
      updateampDecay(1);
      break;

    case CCampSustain:
      if (upperSW) {
        upperData[P_ampSustain] = value;
        upperData[P_oldampSustain] = value;
      } else {
        lowerData[P_ampSustain] = value;
        lowerData[P_oldampSustain] = value;
        if (wholemode) {
          upperData[P_ampSustain] = value;
          upperData[P_oldampSustain] = value;
        }
      }
      ampSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updateampSustain(1);
      break;

    case CCampRelease:
      if (upperSW) {
        upperData[P_ampRelease] = value;
        upperData[P_oldampRelease] = value;
      } else {
        lowerData[P_ampRelease] = value;
        lowerData[P_oldampRelease] = value;
        if (wholemode) {
          upperData[P_ampRelease] = value;
          upperData[P_oldampRelease] = value;
        }
      }
      ampReleasestr = ENVTIMES[value / midioutfrig];
      updateampRelease(1);
      break;

    case CCvolumeControl:
      if (upperSW) {
        upperData[P_volumeControl] = value;
      } else {
        lowerData[P_volumeControl] = value;
        if (wholemode) {
          upperData[P_volumeControl] = value;
        }
      }
      volumeControlstr = value / midioutfrig;
      updatevolumeControl(1);
      break;

    case CCkeyTrack:
      if (upperSW) {
        upperData[P_keytrack] = value;
      } else {
        lowerData[P_keytrack] = value;
        if (wholemode) {
          upperData[P_keytrack] = value;
        }
      }
      keytrackstr = value / midioutfrig;
      updatekeytrack(1);
      break;


    case CCamDepth:
      if (upperSW) {
        upperData[P_amDepth] = value;
      } else {
        lowerData[P_amDepth] = value;
        if (wholemode) {
          upperData[P_amDepth] = value;
        }
      }
      amDepth = value;
      amDepthstr = value / midioutfrig;
      updateamDepth(1);
      break;

      ////////////////////////////////////////////////

    case CCglideSW:
      if (upperSW) {
        upperData[P_glideSW] = !upperData[P_glideSW];
      } else {
        lowerData[P_glideSW] = !lowerData[P_glideSW];
      }
      updateglideSW(1);
      break;

    case CCfilterPoleSW:
      if (upperSW) {
        upperData[P_filterPoleSW] = !upperData[P_filterPoleSW];
      } else {
        lowerData[P_filterPoleSW] = !lowerData[P_filterPoleSW];
      }
      updatefilterPoleSwitch(1);
      break;

    case CCfilterVel:
      if (upperSW) {
        upperData[P_filterVel] = !upperData[P_filterVel];
      } else {
        lowerData[P_filterVel] = !lowerData[P_filterVel];
      }
      updatefilterVel(1);
      break;

    case CCfilterEGinv:
      if (upperSW) {
        upperData[P_filterEGinv] = !upperData[P_filterEGinv];
      } else {
        lowerData[P_filterEGinv] = !lowerData[P_filterEGinv];
      }
      updatefilterEGinv(1);
      break;

    case CCfilterLoop:
      if (upperSW) {
        upperData[P_statefilterLoop] = statefilterLoop;
      } else {
        lowerData[P_statefilterLoop] = statefilterLoop;
      }
      updatefilterLoop(1);
      break;

    case CCvcaLoop:
      if (upperSW) {
        upperData[P_statevcaLoop] = statevcaLoop;
      } else {
        lowerData[P_statevcaLoop] = statevcaLoop;
      }
      updatevcaLoop(1);
      break;

    case CCvcaVel:
      if (upperSW) {
        upperData[P_vcaVel] = !upperData[P_vcaVel];
      } else {
        lowerData[P_vcaVel] = !lowerData[P_vcaVel];
      }
      updatevcaVel(1);
      break;

    case CCvcaGate:
      if (upperSW) {
        upperData[P_vcaGate] = !upperData[P_vcaGate];
      } else {
        lowerData[P_vcaGate] = !lowerData[P_vcaGate];
      }
      updatevcaGate(1);
      break;

    case CCchorus1:
      if (upperSW) {
        upperData[P_chorus1] = !upperData[P_chorus1];
      } else {
        lowerData[P_chorus1] = !lowerData[P_chorus1];
      }
      updatechorus1(1);
      break;

    case CCchorus2:
      if (upperSW) {
        upperData[P_chorus2] = !upperData[P_chorus2];
      } else {
        lowerData[P_chorus2] = !lowerData[P_chorus2];
      }
      updatechorus2(1);
      break;

    case CCwholemode:
      wholemode = true;
      updatewholemode(1);
      break;

    case CCdualmode:
      dualmode = true;
      updatedualmode(1);
      break;

    case CCsplitmode:
      splitmode = true;
      updatesplitmode(1);
      break;


    case CCmonoMulti:
      value > 0 ? monoMulti = 1 : monoMulti = 0;
      updateMonoMulti(1);
      break;

      // case CCPBDepth:
      //   PitchBendLevel = value;
      //   PitchBendLevelstr = PITCHBEND[value / midioutfrig];  // for display
      //   updatePitchBend();
      //   break;

    case CClfoAlt:
      if (upperSW) {
        upperData[P_lfoAlt] = !upperData[P_lfoAlt];
      } else {
        lowerData[P_lfoAlt] = !lowerData[P_lfoAlt];
      }
      updatelfoAlt(1);
      break;

    case CCupperSW:
      upperSW = value;
      updateupperLower();
      break;

    case CCmodwheel:
      value = (value * MIDICCTOPOT);
      switch (modWheelDepth) {
        case 1:
          modWheelLevel = ((value) / 5);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 2:
          modWheelLevel = ((value) / 4);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 3:
          modWheelLevel = ((value) / 3.5);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 4:
          modWheelLevel = ((value) / 3);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 5:
          modWheelLevel = ((value) / 2.5);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 6:
          modWheelLevel = ((value) / 2);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 7:
          modWheelLevel = ((value) / 1.75);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 8:
          modWheelLevel = ((value) / 1.5);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 9:
          modWheelLevel = ((value) / 1.25);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;

        case 10:
          modWheelLevel = (value);
          upperData[P_fmDepth] = int(modWheelLevel);
          lowerData[P_fmDepth] = int(modWheelLevel);
          break;
      }
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  if (inPerformanceMode) {
    if (program < performances.size()) {
      performanceIndex = program;
      currentPerformance = performances[performanceIndex];

      // Update play mode flags
      playMode = currentPerformance.mode;
      wholemode = (playMode == WHOLE);
      dualmode = (playMode == DUAL);
      splitmode = (playMode == SPLIT);
      updateplayMode(0);

      // Apply split settings
      newsplitPoint = currentPerformance.newsplitPoint;
      splitTrans = currentPerformance.splitTrans;

      // Set patch indices
      for (int i = 0; i < patches.size(); i++) {
        if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
        if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
      }

      // Recall both patches
      upperSW = true;
      recallPatch(currentPerformance.upperPatchNo);
      upperSW = false;
      recallPatch(currentPerformance.lowerPatchNo);

      if (wholemode) {
        patchNoU = patchNoL;
        patchNameU = patchNameL;
        currentPatchNameU = currentPatchNameL;
        currentPgmNumU = currentPgmNumL;
        memcpy(upperData, lowerData, sizeof(upperData));
      }

      refreshPatchDisplayFromState();

      // ✅ Update the performance display
      showPerformancePage(
        String(currentPerformance.performanceNo),
        currentPerformance.name,
        currentPerformance.upperPatchNo,
        getPatchName(currentPerformance.upperPatchNo),
        currentPerformance.lowerPatchNo,
        getPatchName(currentPerformance.lowerPatchNo));
    }
  } else {
    // Normal patch recall
    state = PATCH;
    patchNo = program + 1;
    recallPatch(patchNo);
    state = PARAMETER;
  }
}


void myAfterTouch(byte channel, byte value) {
  afterTouch = int(value * MIDICCTOPOT);
  switch (upperData[P_AfterTouchDest]) {
    case 1:
      upperData[P_fmDepth] = afterTouch;
      break;
    case 2:
      upperData[P_filterCutoff] = (oldfilterCutoffU + afterTouch);
      if (afterTouch < 10) {
        upperData[P_filterCutoff] = oldfilterCutoffU;
      }
      if (upperData[P_filterCutoff] > 1023) {
        upperData[P_filterCutoff] = 1023;
      }
      break;
    case 3:
      upperData[P_filterLFO] = afterTouch;
      break;
    case 4:
      upperData[P_amDepth] = afterTouch;
      break;
  }
  switch (lowerData[AfterTouchDest]) {
    case 1:
      lowerData[P_fmDepth] = afterTouch;
      break;
    case 2:
      lowerData[P_filterCutoff] = (oldfilterCutoffL + afterTouch);
      if (afterTouch < 10) {
        lowerData[P_filterCutoff] = oldfilterCutoffL;
      }
      if (lowerData[P_filterCutoff] > 1023) {
        lowerData[P_filterCutoff] = 1023;
      }
      break;
    case 3:
      lowerData[P_filterLFO] = afterTouch;
      break;
    case 4:
      lowerData[P_amDepth] = afterTouch;
      break;
  }
}

void recallPatch(int patchNo) {
  //allNotesOff();

  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];
    recallPatchData(patchFile, data);
    patchFile.close();

    // Find matching patch in the circular buffer to set name and number
    for (int i = 0; i < patches.size(); i++) {

      if (patches[i].patchNo == patchNo) {
        if (upperSW) {
          upperPatchIndex = i;
          currentPgmNumU = String(patches[i].patchNo);
          currentPatchNameU = patches[i].patchName;
        } else {
          lowerPatchIndex = i;
          currentPgmNumL = String(patches[i].patchNo);
          currentPatchNameL = patches[i].patchName;
        }

        break;
      }
    }

    setCurrentPatchData(data);
  }
}

void setCurrentPatchData(String data[]) {
  int tempData[65];  // Temporary array for converted integers

  // Convert data from String to int once
  for (int i = 1; i <= 64; i++) {
    tempData[i] = data[i].toInt();
  }

  if (upperSW) {
    patchNameU = data[0];
    tempData[0] = 1;
    memcpy(upperData, tempData, sizeof(tempData));

    // Update previous values and pick-up flags
    for (int i = 1; i <= 64; i++) {
      prevUpperData[i] = upperData[i];  // Store previous value
      upperPickUp[i] = true;            // Enable pick-up flag
    }

    oldfilterCutoffU = upperData[P_filterCutoff];
    upperParamsToDisplay();
    setAllButtons();
  } else {
    patchNameL = data[0];
    tempData[0] = 1;
    memcpy(lowerData, tempData, sizeof(tempData));

    // Update previous values and pick-up flags
    for (int i = 1; i <= 64; i++) {
      prevLowerData[i] = lowerData[i];  // Store previous value
      lowerPickUp[i] = true;            // Enable pick-up flag
    }

    oldfilterCutoffL = lowerData[P_filterCutoff];
    lowerParamsToDisplay();
    setAllButtons();

    if (wholemode) {

      // Update previous values and pick-up flags
      for (int i = 1; i <= 64; i++) {
        upperData[i] = lowerData[i];  // Store previous value
        //upperPickUp[i] = true;            // Enable pick-up flag
      }

      oldfilterCutoffU = upperData[P_filterCutoff];
      upperParamsToDisplay();
      setAllButtons();
    }
  }

  updatePatchname();
}

void upperParamsToDisplay() {
  updateglideTime(0);
  updateOsc1PW(0);
  updateOsc1PWM(0);
  updateOsc1SawLevel(0);
  updateOsc1PulseLevel(0);
  updateOsc1SubLevel(0);
  updatefmDepth(0);
  updateOsc2PW(0);
  updateOsc2PWM(0);
  updateOsc2SawLevel(0);
  updateOsc2PulseLevel(0);
  updateOsc2TriangleLevel(0);
  updateOsc2Detune(0);
  updateFilterCutoff(0);
  updatefilterRes(0);
  updatefilterEGlevel(0);
  updatekeytrack(0);
  updatefilterLFO(0);
  updatefilterAttack(0);
  updatefilterDecay(0);
  updatefilterSustain(0);
  updatefilterRelease(0);
  updateampAttack(0);
  updateampDecay(0);
  updateampSustain(0);
  updateampRelease(0);
  updateLFORate(0);
  updateLFODelay(0);
  updatepwLFO(0);
  updatenoiseLevel(0);
  updatevolumeControl(0);
  updateamDepth(0);
  updateOsc1Range(0);
  updateOsc2Range(0);

  //updateStratusLFOWaveform(0);
}

void lowerParamsToDisplay() {
  updateglideTime(0);
  updateOsc1PW(0);
  updateOsc1PWM(0);
  updateOsc1SawLevel(0);
  updateOsc1PulseLevel(0);
  updateOsc1SubLevel(0);
  updatefmDepth(0);
  updateOsc2PW(0);
  updateOsc2PWM(0);
  updateOsc2SawLevel(0);
  updateOsc2PulseLevel(0);
  updateOsc2TriangleLevel(0);
  updateOsc2Detune(0);
  updateFilterCutoff(0);
  updatefilterRes(0);
  updatefilterEGlevel(0);
  updatekeytrack(0);
  updatefilterLFO(0);
  updatefilterAttack(0);
  updatefilterDecay(0);
  updatefilterSustain(0);
  updatefilterRelease(0);
  updateampAttack(0);
  updateampDecay(0);
  updateampSustain(0);
  updateampRelease(0);
  updateLFORate(0);
  updateLFODelay(0);
  updatepwLFO(0);
  updatenoiseLevel(0);
  updatevolumeControl(0);
  updateamDepth(0);
  updateOsc1Range(0);
  updateOsc2Range(0);

  //updateStratusLFOWaveform(0);
}

void setAllButtons() {
  updatefilterPoleSwitch(0);
  updatefilterLoop(0);
  updatefilterEGinv(0);
  updatefilterVel(0);
  updatevcaLoop(0);
  updatevcaVel(0);
  updatevcaGate(0);
  updatelfoAlt(0);
  updatechorus1(0);
  updatechorus2(0);
  updateglideSW(0);
}

String getCurrentPatchData() {
  if (upperSW) {
    return patchNameU + "," + String(upperData[P_pwLFO]) + "," + String(upperData[P_fmDepth]) + "," + String(upperData[P_osc2PW]) + "," + String(upperData[P_osc2PWM]) + "," + String(upperData[P_osc1PW]) + "," + String(upperData[P_osc1PWM]) + "," + String(upperData[P_osc1Range])
           + "," + String(upperData[P_osc2Range]) + "," + String(upperData[P_stack]) + "," + String(upperData[P_glideTime]) + "," + String(upperData[P_osc2Detune]) + "," + String(upperData[P_noiseLevel]) + "," + String(upperData[P_osc2SawLevel]) + "," + String(upperData[P_osc1SawLevel])
           + "," + String(upperData[P_osc2PulseLevel]) + "," + String(upperData[P_osc1PulseLevel]) + "," + String(upperData[P_filterCutoff]) + "," + String(upperData[P_filterLFO]) + "," + String(upperData[P_filterRes]) + "," + String(upperData[P_filterType]) + "," + String(upperData[P_filterdoubleLoop])
           + "," + String(upperData[P_vcadoubleLoop]) + "," + String(upperData[P_LFODelayGo]) + "," + String(upperData[P_filterEGlevel]) + "," + String(upperData[P_LFORate]) + "," + String(upperData[P_LFOWaveform]) + "," + String(upperData[P_filterAttack]) + "," + String(upperData[P_filterDecay])
           + "," + String(upperData[P_filterSustain]) + "," + String(upperData[P_filterRelease]) + "," + String(upperData[P_ampAttack]) + "," + String(upperData[P_ampDecay]) + "," + String(upperData[P_ampSustain]) + "," + String(upperData[P_ampRelease]) + "," + String(upperData[P_volumeControl])
           + "," + String(upperData[P_glideSW]) + "," + String(upperData[P_keytrack]) + "," + String(upperData[P_filterPoleSW]) + "," + String(upperData[P_filterLoop]) + "," + String(upperData[P_filterEGinv]) + "," + String(upperData[P_filterVel]) + "," + String(upperData[P_vcaLoop])
           + "," + String(upperData[P_vcaVel]) + "," + String(upperData[P_vcaGate]) + "," + String(upperData[P_lfoAlt]) + "," + String(upperData[P_chorus1]) + "," + String(upperData[P_chorus2]) + "," + String(upperData[P_monoMulti]) + "," + String(upperData[P_modWheelLevel]) + "," + String(upperData[P_PitchBendLevel])
           + "," + String(upperData[P_amDepth]) + "," + String(upperData[P_oldampAttack]) + "," + String(upperData[P_oldampDecay]) + "," + String(upperData[P_oldampSustain]) + "," + String(upperData[P_oldampRelease]) + "," + String(upperData[P_AfterTouchDest]) + "," + String(upperData[P_filterLogLin])
           + "," + String(upperData[P_ampLogLin]) + "," + String(upperData[P_osc2TriangleLevel]) + "," + String(upperData[P_osc1SubLevel]) + "," + String(upperData[P_keyTrackSW]) + "," + String(upperData[P_LFODelay])
           + "," + String(upperData[P_statefilterLoop]) + "," + String(upperData[P_statevcaLoop]);
  } else {
    return patchNameL + "," + String(lowerData[P_pwLFO]) + "," + String(lowerData[P_fmDepth]) + "," + String(lowerData[P_osc2PW]) + "," + String(lowerData[P_osc2PWM]) + "," + String(lowerData[P_osc1PW]) + "," + String(lowerData[P_osc1PWM]) + "," + String(lowerData[P_osc1Range])
           + "," + String(lowerData[P_osc2Range]) + "," + String(lowerData[P_stack]) + "," + String(lowerData[P_glideTime]) + "," + String(lowerData[P_osc2Detune]) + "," + String(lowerData[P_noiseLevel]) + "," + String(lowerData[P_osc2SawLevel]) + "," + String(lowerData[P_osc1SawLevel])
           + "," + String(lowerData[P_osc2PulseLevel]) + "," + String(lowerData[P_osc1PulseLevel]) + "," + String(lowerData[P_filterCutoff]) + "," + String(lowerData[P_filterLFO]) + "," + String(lowerData[P_filterRes]) + "," + String(lowerData[P_filterType]) + "," + String(lowerData[P_filterdoubleLoop])
           + "," + String(lowerData[P_vcadoubleLoop]) + "," + String(lowerData[P_LFODelayGo]) + "," + String(lowerData[P_filterEGlevel]) + "," + String(lowerData[P_LFORate]) + "," + String(lowerData[P_LFOWaveform]) + "," + String(lowerData[P_filterAttack]) + "," + String(lowerData[P_filterDecay])
           + "," + String(lowerData[P_filterSustain]) + "," + String(lowerData[P_filterRelease]) + "," + String(lowerData[P_ampAttack]) + "," + String(lowerData[P_ampDecay]) + "," + String(lowerData[P_ampSustain]) + "," + String(lowerData[P_ampRelease]) + "," + String(lowerData[P_volumeControl])
           + "," + String(lowerData[P_glideSW]) + "," + String(lowerData[P_keytrack]) + "," + String(lowerData[P_filterPoleSW]) + "," + String(lowerData[P_filterLoop]) + "," + String(lowerData[P_filterEGinv]) + "," + String(lowerData[P_filterVel]) + "," + String(lowerData[P_vcaLoop])
           + "," + String(lowerData[P_vcaVel]) + "," + String(lowerData[P_vcaGate]) + "," + String(lowerData[P_lfoAlt]) + "," + String(lowerData[P_chorus1]) + "," + String(lowerData[P_chorus2]) + "," + String(lowerData[P_monoMulti]) + "," + String(lowerData[P_modWheelLevel]) + "," + String(lowerData[P_PitchBendLevel])
           + "," + String(lowerData[P_amDepth]) + "," + String(lowerData[P_oldampAttack]) + "," + String(lowerData[P_oldampDecay]) + "," + String(lowerData[P_oldampSustain]) + "," + String(lowerData[P_oldampRelease]) + "," + String(lowerData[P_AfterTouchDest]) + "," + String(lowerData[P_filterLogLin])
           + "," + String(lowerData[P_ampLogLin]) + "," + String(lowerData[P_osc2TriangleLevel]) + "," + String(lowerData[P_osc1SubLevel]) + "," + String(lowerData[P_keyTrackSW]) + "," + String(lowerData[P_LFODelay])
           + "," + String(lowerData[P_statefilterLoop]) + "," + String(lowerData[P_statevcaLoop]);
  }
}

void checkMux() {

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);


  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    switch (muxInput) {
      case MUX1_osc1PW:
        myControlChange(midiChannel, CCosc1PW, mux1Read);
        break;
      case MUX1_spare0:
        break;
      case MUX1_osc2PWM:
        myControlChange(midiChannel, CCosc2PWM, mux1Read);
        break;
      case MUX1_osc2PW:
        myControlChange(midiChannel, CCosc2PW, mux1Read);
        break;
      case MUX1_spare4:
        break;
      case MUX1_osc1PWM:
        myControlChange(midiChannel, CCosc1PWM, mux1Read);
        break;
      case MUX1_pwLFO:
        myControlChange(midiChannel, CCpwLFO, mux1Read);
        break;
      case MUX1_fmDepth:
        myControlChange(midiChannel, CCfmDepth, mux1Read);
        break;
      case MUX1_osc2SawLevel:
        myControlChange(midiChannel, CCosc2SawLevel, mux1Read);
        break;
      case MUX1_noiseLevel:
        myControlChange(midiChannel, CCnoiseLevel, mux1Read);
        break;
      case MUX1_osc1SawLevel:
        myControlChange(midiChannel, CCosc1SawLevel, mux1Read);
        break;
      case MUX1_osc2Detune:
        myControlChange(midiChannel, CCosc2Detune, mux1Read);
        break;
      case MUX1_osc1Range:
        myControlChange(midiChannel, CCosc1Range, mux1Read);
        break;
      case MUX1_glideTime:
        myControlChange(midiChannel, CCglideTime, mux1Read);
        break;
      case MUX1_osc2Range:
        myControlChange(midiChannel, CCosc2Range, mux1Read);
        break;
      case MUX1_stack:
        myControlChange(midiChannel, CCstack, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;
    switch (muxInput) {
      case MUX2_osc2TriangleLevel:
        myControlChange(midiChannel, CCosc2TriangleLevel, mux2Read);
        break;
      case MUX2_filterLFO:
        myControlChange(midiChannel, CCfilterLFO, mux2Read);
        break;
      case MUX2_filterCutoff:
        myControlChange(midiChannel, CCfilterCutoff, mux2Read);
        break;
      case MUX2_LFOWaveform:
        myControlChange(midiChannel, CCLFOWaveform, mux2Read);
        break;
      case MUX2_osc1PulseLevel:
        myControlChange(midiChannel, CCosc1PulseLevel, mux2Read);
        break;
      case MUX2_LFODelay:
        myControlChange(midiChannel, CCLFODelay, mux2Read);
        break;
      case MUX2_osc2PulseLevel:
        myControlChange(midiChannel, CCosc2PulseLevel, mux2Read);
        break;
      case MUX2_osc1SubLevel:
        myControlChange(midiChannel, CCosc1SubLevel, mux2Read);
        break;
      case MUX2_LFORate:
        myControlChange(midiChannel, CCLFORate, mux2Read);
        break;
      case MUX2_filterDecay:
        myControlChange(midiChannel, CCfilterDecay, mux2Read);
        break;
      case MUX2_filterAttack:
        myControlChange(midiChannel, CCfilterAttack, mux2Read);
        break;
      case MUX2_ampAttack:
        myControlChange(midiChannel, CCampAttack, mux2Read);
        break;
      case MUX2_filterRes:
        myControlChange(midiChannel, CCfilterRes, mux2Read);
        break;
      case MUX2_filterType:
        myControlChange(midiChannel, CCfilterType, mux2Read);
        break;
      case MUX2_keyTrack:
        myControlChange(midiChannel, CCkeyTrack, mux2Read);
        break;
      case MUX2_filterEGlevel:
        myControlChange(midiChannel, CCfilterEGlevel, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;
    switch (muxInput) {
      case MUX3_amDepth:
        myControlChange(midiChannel, CCamDepth, mux3Read);
        break;
      case MUX3_ampSustain:
        myControlChange(midiChannel, CCampSustain, mux3Read);
        break;
      case MUX3_ampDecay:
        myControlChange(midiChannel, CCampDecay, mux3Read);
        break;
      case MUX3_volumeControl:
        myControlChange(midiChannel, CCvolumeControl, mux3Read);
        break;
      case MUX3_filterSustain:
        myControlChange(midiChannel, CCfilterSustain, mux3Read);
        break;
      case MUX3_filterRelease:
        myControlChange(midiChannel, CCfilterRelease, mux3Read);
        break;
      case MUX3_ampRelease:
        myControlChange(midiChannel, CCampRelease, mux3Read);
        break;
    }
  }

  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWrite(MUX_0, muxInput & B0001);
  digitalWrite(MUX_1, muxInput & B0010);
  digitalWrite(MUX_2, muxInput & B0100);
  digitalWrite(MUX_3, muxInput & B1000);
}

void midiCCOut(byte cc, byte value) {
  MIDI.sendControlChange(cc, value, midiChannel);  //MIDI DIN is set to Out
}

void midiCCOutCPU2(byte cc, byte value, byte channel) {
  MIDI2.sendControlChange(cc, value, channel);  //MIDI DIN is set to Out
}

void midiCCOutCPU5(byte cc, byte value, byte channel) {
  MIDI5.sendControlChange(cc, value, channel);  //MIDI DIN is set to Out
}


void outputDAC(int CHIP_SELECT, uint32_t sample_data1, uint32_t sample_data2, uint32_t sample_data3, uint32_t sample_data4) {
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE1));
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data1);
  digitalWriteFast(CHIP_SELECT, HIGH);
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data2);
  digitalWriteFast(CHIP_SELECT, HIGH);
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data3);
  digitalWriteFast(CHIP_SELECT, HIGH);
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data4);
  digitalWriteFast(CHIP_SELECT, HIGH);
  SPI.endTransaction();
}

void writeDemux() {

  switch (muxOutput) {
    case 0:
      switch (upperData[P_LFODelayGo]) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_fmDepth] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (lowerData[P_LFODelayGo]) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_fmDepth] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterAttack] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterAttack] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 1:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc2PWM] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc2PWM] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterDecay] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterDecay] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 2:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc1PWM] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc1PWM] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterSustain] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterSustain] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 3:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_stack] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_stack] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterRelease] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterRelease] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 4:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc2Detune] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc2Detune] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_ampAttack] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_ampAttack] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 5:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_noiseLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_noiseLevel] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_ampDecay] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_ampDecay] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 6:
      switch (upperData[P_LFODelayGo]) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_filterLFO] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (lowerData[P_LFODelayGo]) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_filterLFO] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_ampSustain] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_ampSustain] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 7:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_volumeControl] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_volumeControl] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_ampRelease] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_ampRelease] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 8:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc1SawLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc1SawLevel] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_pwLFO] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_pwLFO] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 9:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc1PulseLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc1PulseLevel] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_LFORate] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_LFORate] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 10:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc2SawLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc2SawLevel] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(LFOWaveCVupper * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(LFOWaveCVlower * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 11:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc2PulseLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc2PulseLevel] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterEGlevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterEGlevel] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 12:  // was keytrack, but now moved to MIDI
      sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterCutoff] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterCutoff] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 13:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc1PW] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc1PW] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_filterRes] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_filterRes] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 14:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_osc2PW] * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_osc2PW] * DACMULT)) & 0xFFFF) << 4);

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_osc1SubLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_osc1SubLevel] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;

    case 15:
      switch (upperData[P_LFODelayGo]) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(upperData[P_amDepth] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (lowerData[P_LFODelayGo]) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(lowerData[P_amDepth] * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }

      sample_data3 = (channel_c & 0xFFF0000F) | (((int(upperData[P_osc2TriangleLevel] * DACMULT)) & 0xFFFF) << 4);
      sample_data4 = (channel_d & 0xFFF0000F) | (((int(lowerData[P_osc2TriangleLevel] * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2, sample_data3, sample_data4);
      digitalWriteFast(DEMUX_EN_1, LOW);
      break;
  }
  delayMicroseconds(10);
  digitalWriteFast(DEMUX_EN_1, HIGH);

  muxOutput++;
  if (muxOutput >= DEMUXCHANNELS)

    muxOutput = 0;

  digitalWriteFast(DEMUX_0, muxOutput & B0001);
  digitalWriteFast(DEMUX_1, muxOutput & B0010);
  digitalWriteFast(DEMUX_2, muxOutput & B0100);
  digitalWriteFast(DEMUX_3, muxOutput & B1000);
}

// Dummy setPerformancesOrdering
void setPerformancesOrdering(uint16_t no) {
  // Optional: reorder performances if needed
}

void checkEeprom() {

  if (oldsplitTrans != splitTrans) {
    setTranspose(splitTrans);
  }

  if (oldfilterLogLinU != upperData[P_filterLogLin]) {
    updateFilterEnv(0);
    oldfilterLogLinU = upperData[P_filterLogLin];
  }

  if (oldfilterLogLinL != lowerData[P_filterLogLin]) {
    updateFilterEnv(0);
    oldfilterLogLinL = lowerData[P_filterLogLin];
  }

  if (oldampLogLinU != upperData[P_ampLogLin]) {
    updateAmpEnv(0);
    oldampLogLinU = upperData[P_ampLogLin];
  }

  if (oldampLogLinL != lowerData[P_ampLogLin]) {
    updateAmpEnv(0);
    oldampLogLinL = lowerData[P_ampLogLin];
  }

  if (oldkeyTrackSWU != upperData[P_keyTrackSW]) {
    updatekeyTrackSW(0);
    oldkeyTrackSWU = upperData[P_keyTrackSW];
  }

  if (oldkeyTrackSWL != lowerData[P_keyTrackSW]) {
    updatekeyTrackSW(0);
    oldkeyTrackSWL = lowerData[P_keyTrackSW];
  }

  if (oldmonoMultiU != upperData[P_monoMulti]) {
    updateMonoMulti(0);
    oldmonoMultiU = upperData[P_monoMulti];
  }

  if (oldmonoMultiL != lowerData[P_monoMulti]) {
    updateMonoMulti(0);
    oldmonoMultiL = lowerData[P_monoMulti];
  }

  if (oldAfterTouchDestU != upperData[P_AfterTouchDest]) {
    oldAfterTouchDestU = upperData[P_AfterTouchDest];
  }

  if (oldAfterTouchDestL != lowerData[P_AfterTouchDest]) {
    oldAfterTouchDestL = lowerData[P_AfterTouchDest];
  }
}

void onButtonPress(uint16_t btnIndex, uint8_t btnType) {

  // to check if a specific button was pressed

  if (btnIndex == CHORUS1_SW && btnType == ROX_PRESSED) {
    chorus1 = !chorus1;
    myControlChange(midiChannel, CCchorus1, chorus1);
  }

  if (btnIndex == CHORUS2_SW && btnType == ROX_PRESSED) {
    chorus2 = !chorus2;
    myControlChange(midiChannel, CCchorus2, chorus2);
  }

  if (btnIndex == GLIDE_SW && btnType == ROX_PRESSED) {
    glideSW = !glideSW;
    myControlChange(midiChannel, CCglideSW, glideSW);
  }

  if (btnIndex == VCALOOP_SW && btnType == ROX_PRESSED) {
    statevcaLoop = oldvcaLoop + 1;
    myControlChange(midiChannel, CCvcaLoop, statevcaLoop);
  }

  if (btnIndex == FILTERLOOP_SW && btnType == ROX_PRESSED) {
    statefilterLoop = oldfilterLoop + 1;
    myControlChange(midiChannel, CCfilterLoop, statefilterLoop);
  }

  if (btnIndex == VCAGATE_SW && btnType == ROX_PRESSED) {
    vcaGate = !vcaGate;
    myControlChange(midiChannel, CCvcaGate, vcaGate);
  }

  if (btnIndex == VCAVEL_SW && btnType == ROX_PRESSED) {
    vcaVel = !vcaVel;
    myControlChange(midiChannel, CCvcaVel, vcaVel);
  }

  if (btnIndex == LFO_ALT_SW && btnType == ROX_PRESSED) {
    lfoAlt = !lfoAlt;
    myControlChange(midiChannel, CClfoAlt, lfoAlt);
  }

  if (btnIndex == FILTERPOLE_SW && btnType == ROX_PRESSED) {
    filterPoleSW = !filterPoleSW;
    myControlChange(midiChannel, CCfilterPoleSW, filterPoleSW);
  }

  if (btnIndex == FILTERINV_SW && btnType == ROX_PRESSED) {
    filterEGinv = !filterEGinv;
    myControlChange(midiChannel, CCfilterEGinv, filterEGinv);
  }

  if (btnIndex == FILTERVEL_SW && btnType == ROX_PRESSED) {
    filterVel = !filterVel;
    myControlChange(midiChannel, CCfilterVel, filterVel);
  }

  if (btnIndex == UPPER_SW && btnType == ROX_HELD) {
    if (state == PARAMETER || state == PERFORMANCE_RECALL) {
      // Switch to performance mode temporarily to select a slot
      inPerformanceMode = true;
      state = PERFORMANCE_SAVE;

      performanceIndex = performances.size();  // start on a new slot
      int newPerfNo = performanceIndex + 1;

      PlayMode currentMode;
      if (dualmode)
        currentMode = DUAL;
      else if (splitmode)
        currentMode = SPLIT;
      else
        currentMode = WHOLE;

      Performance newPerf = {
        newPerfNo,
        patches[upperPatchIndex].patchNo,
        patches[lowerPatchIndex].patchNo,
        INITPATCHNAME,
        currentMode,
        newsplitPoint,
        splitTrans
      };

      currentPerformance = newPerf;

      if (performanceIndex >= performances.size()) {
        performances.push(currentPerformance);  // temp slot
      }

      showPerformancePage(
        String(newPerf.performanceNo),
        newPerf.name,
        newPerf.upperPatchNo,
        getPatchName(newPerf.upperPatchNo),
        newPerf.lowerPatchNo,
        getPatchName(newPerf.lowerPatchNo));

      // Wait for encoder click to continue to PERFORMANCE_NAMING
      // Don't push() yet — defer until naming is complete
    }
  } else if (btnIndex == UPPER_SW && btnType == ROX_RELEASED) {
    upperSW = !upperSW;
    myControlChange(midiChannel, CCupperSW, upperSW);
  }

  if (btnIndex == WHOLE_SW && btnType == ROX_HELD) {
    polyMode = !polyMode;
  } else if (btnIndex == WHOLE_SW && btnType == ROX_RELEASED) {
    wholemode = !wholemode;
    myControlChange(midiChannel, CCwholemode, wholemode);
  }

  if (btnIndex == DUAL_SW && btnType == ROX_PRESSED) {
    dualmode = !dualmode;
    myControlChange(midiChannel, CCdualmode, dualmode);
  }

  if (btnIndex == SPLIT_SW && btnType == ROX_PRESSED) {
    splitmode = !splitmode;
    myControlChange(midiChannel, CCsplitmode, splitmode);
  }
}

void showSettingsPage() {
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

void showPerformancePage(String perfNum, String name, int upperNo, String upperName, int lowerNo, String lowerName) {
  currentPerfNum = perfNum;
  currentPerfName = name;
  currentUpperPatchNo = upperNo;
  currentUpperPatchName = upperName;
  currentLowerPatchNo = lowerNo;
  currentLowerPatchName = lowerName;
}

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    if (inPerformanceMode && (state == PARAMETER || state == PATCH)) {
      state = PERFORMANCE_DELETE;
    } else if (state == PARAMETER || state == PATCH) {
      state = DELETE;
    }
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case SAVE:
        {
          if (renamedPatch.length() == 0) {
            renamedPatch = patches.last().patchName;
          }

          // Update patch name depending on upper or lower
          if (upperSW) {
            patchNameU = renamedPatch;
            currentPatchNameU = renamedPatch;
            currentPgmNumU = String(patches.last().patchNo);
          } else {
            patchNameL = renamedPatch;
            currentPatchNameL = renamedPatch;
            currentPgmNumL = String(patches.last().patchNo);
          }

          // ✅ Update last patch in the buffer before saving
          patches.last().patchName = renamedPatch;

          // ✅ Save updated patch data
          String patchData = getCurrentPatchData();
          savePatch(String(patches.last().patchNo).c_str(), patchData);

          // ✅ Reload and reorder patches explicitly
          loadPatches();
          setPatchesOrdering(patches.last().patchNo);

          // ✅ Correctly update patch index for immediate display
          int targetPatchNo = upperSW ? patchNoU : patchNoL;

          for (int i = 0; i < patches.size(); i++) {
            if (patches[i].patchNo == targetPatchNo) {
              if (upperSW) upperPatchIndex = i;
              else lowerPatchIndex = i;
              break;
            }
          }

          // ✅ Immediately refresh display with updated data
          refreshPatchDisplayFromState();

          renamedPatch = "";
          state = PARAMETER;
        }
        break;


      case PATCHNAMING:
        {
          //Serial.println("renamedPatch BEFORE SAVING: " + renamedPatch);

          if (renamedPatch.length() == 0) {
            renamedPatch = patches.last().patchName;  // fallback to existing name
          }

          // Update correct upper/lower patch name based on current layer
          if (upperSW) {
            patchNameU = renamedPatch;
            currentPatchNameU = renamedPatch;  // Update immediately
            currentPgmNumU = String(patches.last().patchNo);
          } else {
            patchNameL = renamedPatch;
            currentPatchNameL = renamedPatch;  // Update immediately
            currentPgmNumL = String(patches.last().patchNo);
          }

          // Update last patch in the patches buffer
          patches.last().patchName = renamedPatch;

          // Save patch data (with the correct name included)
          String patchData = getCurrentPatchData();
          savePatch(String(patches.last().patchNo).c_str(), patchData);

          loadPatches();                   // Refresh patches list from SD card
          refreshPatchDisplayFromState();  // immediately update the display
          setPatchesOrdering(patches.last().patchNo);

          renamedPatch = "";
          state = PARAMETER;
        }
        break;

      case PARAMETER:
        if (inPerformanceMode) {
          if (performances.size() < PERFORMANCE_LIMIT) {
            int newPerfNo = performances.size() + 1;
            Performance newPerf = {
              newPerfNo,
              patches[upperPatchIndex].patchNo,
              patches[lowerPatchIndex].patchNo,
              INITPATCHNAME,
              (PlayMode)playMode,
              newsplitPoint,
              splitTrans
            };
            currentPerformance = newPerf;
            performances.push(newPerf);
            performanceIndex = performances.size() - 1;

            showPerformancePage(
              String(newPerf.performanceNo),
              newPerf.name,
              newPerf.upperPatchNo,
              getPatchName(newPerf.upperPatchNo),
              newPerf.lowerPatchNo,
              getPatchName(newPerf.lowerPatchNo));

            state = PERFORMANCE_SAVE;
          }
        } else {
          // 🛠 PATCH SAVE FLOW
          if (patches.size() < PATCHES_LIMIT) {
            resetPatchesOrdering();  // start from patch 1
            patches.push({ patches.size() + 1, INITPATCHNAME });
            state = SAVE;
          }
        }
        break;

      case PERFORMANCE_SAVE:
        currentPerformance = performances[performanceIndex];
        state = PERFORMANCE_NAMING;
        renamedPatch = currentPerformance.name;
        charIndex = 0;
        currentCharacter = CHARACTERS[charIndex];
        startedRenaming = false;
        showRenamingPage(renamedPatch);
        break;

      case PERFORMANCE_NAMING:
        if (saveButton.numClicks() == 1) {
          // ✅ Update name if anything was entered
          if (renamedPatch.length() > 0) {
            currentPerformance.name = renamedPatch;
          }

          // ✅ Finalize data fields
          currentPerformance.upperPatchNo = patches[upperPatchIndex].patchNo;
          currentPerformance.lowerPatchNo = patches[lowerPatchIndex].patchNo;

          PlayMode currentMode = WHOLE;
          if (dualmode) currentMode = DUAL;
          else if (splitmode) currentMode = SPLIT;

          currentPerformance.mode = currentMode;
          currentPerformance.newsplitPoint = newsplitPoint;
          currentPerformance.splitTrans = splitTrans;

          // ✅ Update or insert in buffer
          if (performanceIndex < performances.size()) {
            performances[performanceIndex] = currentPerformance;
          } else {
            performances.push(currentPerformance);
          }

          // ✅ Save to file
          char filename[16];
          snprintf(filename, sizeof(filename), "perf%03d", currentPerformance.performanceNo);
          savePerformance(filename, currentPerformance);

          // ✅ Reload full list
          loadPerformances();

          // ✅ Re-sync currentPerformance and index
          for (int i = 0; i < performances.size(); i++) {
            if (performances[i].performanceNo == currentPerformance.performanceNo) {
              performanceIndex = i;
              currentPerformance = performances[i];
              break;
            }
          }

          // ✅ Show updated page with new name
          showPerformancePage(
            String(currentPerformance.performanceNo),
            currentPerformance.name,
            currentPerformance.upperPatchNo,
            getPatchName(currentPerformance.upperPatchNo),
            currentPerformance.lowerPatchNo,
            getPatchName(currentPerformance.lowerPatchNo));

          // ✅ Reset renaming state
          renamedPatch = "";
          charIndex = 0;
          currentCharacter = CHARACTERS[0];
          startedRenaming = false;

          // ✅ Return to performance view
          state = PERFORMANCE_RECALL;
          inPerformanceMode = true;
        }

        else if (recallButton.numClicks() == 1) {
          if (renamedPatch.length() < 12) {
            renamedPatch.concat(String(currentCharacter));
            charIndex = 0;
            currentCharacter = CHARACTERS[charIndex];
            showRenamingPage(renamedPatch);
          }
        }

        else if (backButton.numClicks() == 1) {
          renamedPatch = "";
          charIndex = 0;
          startedRenaming = false;
          state = PARAMETER;
          if (performances.size() > 0 && performances.last().name == INITPATCHNAME) {
            performances.pop();
          }
        }
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
  } else if (settingsButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = SETTINGS;
        showSettingsPage();
        break;
      case SETTINGS:
        showSettingsPage();
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }

  backButton.update();
  if (backButton.held()) {
    //If Back button held, Panic - all notes off
  } else if (backButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        renamedPatch = "";
        state = PARAMETER;
        loadPatches();  //Remove patch that was to be saved
        setPatchesOrdering(patchNo);
        break;
      case PATCHNAMING:
        charIndex = 0;
        renamedPatch = "";
        state = SAVE;
        break;
      case DELETE:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SETTINGS:
        state = PARAMETER;
        break;
      case SETTINGSVALUE:
        state = SETTINGS;
        showSettingsPage();
        break;
      case PERFORMANCE_NAMING:
        renamedPatch = "";
        charIndex = 0;
        state = PARAMETER;
        // Optionally remove the unsaved performance from the buffer:
        if (performances.size() > 0 && performances.last().name == INITPATCHNAME) {
          performances.pop();
        }
        break;
      case PERFORMANCE_DELETE:
        setPerformancesOrdering(currentPerformance.performanceNo);
        state = PARAMETER;
        break;
    }
  }

  // Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    if (!recallHeldToggleLatch) {
      inPerformanceMode = !inPerformanceMode;
      recallHeldToggleLatch = true;

      showCurrentParameterPage("Mode", inPerformanceMode ? "Performance" : "Patch");

      if (inPerformanceMode && performances.size() > 0) {
        performanceIndex = 0;
        currentPerformance = performances[performanceIndex];

        // Apply mode and split info
        playMode = currentPerformance.mode;
        wholemode = (playMode == WHOLE);
        dualmode = (playMode == DUAL);
        splitmode = (playMode == SPLIT);

        newsplitPoint = currentPerformance.newsplitPoint;
        splitTrans = currentPerformance.splitTrans;
        updateplayMode(0);

        // Find patch indices
        for (int i = 0; i < patches.size(); i++) {
          if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
          if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
        }

        upperSW = true;
        recallPatch(currentPerformance.upperPatchNo);

        upperSW = false;
        recallPatch(currentPerformance.lowerPatchNo);

        // WHOLE mode needs to mirror lower into upper
        if (wholemode) {
          patchNoU = patchNoL;
          patchNameU = patchNameL;
          currentPatchNameU = currentPatchNameL;
          currentPgmNumU = currentPgmNumL;
          memcpy(upperData, lowerData, sizeof(upperData));
        }

        refreshPatchDisplayFromState();

        showPerformancePage(
          String(currentPerformance.performanceNo),
          currentPerformance.name,
          currentPerformance.upperPatchNo,
          getPatchName(currentPerformance.upperPatchNo),
          currentPerformance.lowerPatchNo,
          getPatchName(currentPerformance.lowerPatchNo));

        state = PERFORMANCE_RECALL;
      } else {
        state = PARAMETER;
        upperSW = true;
        recallPatch(patches[upperPatchIndex].patchNo);
        upperSW = false;
        recallPatch(patches[lowerPatchIndex].patchNo);
        refreshPatchDisplayFromState();
      }
    }
  } else {
    recallHeldToggleLatch = false;
  }
  if (recallButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        //Serial.println("[INFO] Ignored default RECALL to avoid overwriting performance recall.");
        state = PARAMETER;
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 12)  //actually 12 chars
        {
          renamedPatch.concat(String(currentCharacter));
          charIndex = 0;
          currentCharacter = CHARACTERS[charIndex];
          showRenamingPage(renamedPatch);
        }
        break;
      case DELETE:
        //Don't delete final patch
        if (patches.size() > 1) {
          state = DELETEMSG;
          patchNo = patches.first().patchNo;     //PatchNo to delete from SD card
          patches.shift();                       //Remove patch from circular buffer
          deletePatch(String(patchNo).c_str());  //Delete from SD card
          loadPatches();                         //Repopulate circular buffer to start from lowest Patch No
          renumberPatchesOnSD();
          loadPatches();                      //Repopulate circular buffer again after delete
          patchNo = patches.first().patchNo;  //Go back to 1
          recallPatch(patchNo);               //Load first patch
        }
        state = PARAMETER;
        break;
      case SETTINGS:
        state = SETTINGSVALUE;
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;

      case PARAMETER:
        // Enter performance recall
        if (performances.size() > 0) {
          currentPerformance = performances.first();
          showPerformancePage(
            String(currentPerformance.performanceNo),
            currentPerformance.name,
            currentPerformance.upperPatchNo,
            getPatchName(currentPerformance.upperPatchNo),
            currentPerformance.lowerPatchNo,
            getPatchName(currentPerformance.lowerPatchNo));
          state = PERFORMANCE_RECALL;
        }
        break;

      case PERFORMANCE_DELETE:
        if (performances.size() > 0) {
          state = PERFORMANCE_DELETEMSG;

          int deletedNo = performances.first().performanceNo;
          performances.shift();          // Remove from buffer
          deletePerformance(deletedNo);  // Delete file
          loadPerformances();            // Refresh buffer
          renumberPerformancesOnSD();    // Reorder files
          loadPerformances();            // Reload to apply new order

          currentPerformance = performances.first();
          recallPerformance(currentPerformance);
        }
        state = PARAMETER;
        return;


      case PERFORMANCE_DELETEMSG:
        // Show deletion complete screen briefly
        tft.fillScreen(ST7735_BLACK);
        tft.setFont(&FreeSans12pt7b);
        tft.setTextColor(ST7735_YELLOW);
        tft.setCursor(10, 60);
        tft.println("Renumbering");
        tft.setCursor(10, 100);
        tft.println("Performances...");
        tft.updateScreen();
        delay(1000);
        state = PARAMETER;
        break;
    }
  }
}

// Updated checkEncoder() with upperPatchIndex and lowerPatchIndex
void checkEncoder() {
  long encRead = encoder.read();
  bool moved = false;

  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    moved = true;

    switch (state) {

      case PERFORMANCE_DELETE:
        if (encCW) {
          performances.push(performances.shift());
        } else {
          performances.unshift(performances.pop());
        }
        break;

      case PERFORMANCE_SAVE:
        performanceIndex++;
        if (performanceIndex >= performances.size()) performanceIndex = 0;
        currentPerformance = performances[performanceIndex];
        showPerformancePage(
          String(currentPerformance.performanceNo),
          currentPerformance.name,
          currentPerformance.upperPatchNo,
          getPatchName(currentPerformance.upperPatchNo),
          currentPerformance.lowerPatchNo,
          getPatchName(currentPerformance.lowerPatchNo));
        break;

      case PERFORMANCE_RECALL:
        performanceIndex++;
        if (performanceIndex >= performances.size()) performanceIndex = 0;
        currentPerformance = performances[performanceIndex];

        // 🟡 Apply mode and split info
        playMode = currentPerformance.mode;
        wholemode = (playMode == WHOLE);
        dualmode = (playMode == DUAL);
        splitmode = (playMode == SPLIT);

        newsplitPoint = currentPerformance.newsplitPoint;
        splitTrans = currentPerformance.splitTrans;
        updateplayMode(0);

        // 🟡 Match patch indices
        for (int i = 0; i < patches.size(); i++) {
          if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
          if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
        }

        // 🟡 Recall both patches
        upperSW = true;
        recallPatch(currentPerformance.upperPatchNo);

        upperSW = false;
        recallPatch(currentPerformance.lowerPatchNo);

        // 🟡 Mirror lower into upper in WHOLE mode
        if (wholemode) {
          patchNoU = patchNoL;
          patchNameU = patchNameL;
          currentPatchNameU = currentPatchNameL;
          currentPgmNumU = currentPgmNumL;
          memcpy(upperData, lowerData, sizeof(upperData));
        }

        refreshPatchDisplayFromState();

        // 🟡 Update the performance page with correct info
        showPerformancePage(
          String(currentPerformance.performanceNo),
          currentPerformance.name,
          currentPerformance.upperPatchNo,
          getPatchName(currentPerformance.upperPatchNo),
          currentPerformance.lowerPatchNo,
          getPatchName(currentPerformance.lowerPatchNo));
        break;

      case PERFORMANCE_NAMING:
        if (!startedRenaming) {
          renamedPatch = "";
          startedRenaming = true;
        }

        charIndex++;
        if (charIndex >= TOTALCHARS) charIndex = 0;
        currentCharacter = CHARACTERS[charIndex];
        showRenamingPage(renamedPatch + currentCharacter);
        break;

      case PARAMETER:
        if (inPerformanceMode) {
          performanceIndex++;
          if (performanceIndex >= performances.size()) performanceIndex = 0;
          currentPerformance = performances[performanceIndex];

          for (int i = 0; i < patches.size(); i++) {
            if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
            if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
          }

          playMode = currentPerformance.mode;
          wholemode = (playMode == WHOLE);
          updateplayMode(0);

          upperSW = true;
          recallPatch(currentPerformance.upperPatchNo);
          upperSW = false;
          recallPatch(currentPerformance.lowerPatchNo);
        } else {
          if (upperSW) {
            upperPatchIndex++;
            if (upperPatchIndex >= patches.size()) upperPatchIndex = 0;
            patchNo = patches[upperPatchIndex].patchNo;
            recallPatch(patchNo);
          } else {
            lowerPatchIndex++;
            if (lowerPatchIndex >= patches.size()) lowerPatchIndex = 0;
            patchNo = patches[lowerPatchIndex].patchNo;
            recallPatch(patchNo);
          }
        }
        refreshPatchDisplayFromState();
        break;

      case RECALL:
      case SAVE:
      case DELETE:
        patches.push(patches.shift());
        break;

      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;

      case SETTINGS:
        settings::increment_setting();
        showSettingsPage();
        break;

      case SETTINGSVALUE:
        settings::increment_setting_value();
        showSettingsPage();
        break;
    }
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    moved = true;

    switch (state) {

      case PERFORMANCE_DELETE:
        if (encCW) {
          performances.push(performances.shift());
        } else {
          performances.unshift(performances.pop());
        }
        break;

      case PERFORMANCE_SAVE:
        performanceIndex--;
        if (performanceIndex < 0) performanceIndex = performances.size() - 1;
        currentPerformance = performances[performanceIndex];
        showPerformancePage(
          String(currentPerformance.performanceNo),
          currentPerformance.name,
          currentPerformance.upperPatchNo,
          getPatchName(currentPerformance.upperPatchNo),
          currentPerformance.lowerPatchNo,
          getPatchName(currentPerformance.lowerPatchNo));
        break;

      case PERFORMANCE_RECALL:
        performanceIndex--;
        if (performanceIndex < 0) performanceIndex = performances.size() - 1;

        currentPerformance = performances[performanceIndex];

        // Apply mode and split info
        playMode = currentPerformance.mode;
        wholemode = (playMode == WHOLE);
        dualmode = (playMode == DUAL);
        splitmode = (playMode == SPLIT);

        newsplitPoint = currentPerformance.newsplitPoint;
        splitTrans = currentPerformance.splitTrans;
        updateplayMode(0);

        for (int i = 0; i < patches.size(); i++) {
          if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
          if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
        }

        upperSW = true;
        recallPatch(currentPerformance.upperPatchNo);
        upperSW = false;
        recallPatch(currentPerformance.lowerPatchNo);

        if (wholemode) {
          patchNoU = patchNoL;
          patchNameU = patchNameL;
          currentPatchNameU = currentPatchNameL;
          currentPgmNumU = currentPgmNumL;
          memcpy(upperData, lowerData, sizeof(upperData));
        }

        refreshPatchDisplayFromState();

        showPerformancePage(
          String(currentPerformance.performanceNo),
          currentPerformance.name,
          currentPerformance.upperPatchNo,
          getPatchName(currentPerformance.upperPatchNo),
          currentPerformance.lowerPatchNo,
          getPatchName(currentPerformance.lowerPatchNo));
        break;

      case PERFORMANCE_NAMING:
        if (!startedRenaming) {
          renamedPatch = "";
          startedRenaming = true;
        }

        charIndex--;
        if (charIndex < 0) charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex];
        showRenamingPage(renamedPatch + currentCharacter);
        break;

      case PARAMETER:
        if (inPerformanceMode) {
          performanceIndex--;
          if (performanceIndex < 0) performanceIndex = performances.size() - 1;
          currentPerformance = performances[performanceIndex];

          for (int i = 0; i < patches.size(); i++) {
            if (patches[i].patchNo == currentPerformance.upperPatchNo) upperPatchIndex = i;
            if (patches[i].patchNo == currentPerformance.lowerPatchNo) lowerPatchIndex = i;
          }

          playMode = currentPerformance.mode;
          wholemode = (playMode == WHOLE);
          updateplayMode(0);

          upperSW = true;
          recallPatch(currentPerformance.upperPatchNo);
          upperSW = false;
          recallPatch(currentPerformance.lowerPatchNo);
        } else {
          if (upperSW) {
            upperPatchIndex--;
            if (upperPatchIndex < 0) upperPatchIndex = patches.size() - 1;
            patchNo = patches[upperPatchIndex].patchNo;
            recallPatch(patchNo);
          } else {
            lowerPatchIndex--;
            if (lowerPatchIndex < 0) lowerPatchIndex = patches.size() - 1;
            patchNo = patches[lowerPatchIndex].patchNo;
            recallPatch(patchNo);
          }
        }
        refreshPatchDisplayFromState();
        break;


      case RECALL:
      case SAVE:
      case DELETE:
        patches.unshift(patches.pop());
        break;

      case PATCHNAMING:
        if (charIndex == -1) charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;

      case SETTINGS:
        settings::decrement_setting();
        showSettingsPage();
        break;

      case SETTINGSVALUE:
        settings::decrement_setting_value();
        showSettingsPage();
        break;
    }
  }

  if (moved) {
    encPrevious = encRead;
  }
}

void reinitialiseToPanel() {
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++) {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
    mux3ValuesPrev[i] = RE_READ;
  }
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings", "", "");
}


void loop() {

  checkSwitches();
  checkEeprom();
  writeDemux();
  checkMux();
  checkEncoder();
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);
  octoswitch.update();
  srp.update();
  sr.update();
  LFODelayHandle();
}