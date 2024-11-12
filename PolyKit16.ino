/*
  PolyKit 16 MUX - Firmware Rev 1.7

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
#include <RoxMux.h>


#define PARAMETER 0      //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1         //Patches list
#define SAVE 2           //Save patch page
#define REINITIALISE 3   // Reinitialise message
#define PATCH 4          // Show current patch bypassing PARAMETER
#define PATCHNAMING 5    // Patch naming page
#define DELETE 6         //Delete patch page
#define DELETEMSG 7      //Delete patch message page
#define SETTINGS 8       //Settings page
#define SETTINGSVALUE 9  //Settings page

unsigned int state = PARAMETER;

uint32_t int_ref_on_flexible_mode = 0b00001001000010100000000000000000;  // { 0000 , 1001 , 0000 , 1010000000000000 , 0000 }

uint32_t sample_data1 = 0b00000000000000000000000000000000;
uint32_t sample_data2 = 0b00000000000000000000000000000000;
uint32_t channel_a = 0b00000010000000000000000000000000;
uint32_t channel_b = 0b00000010000100000000000000000000;
uint32_t channel_c = 0b00000010001000000000000000000000;
uint32_t channel_d = 0b00000010001100000000000000000000;
uint32_t channel_e = 0b00000010010000000000000000000000;
uint32_t channel_f = 0b00000010010100000000000000000000;
uint32_t channel_g = 0b00000010011000000000000000000000;
uint32_t channel_h = 0b00000010011100000000000000000000;

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
int patchNo = 0;
int patchNoU = 0;
int patchNoL = 0;
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

  srp.begin(SRP_DATA, SRP_LATCH, SRP_CLK, SRP_PWM);
  sr.begin(SR_DATA, SR_LATCH, SR_CLK, SR_PWM);

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
  AfterTouchDestU = getAfterTouchU();
  oldAfterTouchDestU = AfterTouchDestU;
  AfterTouchDestL = getAfterTouchL();
  oldAfterTouchDestL = AfterTouchDestL;

  newsplitPoint = getSplitPoint();

  splitTrans = getSplitTrans();
  setTranspose(splitTrans);

  //Read Pitch Bend Range from EEPROM
  pitchBendRange = getPitchBendRange();

  //Read Mod Wheel Depth from EEPROM
  modWheelDepth = getModWheelDepth();

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  monoMultiL = getMonoMultiL();
  oldmonoMultiL = monoMultiL;
  monoMultiU = getMonoMultiU();
  oldmonoMultiU = monoMultiU;
  filterLogLinU = getFilterEnvU();
  oldfilterLogLinU = filterLogLinU;
  filterLogLinL = getFilterEnvL();
  oldfilterLogLinL = filterLogLinL;
  ampLogLinU = getAmpEnvU();
  oldampLogLinU = ampLogLinU;
  ampLogLinL = getAmpEnvL();
  oldampLogLinL = ampLogLinL;
  keyTrackSWU = getKeyTrackU();
  oldkeyTrackSWU = keyTrackSWU;
  keyTrackSWL = getKeyTrackL();
  oldkeyTrackSWL = keyTrackSWL;

  //setupDisplay();
  //delay(3000);

  patchNoU = getLastPatchU();
  patchNoL = getLastPatchL();
  upperSW = 1;
  recallPatch(patchNoU);
  upperSW = 0;
  recallPatch(patchNoL);  //Load first patch
  updatewholemode();
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
  if (monoMultiU && !LFODelayGoU) {
    if (oldnumberOfNotesU < numberOfNotesU) {
      previousMillisU = currentMillisU;
      oldnumberOfNotesU = numberOfNotesU;
    }
  }
  if (numberOfNotesU > 0) {
    if (currentMillisU - previousMillisU >= intervalU) {
      LFODelayGoU = 1;
    } else {
      LFODelayGoU = 0;
    }
  } else {
    LFODelayGoU = 1;
    previousMillisU = currentMillisU;  //reset timer so its ready for the next time
  }

  unsigned long currentMillisL = millis();
  if (monoMultiL && !LFODelayGoL) {
    if (oldnumberOfNotesL < numberOfNotesL) {
      previousMillisL = currentMillisL;
      oldnumberOfNotesL = numberOfNotesL;
    }
  }
  if (numberOfNotesL > 0) {
    if (currentMillisL - previousMillisL >= intervalL) {
      LFODelayGoL = 1;
    } else {
      LFODelayGoL = 0;
    }
  } else {
    LFODelayGoL = 1;
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

void DinHandlePitchBend(byte channel, int pitch) {
  MIDI2.sendPitchBend(pitch, 1);
  MIDI5.sendPitchBend(pitch, 1);
}

void getDelayTime() {
  delaytimeL = LFODelayL;
  if (delaytimeL <= 0) {
    delaytimeL = 0.1;
  }
  intervalL = (delaytimeL * 10);

  delaytimeU = LFODelayU;
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

void updatepwLFO() {
  showCurrentParameterPage("PWM Rate", int(pwLFOstr));
}

void updatefmDepth() {
  showCurrentParameterPage("FM Depth", int(fmDepthstr));
}

void updateosc2PW() {
  showCurrentParameterPage("OSC2 PW", String(osc2PWstr) + " %");
}

void updateosc2PWM() {
  showCurrentParameterPage("OSC2 PWM", int(osc2PWMstr));
}

void updateosc1PW() {
  showCurrentParameterPage("OSC1 PW", String(osc1PWstr) + " %");
}

void updateosc1PWM() {
  showCurrentParameterPage("OSC1 PWM", int(osc1PWMstr));
}

void updateosc1Range(boolean announce) {
  if (upperSW) {
    if (osc1Rangestr > 100) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("8"));
      }
      midiCCOutCPU5(CCosc1Range, 127, 1);
    } else if (osc1Rangestr < 100 && osc1Rangestr > 33) {
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
    if (osc1Rangestr > 100) {
      if (announce) {
        showCurrentParameterPage("Osc1 Range", String("8"));
      }
      midiCCOutCPU2(CCosc1Range, 127, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc1Range, 127, 1);
      }
    } else if (osc1Rangestr < 100 && osc1Rangestr > 33) {
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

void updateosc2Range(boolean announce) {
  if (upperSW) {
    if (osc2Rangestr > 100) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("8"));
      }
      midiCCOutCPU5(CCosc2Range, 127, 1);
    } else if (osc2Rangestr < 100 && osc2Rangestr > 33) {
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
    if (osc2Rangestr > 100) {
      if (announce) {
        showCurrentParameterPage("Osc2 Range", String("8"));
      }
      midiCCOutCPU2(CCosc2Range, 127, 1);
      if (wholemode) {
        midiCCOutCPU5(CCosc2Range, 127, 1);
      }
    } else if (osc2Rangestr < 100 && osc2Rangestr > 33) {
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

void updateglideTime() {
  showCurrentParameterPage("Glide Time", String(glideTimestr * 10) + " Seconds");
}

void updateosc2Detune() {
  showCurrentParameterPage("OSC2 Detune", String(osc2Detunestr));
}

void updatenoiseLevel() {
  showCurrentParameterPage("Noise Level", String(noiseLevelstr));
}

void updateOsc2SawLevel() {
  showCurrentParameterPage("OSC2 Saw", int(osc2SawLevelstr));
}

void updateOsc1SawLevel() {
  showCurrentParameterPage("OSC1 Saw", int(osc1SawLevelstr));
}

void updateosc2PulseLevel() {
  showCurrentParameterPage("OSC2 Pulse", int(osc2PulseLevelstr));
}

void updateOsc1PulseLevel() {
  showCurrentParameterPage("OSC1 Pulse", int(osc1PulseLevelstr));
}

void updateosc2TriangleLevel() {
  showCurrentParameterPage("OSC2 Triangle", int(osc2TriangleLevelstr));
}

void updateOsc1SubLevel() {
  showCurrentParameterPage("OSC1 Sub", int(osc1SubLevelstr));
}

void updateamDepth() {
  showCurrentParameterPage("AM Depth", int(amDepthstr));
}

void updateFilterCutoff() {
  showCurrentParameterPage("Cutoff", String(filterCutoffstr) + " Hz");
}

void updatefilterLFO() {
  showCurrentParameterPage("TM depth", int(filterLFOstr));
}

void updatefilterRes() {
  showCurrentParameterPage("Resonance", int(filterResstr));
}

void updateFilterType(boolean announce) {
  if (upperSW) {
    switch (filterTypeU) {
      case 0:
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
        if (filterPoleSWU == 1) {
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
    switch (filterTypeL) {
      case 0:
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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
        if (filterPoleSWL == 1) {
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

void updatefilterEGlevel() {
  showCurrentParameterPage("EG Depth", int(filterEGlevelstr));
}

void updatekeytrack() {
  showCurrentParameterPage("Keytrack", int(keytrackstr));
}

void updateLFORate() {
  showCurrentParameterPage("LFO Rate", String(LFORatestr) + " Hz");
}

void updateLFODelay() {
  showCurrentParameterPage("LFO Delay", String(LFODelaystr));
}

void updateStratusLFOWaveform() {
  if (lfoAlt == 1) {
    switch (LFOWaveformstr) {
      case 0:
        StratusLFOWaveform = "Saw +Oct";
        break;

      case 1:
        StratusLFOWaveform = "Quad Saw";
        break;

      case 2:
        StratusLFOWaveform = "Quad Pulse";
        break;

      case 3:
        StratusLFOWaveform = "Tri Step";
        break;

      case 4:
        StratusLFOWaveform = "Sine +Oct";
        break;

      case 5:
        StratusLFOWaveform = "Sine +3rd";
        break;

      case 6:
        StratusLFOWaveform = "Sine +4th";
        break;

      case 7:
        StratusLFOWaveform = "Rand Slopes";
        break;
    }
  } else {
    switch (LFOWaveformstr) {
      case 0:
        StratusLFOWaveform = "Sawtooth Up";
        break;


      case 1:
        StratusLFOWaveform = "Sawtooth Down";
        break;

      case 2:
        StratusLFOWaveform = "Squarewave";
        break;

      case 3:
        StratusLFOWaveform = "Triangle";
        break;

      case 4:
        StratusLFOWaveform = "Sinewave";
        break;

      case 5:
        StratusLFOWaveform = "Sweeps";
        break;

      case 6:
        StratusLFOWaveform = "Lumps";
        break;

      case 7:
        StratusLFOWaveform = "Sample & Hold";
        break;
    }
  }
  showCurrentParameterPage("LFO Wave", StratusLFOWaveform);
}

void updatefilterAttack() {
  if (filterAttackstr < 1000) {
    showCurrentParameterPage("VCF Attack", String(int(filterAttackstr)) + " ms", FILTER_ENV);
  } else {
    showCurrentParameterPage("VCF Attack", String(filterAttackstr * 0.001) + " s", FILTER_ENV);
  }
}

void updatefilterDecay() {
  if (filterDecaystr < 1000) {
    showCurrentParameterPage("VCF Decay", String(int(filterDecaystr)) + " ms", FILTER_ENV);
  } else {
    showCurrentParameterPage("VCF Decay", String(filterDecaystr * 0.001) + " s", FILTER_ENV);
  }
}

void updatefilterSustain() {
  showCurrentParameterPage("VCF Sustain", String(filterSustainstr), FILTER_ENV);
}

void updatefilterRelease() {
  if (filterReleasestr < 1000) {
    showCurrentParameterPage("VCF Release", String(int(filterReleasestr)) + " ms", FILTER_ENV);
  } else {
    showCurrentParameterPage("VCF Release", String(filterReleasestr * 0.001) + " s", FILTER_ENV);
  }
}

void updateampAttack() {
  if (ampAttackstr < 1000) {
    showCurrentParameterPage("VCA Attack", String(int(ampAttackstr)) + " ms", AMP_ENV);
  } else {
    showCurrentParameterPage("VCA Attack", String(ampAttackstr * 0.001) + " s", AMP_ENV);
  }
}

void updateampDecay() {
  if (ampDecaystr < 1000) {
    showCurrentParameterPage("VCA Decay", String(int(ampDecaystr)) + " ms", AMP_ENV);
  } else {
    showCurrentParameterPage("VCA Decay", String(ampDecaystr * 0.001) + " s", AMP_ENV);
  }
}

void updateampSustain() {
  showCurrentParameterPage("VCA Sustain", String(ampSustainstr), AMP_ENV);
}

void updateampRelease() {
  if (ampReleasestr < 1000) {
    showCurrentParameterPage("VCA Release", String(int(ampReleasestr)) + " ms", AMP_ENV);
  } else {
    showCurrentParameterPage("VCA Release", String(ampReleasestr * 0.001) + " s", AMP_ENV);
  }
}

void updatevolumeControl() {
  showCurrentParameterPage("Volume", int(volumeControlstr));
}

////////////////////////////////////////////////////////////////

void updateglideSW(boolean announce) {
  if (upperSW) {
    if (glideSWU == 0) {
      if (announce) {
        showCurrentParameterPage("Glide", "Off");
        midiCCOutCPU5(CCglideSW, 1, 1);
        delay(1);
        midiCCOutCPU5(CCglideTime, 0, 1);
      }
      sr.writePin(GLIDE_LED, LOW);  // LED off
    } else {
      if (announce) {
        showCurrentParameterPage("Glide", "On");
        midiCCOutCPU5(CCglideTime, int(glideTimeU / 8), 1);
        delay(1);
        midiCCOutCPU5(CCglideSW, 127, 1);
      }
      sr.writePin(GLIDE_LED, HIGH);  // LED on
    }
  } else {
    if (glideSWL == 0) {
      if (announce) {
        showCurrentParameterPage("Glide", "Off");
        midiCCOutCPU2(CCglideSW, 1, 1);
        delay(1);
        midiCCOutCPU2(CCglideTime, 0, 1);
        if (wholemode) {
          midiCCOutCPU5(CCglideSW, 1, 1);
          delay(1);
          midiCCOutCPU5(CCglideTime, 0, 1);
        }
      }
      sr.writePin(GLIDE_LED, LOW);  // LED off
    } else {
      if (announce) {
        showCurrentParameterPage("Glide", "On");
        midiCCOutCPU2(CCglideTime, int(glideTimeL / 8), 1);
        delay(1);
        midiCCOutCPU2(CCglideSW, 127, 1);
        if (wholemode) {
          midiCCOutCPU5(CCglideSW, 127, 1);
          delay(1);
          midiCCOutCPU5(CCglideTime, int(glideTimeU / 8), 1);
        }
      }
      sr.writePin(GLIDE_LED, HIGH);  // LED on
    }
  }
}

void updatefilterPoleSwitch(boolean announce) {
  if (upperSW) {
    if (filterPoleSWU == 1) {
      if (announce) {
        //showCurrentParameterPage("VCF Pole", "On");
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 127);
      }
      sr.writePin(FILTERPOLE_LED, HIGH);
      srp.writePin(FILTER_POLE_UPPER, HIGH);
    } else {
      if (announce) {
        //showCurrentParameterPage("VCF Pole", "Off");
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 1);
      }
      sr.writePin(FILTERPOLE_LED, LOW);
      srp.writePin(FILTER_POLE_UPPER, LOW);
    }
  } else {
    if (filterPoleSWL == 1) {
      if (announce) {
        //showCurrentParameterPage("VCF Pole", "On");
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 127);
      }
      sr.writePin(FILTERPOLE_LED, HIGH);
      srp.writePin(FILTER_POLE_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(FILTER_POLE_UPPER, HIGH);
      }
    } else {
      if (announce) {
        //showCurrentParameterPage("VCF Pole", "Off");
        updateFilterType(1);
        midiCCOut(CCfilterPoleSW, 1);
      }
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
    switch (statefilterLoopU) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCF Key Loop", "On");
          midiCCOut(CCfilterLoop, 127);
        }
        sr.writePin(FILTERLOOP_LED, HIGH);        // LED on
        sr.writePin(FILTERLOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(FILTER_MODE_BIT0_UPPER, LOW);
        srp.writePin(FILTER_MODE_BIT1_UPPER, HIGH);
        oldfilterLoop = statefilterLoopU;
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
        oldfilterLoop = statefilterLoopU;
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
    switch (statefilterLoopL) {
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
        oldfilterLoop = statefilterLoopL;
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
        oldfilterLoop = statefilterLoopL;
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
    if (filterEGinvU == 0) {
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
    if (filterEGinvL == 0) {
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
    if (filterVelU == 0) {
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
    if (filterVelL == 0) {
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
    switch (statevcaLoopU) {
      case 1:
        if (announce) {
          showCurrentParameterPage("VCA Key Loop", "On");
          midiCCOut(CCvcaLoop, 127);
        }
        sr.writePin(VCALOOP_LED, HIGH);        // LED on
        sr.writePin(VCALOOP_DOUBLE_LED, LOW);  // LED on
        srp.writePin(AMP_MODE_BIT0_UPPER, LOW);
        srp.writePin(AMP_MODE_BIT1_UPPER, HIGH);
        oldvcaLoop = statevcaLoopU;
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
        oldvcaLoop = statevcaLoopU;
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
    switch (statevcaLoopL) {
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
        oldvcaLoop = statevcaLoopL;
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
        oldvcaLoop = statevcaLoopL;
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
    if (vcaVelU == 0) {
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
    if (vcaVelL == 0) {
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
    if (vcaGateU == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "Off");
        midiCCOut(CCvcaGate, 1);
      }
      sr.writePin(VCAGATE_LED, LOW);  // LED off
      ampAttackU = oldampAttackU;
      ampDecayU = oldampDecayU;
      ampSustainU = oldampSustainU;
      ampReleaseU = oldampReleaseU;
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "On");
        midiCCOut(CCvcaGate, 127);
      }
      sr.writePin(VCAGATE_LED, HIGH);  // LED on
      ampAttackU = 0;
      ampDecayU = 0;
      ampSustainU = 1023;
      ampReleaseU = 0;
    }
  } else {
    if (vcaGateL == 0) {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "Off");
        midiCCOut(CCvcaGate, 1);
      }
      sr.writePin(VCAGATE_LED, LOW);  // LED off
      ampAttackL = oldampAttackL;
      ampDecayL = oldampDecayL;
      ampSustainL = oldampSustainL;
      ampReleaseL = oldampReleaseL;
      if (wholemode) {
        ampAttackU = oldampAttackU;
        ampDecayU = oldampDecayU;
        ampSustainU = oldampSustainU;
        ampReleaseU = oldampReleaseU;
      }
    } else {
      if (announce) {
        showCurrentParameterPage("VCA Gate", "On");
        midiCCOut(CCvcaGate, 127);
      }
      sr.writePin(VCAGATE_LED, HIGH);  // LED on
      ampAttackL = 0;
      ampDecayL = 0;
      ampSustainL = 1023;
      ampReleaseL = 0;
      if (wholemode) {
        ampAttackU = 0;
        ampDecayU = 0;
        ampSustainU = 1023;
        ampReleaseU = 0;
      }
    }
  }
}

void updatelfoAlt(boolean announce) {
  if (upperSW) {
    if (lfoAltU == 0) {
      if (announce) {
        //showCurrentParameterPage("LFO Waveform", String("Original"));
        updateStratusLFOWaveform();
        midiCCOut(CClfoAlt, 1);
      }
      sr.writePin(LFO_ALT_LED, LOW);  // LED off
      srp.writePin(LFO_ALT_UPPER, HIGH);
    } else {
      if (announce) {
        //showCurrentParameterPage("LFO Waveform", String("Alternate"));
        updateStratusLFOWaveform();
        midiCCOut(CClfoAlt, 127);
      }
      sr.writePin(LFO_ALT_LED, HIGH);  // LED on
      srp.writePin(LFO_ALT_UPPER, LOW);
    }
  } else {
    if (lfoAltL == 0) {
      if (announce) {
        //showCurrentParameterPage("LFO Waveform", String("Original"));
        updateStratusLFOWaveform();
        midiCCOut(CClfoAlt, 1);
      }
      sr.writePin(LFO_ALT_LED, LOW);  // LED off
      srp.writePin(LFO_ALT_LOWER, HIGH);
      if (wholemode) {
        srp.writePin(LFO_ALT_UPPER, HIGH);
      }
    } else {
      if (announce) {
        //showCurrentParameterPage("LFO Waveform", String("Alternate"));
        updateStratusLFOWaveform();
        midiCCOut(CClfoAlt, 127);
      }
      sr.writePin(LFO_ALT_LED, HIGH);  // LED on
      srp.writePin(LFO_ALT_LOWER, LOW);
      if (wholemode) {
        srp.writePin(LFO_ALT_UPPER, LOW);
      }
    }
  }
}

void updatekeyTrackSW(boolean announce) {
  if (upperSW) {
    if (keyTrackSWU == 0) {
      srp.writePin(FILTER_KEYTRACK_UPPER, LOW);
      midiCCOutCPU5(CCkeyTrackSW, 1, 1);
      delay(1);
      midiCCOutCPU5(CCkeyTrack, 1, 1);
    } else {
      srp.writePin(FILTER_KEYTRACK_UPPER, HIGH);
      midiCCOutCPU5(CCkeyTrackSW, 127, 1);
      delay(1);
      midiCCOutCPU5(CCkeyTrack, keytrackU, 1);
    }
  } else {
    if (keyTrackSWL == 0) {
      srp.writePin(FILTER_KEYTRACK_LOWER, LOW);
      midiCCOutCPU2(CCkeyTrackSW, 1, 1);
      delay(1);
      midiCCOutCPU2(CCkeyTrack, 1, 1);
      if (wholemode) {
        srp.writePin(FILTER_KEYTRACK_UPPER, LOW);
        midiCCOutCPU5(CCkeyTrackSW, 1, 1);
        delay(1);
        midiCCOutCPU5(CCkeyTrack, 1, 1);
      }
    } else {
      srp.writePin(FILTER_KEYTRACK_LOWER, HIGH);
      midiCCOutCPU2(CCkeyTrackSW, 127, 1);
      delay(1);
      midiCCOutCPU2(CCkeyTrack, keytrackL, 1);
      if (wholemode) {
        srp.writePin(FILTER_KEYTRACK_UPPER, HIGH);
        midiCCOutCPU5(CCkeyTrackSW, 127, 1);
        delay(1);
        midiCCOutCPU5(CCkeyTrack, keytrackU, 1);
      }
    }
  }
}

void updateupperLower() {
  if (!wholemode) {
    if (upperSW) {
      // upper mode upperSW will be true
      sr.writePin(UPPER_LED, HIGH);  // LED off
      //srp.writePin(UPPER2, LOW);
      setAllButtons();
    } else {
      // lower mode upperSW will be false
      sr.writePin(UPPER_LED, LOW);  // LED off
      //srp.writePin(UPPER2, HIGH);
      setAllButtons();
    }
  } else {
    upperSW = 0;
  }
}

void updatewholemode() {
  allNotesOff();
  showCurrentParameterPage("Mode", String("Whole"));
  sr.writePin(WHOLE_LED, HIGH);  // LED off
  sr.writePin(DUAL_LED, LOW);    // LED off
  sr.writePin(SPLIT_LED, LOW);   // LED off
  sr.writePin(UPPER_LED, LOW);   // LED off
  srp.writePin(UPPER2, HIGH);
  upperSW = 0;
  setAllButtons();
  dualmode = false;
  splitmode = false;
}

void updatedualmode() {
  allNotesOff();
  showCurrentParameterPage("Mode", String("Dual"));
  sr.writePin(DUAL_LED, HIGH);  // LED off
  sr.writePin(WHOLE_LED, LOW);  // LED off
  sr.writePin(SPLIT_LED, LOW);  // LED off
  srp.writePin(UPPER2, LOW);
  wholemode = false;
  splitmode = false;
}

void updatesplitmode() {
  allNotesOff();
  showCurrentParameterPage("Mode", String("Split"));
  sr.writePin(SPLIT_LED, HIGH);  // LED off
  sr.writePin(WHOLE_LED, LOW);   // LED off
  sr.writePin(DUAL_LED, LOW);    // LED off
  srp.writePin(UPPER2, LOW);
  wholemode = false;
  dualmode = false;
}

void updatechorus1(boolean announce) {
  if (upperSW) {
    if (!chorus1U) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("Off"));
        midiCCOut(CCchorus1, 1);
      }
      sr.writePin(CHORUS1_LED, LOW);  // LED off
      srp.writePin(CHORUS1_OUT_UPPER, LOW);
    }
    if (chorus1U) {
      if (announce) {
        showCurrentParameterPage("Chorus 1", String("On"));
        midiCCOut(CCchorus1, 127);
      }
      sr.writePin(CHORUS1_LED, HIGH);  // LED on
      srp.writePin(CHORUS1_OUT_UPPER, HIGH);
    }
  }
  if (!upperSW) {
    if (!chorus1L) {
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
    if (chorus1L) {
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
    if (!chorus2U) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("Off"));
        midiCCOut(CCchorus2, 1);
      }
      sr.writePin(CHORUS2_LED, LOW);  // LED off
      srp.writePin(CHORUS2_OUT_UPPER, LOW);
    }
    if (chorus2U) {
      if (announce) {
        showCurrentParameterPage("Chorus 2", String("On"));
        midiCCOut(CCchorus2, 127);
      }
      sr.writePin(CHORUS2_LED, HIGH);  // LED on
      srp.writePin(CHORUS2_OUT_UPPER, HIGH);
    }
  }
  if (!upperSW) {
    if (!chorus2L) {
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
    if (chorus2L) {
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
  if (filterLogLinU == 0) {
    srp.writePin(FILTER_LIN_LOG_UPPER, HIGH);
  } else {
    srp.writePin(FILTER_LIN_LOG_UPPER, LOW);
  }
  if (filterLogLinL == 0) {
    srp.writePin(FILTER_LIN_LOG_LOWER, HIGH);
    if (wholemode) {
      srp.writePin(FILTER_LIN_LOG_UPPER, HIGH);
    }
  } else {
    srp.writePin(FILTER_LIN_LOG_LOWER, LOW);
    if (wholemode) {
      srp.writePin(FILTER_LIN_LOG_UPPER, LOW);
    }
  }
}

void updateAmpEnv(boolean announce) {
  if (ampLogLinU == 0) {
    srp.writePin(AMP_LIN_LOG_UPPER, LOW);
  } else {
    srp.writePin(AMP_LIN_LOG_UPPER, HIGH);
  }
  if (ampLogLinL == 0) {
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
    if (monoMultiU == 0) {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "Off");
      }
    } else {
      if (announce) {
        showCurrentParameterPage("LFO Retrigger", "On");
      }
    }
  } else {
    if (monoMultiL == 0) {
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
  showPatchPage(String(patchNoU), patchNameU, String(patchNoL), patchNameL);
}

void myConvertControlChange(byte channel, byte number, byte value) {
  newvalue = value;
  myControlChange(channel, number, newvalue);
}

void myControlChange(byte channel, byte control, int value) {

  switch (control) {
    case CCpwLFO:
      if (upperSW) {
        pwLFOU = value;
      } else {
        pwLFOL = value;
        if (wholemode) {
          pwLFOU = value;
        }
      }
      pwLFOstr = value / midioutfrig;  // for display
      updatepwLFO();
      break;

    case CCfmDepth:
      if (upperSW) {
        fmDepthU = value;
      } else {
        fmDepthL = value;
        if (wholemode) {
          fmDepthU = value;
        }
      }
      fmDepthstr = value / midioutfrig;
      updatefmDepth();
      break;

    case CCosc2PW:
      if (upperSW) {
        osc2PWU = value;
      } else {
        osc2PWL = value;
        if (wholemode) {
          osc2PWU = value;
        }
      }
      osc2PWstr = PULSEWIDTH[value / midioutfrig];
      updateosc2PW();
      break;

    case CCosc2PWM:
      if (upperSW) {
        osc2PWMU = value;
      } else {
        osc2PWML = value;
        if (wholemode) {
          osc2PWMU = value;
        }
      }
      osc2PWMstr = value / midioutfrig;
      updateosc2PWM();
      break;

    case CCosc1PW:
      if (upperSW) {
        osc1PWU = value;
      } else {
        osc1PWL = value;
        if (wholemode) {
          osc1PWU = value;
        }
      }
      osc1PWstr = PULSEWIDTH[value / midioutfrig];
      updateosc1PW();
      break;

    case CCosc1PWM:
      if (upperSW) {
        osc1PWMU = value;
      } else {
        osc1PWML = value;
        if (wholemode) {
          osc1PWMU = value;
        }
      }
      osc1PWMstr = value / midioutfrig;
      updateosc1PWM();
      break;

    case CCosc1Range:
      if (upperSW) {
        osc1RangeU = value;
      } else {
        osc1RangeL = value;
        if (wholemode) {
          osc1RangeU = value;
        }
      }
      osc1Rangestr = value / midioutfrig;
      updateosc1Range(1);
      break;

    case CCosc2Range:
      if (upperSW) {
        osc2RangeU = value;
      } else {
        osc2RangeL = value;
        if (wholemode) {
          osc2RangeU = value;
        }
      }
      osc2Rangestr = value / midioutfrig;
      updateosc2Range(1);
      break;

    case CCstack:
      if (upperSW) {
        stackU = value;
      } else {
        stackL = value;
        if (wholemode) {
          stackU = value;
        }
      }
      stackstr = value >> 7;
      updatestack();
      break;

    case CCglideTime:
      if (upperSW) {
        glideTimeU = value;
        midiCCOutCPU5(CCglideTime, (glideTimeU / midioutfrig), 1);
      } else {
        glideTimeL = value;
        midiCCOutCPU2(CCglideTime, (glideTimeL / midioutfrig), 1);
        if (wholemode) {
          glideTimeU = value;
          midiCCOutCPU5(CCglideTime, (glideTimeU / midioutfrig), 1);
        }
      }
      glideTimestr = LINEAR[value / midioutfrig];
      updateglideTime();
      break;

    case CCosc2Detune:
      if (upperSW) {
        osc2DetuneU = value;
      } else {
        osc2DetuneL = value;
        if (wholemode) {
          osc2DetuneU = value;
        }
      }
      osc2Detunestr = PULSEWIDTH[value / midioutfrig];
      updateosc2Detune();
      break;

    case CCnoiseLevel:
      if (upperSW) {
        noiseLevelU = value;
      } else {
        noiseLevelL = value;
        if (wholemode) {
          noiseLevelU = value;
        }
      }
      noiseLevelstr = LINEARCENTREZERO[value / midioutfrig];
      updatenoiseLevel();
      break;

    case CCosc2SawLevel:
      if (upperSW) {
        osc2SawLevelU = value;
      } else {
        osc2SawLevelL = value;
        if (wholemode) {
          osc2SawLevelU = value;
        }
      }
      osc2SawLevelstr = value / midioutfrig;  // for display
      updateOsc2SawLevel();
      break;

    case CCosc1SawLevel:
      if (upperSW) {
        osc1SawLevelU = value;
      } else {
        osc1SawLevelL = value;
        if (wholemode) {
          osc1SawLevelU = value;
        }
      }
      osc1SawLevelstr = value / midioutfrig;  // for display
      updateOsc1SawLevel();
      break;

    case CCosc2PulseLevel:
      if (upperSW) {
        osc2PulseLevelU = value;
      } else {
        osc2PulseLevelL = value;
        if (wholemode) {
          osc2PulseLevelU = value;
        }
      }
      osc2PulseLevelstr = value / midioutfrig;  // for display
      updateosc2PulseLevel();
      break;

    case CCosc1PulseLevel:
      if (upperSW) {
        osc1PulseLevelU = value;
      } else {
        osc1PulseLevelL = value;
        if (wholemode) {
          osc1PulseLevelU = value;
        }
      }
      osc1PulseLevelstr = value / midioutfrig;  // for display
      updateOsc1PulseLevel();
      break;

    case CCosc2TriangleLevel:
      if (upperSW) {
        osc2TriangleLevelU = value;
      } else {
        osc2TriangleLevelL = value;
        if (wholemode) {
          osc2TriangleLevelU = value;
        }
      }
      osc2TriangleLevelstr = value / midioutfrig;  // for display
      updateosc2TriangleLevel();
      break;

    case CCosc1SubLevel:
      if (upperSW) {
        osc1SubLevelU = value;
      } else {
        osc1SubLevelL = value;
        if (wholemode) {
          osc1SubLevelU = value;
        }
      }
      osc1SubLevelstr = value / midioutfrig;  // for display
      updateOsc1SubLevel();
      break;

    case CCLFODelay:
      if (upperSW) {
        LFODelayU = value;
      } else {
        LFODelayL = value;
        if (wholemode) {
          LFODelayU = value;
        }
      }
      LFODelaystr = value / midioutfrig;  // for display
      updateLFODelay();
      break;

    case CCfilterCutoff:
      if (upperSW) {
        filterCutoffU = value;
        oldfilterCutoffU = value;
      } else {
        filterCutoffL = value;
        oldfilterCutoffL = value;
        if (wholemode) {
          filterCutoffU = value;
          oldfilterCutoffU = value;
        }
      }
      filterCutoffstr = FILTERCUTOFF[value / midioutfrig];
      updateFilterCutoff();
      break;

    case CCfilterLFO:
      if (upperSW) {
        filterLFOU = value;
      } else {
        filterLFOL = value;
        if (wholemode) {
          filterLFOU = value;
        }
      }
      filterLFOstr = value / midioutfrig;
      updatefilterLFO();
      break;

    case CCfilterRes:
      if (upperSW) {
        filterResU = value;
      } else {
        filterResL = value;
        if (wholemode) {
          filterResU = value;
        }
      }
      filterResstr = int(value / midioutfrig);
      updatefilterRes();
      break;

    case CCfilterType:
      if (upperSW) {
        filterTypeU = value >> 7;
      } else {
        filterTypeL = value >> 7;
        if (wholemode) {
          filterTypeU = value >> 7;
        }
      }
      updateFilterType(1);
      break;

    case CCfilterEGlevel:
      if (upperSW) {
        filterEGlevelU = value;
      } else {
        filterEGlevelL = value;
        if (wholemode) {
          filterEGlevelU = value;
        }
      }
      filterEGlevelstr = int(value / midioutfrig);
      updatefilterEGlevel();
      break;

    case CCLFORate:
      if (upperSW) {
        LFORateU = value;
      } else {
        LFORateL = value;
        if (wholemode) {
          LFORateU = value;
        }
      }
      LFORatestr = LFOTEMPO[value / midioutfrig];  // for display
      updateLFORate();
      break;

    case CCLFOWaveform:
      if (upperSW) {
        LFOWaveformU = value;
      } else {
        LFOWaveformL = value;
        if (wholemode) {
          LFOWaveformU = value;
        }
      }
      LFOWaveformstr = value >> 7;
      updateStratusLFOWaveform();
      break;

    case CCfilterAttack:
      if (upperSW) {
        filterAttackU = value;
      } else {
        filterAttackL = value;
        if (wholemode) {
          filterAttackU = value;
        }
      }
      filterAttackstr = ENVTIMES[value / midioutfrig];
      updatefilterAttack();
      break;

    case CCfilterDecay:
      if (upperSW) {
        filterDecayU = value;
      } else {
        filterDecayL = value;
        if (wholemode) {
          filterDecayU = value;
        }
      }
      filterDecaystr = ENVTIMES[value / midioutfrig];
      updatefilterDecay();
      break;

    case CCfilterSustain:
      if (upperSW) {
        filterSustainU = value;
      } else {
        filterSustainL = value;
        if (wholemode) {
          filterSustainU = value;
        }
      }
      filterSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updatefilterSustain();
      break;

    case CCfilterRelease:
      if (upperSW) {
        filterReleaseU = value;
      } else {
        filterReleaseL = value;
        if (wholemode) {
          filterReleaseU = value;
        }
      }
      filterReleasestr = ENVTIMES[value / midioutfrig];
      updatefilterRelease();
      break;

    case CCampAttack:
      if (upperSW) {
        ampAttackU = value;
        oldampAttackU = value;
      } else {
        ampAttackL = value;
        oldampAttackL = value;
        if (wholemode) {
          ampAttackU = value;
          oldampAttackU = value;
        }
      }
      ampAttackstr = ENVTIMES[value / midioutfrig];
      updateampAttack();
      break;

    case CCampDecay:
      if (upperSW) {
        ampDecayU = value;
        oldampDecayU = value;
      } else {
        ampDecayL = value;
        oldampDecayL = value;
        if (wholemode) {
          ampDecayU = value;
          oldampDecayU = value;
        }
      }
      ampDecaystr = ENVTIMES[value / midioutfrig];
      updateampDecay();
      break;

    case CCampSustain:
      if (upperSW) {
        ampSustainU = value;
        oldampSustainU = value;
      } else {
        ampSustainL = value;
        oldampSustainL = value;
        if (wholemode) {
          ampSustainU = value;
          oldampSustainU = value;
        }
      }
      ampSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updateampSustain();
      break;

    case CCampRelease:
      if (upperSW) {
        ampReleaseU = value;
        oldampReleaseU = value;
      } else {
        ampReleaseL = value;
        oldampReleaseL = value;
        if (wholemode) {
          ampReleaseU = value;
          oldampReleaseU = value;
        }
      }
      ampReleasestr = ENVTIMES[value / midioutfrig];
      updateampRelease();
      break;

    case CCvolumeControl:
      if (upperSW) {
        volumeControlU = value;
      } else {
        volumeControlL = value;
        if (wholemode) {
          volumeControlU = value;
        }
      }
      volumeControlstr = value / midioutfrig;
      updatevolumeControl();
      break;

    case CCkeyTrack:
      if (upperSW) {
        keytrackU = value;
        midiCCOutCPU5(CCkeyTrack, (keytrackU / midioutfrig), 1);
      } else {
        keytrackL = value;
        midiCCOutCPU2(CCkeyTrack, (keytrackL / midioutfrig), 1);
        if (wholemode) {
          keytrackU = value;
          midiCCOutCPU5(CCkeyTrack, (keytrackU / midioutfrig), 1);
        }
      }
      keytrackstr = value / midioutfrig;
      updatekeytrack();
      break;


    case CCamDepth:
      if (upperSW) {
        amDepthU = value;
      } else {
        amDepthL = value;
        if (wholemode) {
          amDepthU = value;
        }
      }
      amDepth = value;
      amDepthstr = value / midioutfrig;
      updateamDepth();
      break;

      ////////////////////////////////////////////////

    case CCglideSW:
      if (upperSW) {
        glideSWU = !glideSWU;
      } else {
        glideSWL = !glideSWL;
      }
      updateglideSW(1);
      break;

    case CCfilterPoleSW:
      if (upperSW) {
        filterPoleSWU = !filterPoleSWU;
      } else {
        filterPoleSWL = !filterPoleSWL;
      }
      updatefilterPoleSwitch(1);
      break;

    case CCfilterVel:
      if (upperSW) {
        filterVelU = !filterVelU;
      } else {
        filterVelL = !filterVelL;
      }
      updatefilterVel(1);
      break;

    case CCfilterEGinv:
      if (upperSW) {
        filterEGinvU = !filterEGinvU;
      } else {
        filterEGinvL = !filterEGinvL;
      }
      updatefilterEGinv(1);
      break;

    case CCfilterLoop:
      if (upperSW) {
        statefilterLoopU = statefilterLoop;
      } else {
        statefilterLoopL = statefilterLoop;
      }
      updatefilterLoop(1);
      break;

    case CCvcaLoop:
      if (upperSW) {
        statevcaLoopU = statevcaLoop;
      } else {
        statevcaLoopL = statevcaLoop;
      }
      updatevcaLoop(1);
      break;

    case CCvcaVel:
      if (upperSW) {
        vcaVelU = !vcaVelU;
      } else {
        vcaVelL = !vcaVelL;
      }
      updatevcaVel(1);
      break;

    case CCvcaGate:
      if (upperSW) {
        vcaGateU = !vcaGateU;
      } else {
        vcaGateL = !vcaGateL;
      }
      updatevcaGate(1);
      break;

    case CCchorus1:
      if (upperSW) {
        chorus1U = !chorus1U;
      } else {
        chorus1L = !chorus1L;
      }
      updatechorus1(1);
      break;

    case CCchorus2:
      if (upperSW) {
        chorus2U = !chorus2U;
      } else {
        chorus2L = !chorus2L;
      }
      updatechorus2(1);
      break;

    case CCwholemode:
      wholemode = true;
      updatewholemode();
      break;

    case CCdualmode:
      dualmode = true;
      updatedualmode();
      break;

    case CCsplitmode:
      splitmode = true;
      updatesplitmode();
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
        value > 0 ? lfoAltU = 1 : lfoAltU = 0;
      } else {
        value > 0 ? lfoAltL = 1 : lfoAltL = 0;
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
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 2:
          modWheelLevel = ((value) / 4);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 3:
          modWheelLevel = ((value) / 3.5);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 4:
          modWheelLevel = ((value) / 3);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 5:
          modWheelLevel = ((value) / 2.5);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 6:
          modWheelLevel = ((value) / 2);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 7:
          modWheelLevel = ((value) / 1.75);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 8:
          modWheelLevel = ((value) / 1.5);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 9:
          modWheelLevel = ((value) / 1.25);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;

        case 10:
          modWheelLevel = (value);
          fmDepthU = int(modWheelLevel);
          fmDepthL = int(modWheelLevel);
          break;
      }
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  state = PARAMETER;
}

void myAfterTouch(byte channel, byte value) {
  afterTouch = int(value * MIDICCTOPOT);
  switch (AfterTouchDestU) {
    case 1:
      fmDepthU = afterTouch;
      break;
    case 2:
      filterCutoffU = (oldfilterCutoffU + afterTouch);
      if (afterTouch < 10) {
        filterCutoffU = oldfilterCutoffU;
      }
      if (filterCutoffU > 1023) {
        filterCutoffU = 1023;
      }
      break;
    case 3:
      filterLFOU = afterTouch;
      break;
    case 4:
      amDepthU = afterTouch;
      break;
  }
  switch (AfterTouchDestL) {
    case 1:
      fmDepthL = afterTouch;
      break;
    case 2:
      filterCutoffL = (oldfilterCutoffL + afterTouch);
      if (afterTouch < 10) {
        filterCutoffL = oldfilterCutoffL;
      }
      if (filterCutoffL > 1023) {
        filterCutoffL = 1023;
      }
      break;
    case 3:
      filterLFOL = afterTouch;
      break;
    case 4:
      amDepthL = afterTouch;
      break;
  }
}

void recallPatch(int patchNo) {
  allNotesOff();
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    //Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];  //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
    if (upperSW) {
      storeLastPatchU(patchNoU);
      upperpatchtag = patchNoU;
    } else {
      storeLastPatchL(patchNoL);
      lowerpatchtag = patchNoL;
    }
  }
}

void setCurrentPatchData(String data[]) {
  if (upperSW) {
    patchNameU = data[0];
    pwLFOU = data[1].toInt();
    fmDepthU = data[2].toInt();
    osc2PWU = data[3].toInt();
    osc2PWMU = data[4].toInt();
    osc1PWU = data[5].toInt();
    osc1PWMU = data[6].toInt();
    osc1RangeU = data[7].toInt();
    osc2RangeU = data[8].toInt();
    stackU = data[9].toInt();
    glideTimeU = data[10].toInt();
    osc2DetuneU = data[11].toInt();
    noiseLevelU = data[12].toInt();
    osc2SawLevelU = data[13].toInt();
    osc1SawLevelU = data[14].toInt();
    osc2PulseLevelU = data[15].toInt();
    osc1PulseLevelU = data[16].toInt();
    filterCutoffU = data[17].toInt();
    filterLFOU = data[18].toInt();
    filterResU = data[19].toInt();
    filterTypeU = data[20].toInt();
    filterdoubleLoopU = data[21].toInt();
    vcadoubleLoopU = data[22].toInt();
    LFODelayGoU = data[23].toInt();
    filterEGlevelU = data[24].toInt();
    LFORateU = data[25].toInt();
    LFOWaveformU = data[26].toInt();
    filterAttackU = data[27].toInt();
    filterDecayU = data[28].toInt();
    filterSustainU = data[29].toInt();
    filterReleaseU = data[30].toInt();
    ampAttackU = data[31].toInt();
    ampDecayU = data[32].toInt();
    ampSustainU = data[33].toInt();
    ampReleaseU = data[34].toInt();
    volumeControlU = data[35].toInt();
    glideSWU = data[36].toInt();
    keytrackU = data[37].toInt();
    filterPoleSWU = data[38].toInt();
    filterLoopU = data[39].toInt();
    filterEGinvU = data[40].toInt();
    filterVelU = data[41].toInt();
    vcaLoopU = data[42].toInt();
    vcaVelU = data[43].toInt();
    vcaGateU = data[44].toInt();
    lfoAltU = data[45].toInt();
    chorus1U = data[46].toInt();
    chorus2U = data[47].toInt();
    monoMultiU = data[48].toInt();
    modWheelLevelU = data[49].toInt();
    PitchBendLevelU = data[50].toInt();
    amDepthU = data[51].toInt();
    oct1AU = data[52].toInt();
    oct1BU = data[53].toInt();
    oct2AU = data[54].toInt();
    oct2BU = data[55].toInt();
    oldampAttackU = data[56].toInt();
    oldampDecayU = data[57].toInt();
    oldampSustainU = data[58].toInt();
    oldampReleaseU = data[59].toInt();
    AfterTouchDestU = data[60].toInt();
    filterLogLinU = data[61].toInt();
    ampLogLinU = data[62].toInt();
    osc2TriangleLevelU = data[63].toInt();
    osc1SubLevelU = data[64].toInt();
    keyTrackSWU = data[65].toInt();
    LFODelayU = data[66].toInt();

    oldfilterCutoffU = filterCutoffU;

  } else {
    patchNameL = data[0];
    pwLFOL = data[1].toInt();
    fmDepthL = data[2].toInt();
    osc2PWL = data[3].toInt();
    osc2PWML = data[4].toInt();
    osc1PWL = data[5].toInt();
    osc1PWML = data[6].toInt();
    osc1RangeL = data[7].toInt();
    osc2RangeL = data[8].toInt();
    stackL = data[9].toInt();
    glideTimeL = data[10].toInt();
    osc2DetuneL = data[11].toInt();
    noiseLevelL = data[12].toInt();
    osc2SawLevelL = data[13].toInt();
    osc1SawLevelL = data[14].toInt();
    osc2PulseLevelL = data[15].toInt();
    osc1PulseLevelL = data[16].toInt();
    filterCutoffL = data[17].toInt();
    filterLFOL = data[18].toInt();
    filterResL = data[19].toInt();
    filterTypeL = data[20].toInt();
    filterdoubleLoopL = data[21].toInt();
    vcadoubleLoopL = data[22].toInt();
    LFODelayGoL = data[23].toInt();
    filterEGlevelL = data[24].toInt();
    LFORateL = data[25].toInt();
    LFOWaveformL = data[26].toInt();
    filterAttackL = data[27].toInt();
    filterDecayL = data[28].toInt();
    filterSustainL = data[29].toInt();
    filterReleaseL = data[30].toInt();
    ampAttackL = data[31].toInt();
    ampDecayL = data[32].toInt();
    ampSustainL = data[33].toInt();
    ampReleaseL = data[34].toInt();
    volumeControlL = data[35].toInt();
    glideSWL = data[36].toInt();
    keytrackL = data[37].toInt();
    filterPoleSWL = data[38].toInt();
    filterLoopL = data[39].toInt();
    filterEGinvL = data[40].toInt();
    filterVelL = data[41].toInt();
    vcaLoopL = data[42].toInt();
    vcaVelL = data[43].toInt();
    vcaGateL = data[44].toInt();
    lfoAltL = data[45].toInt();
    chorus1L = data[46].toInt();
    chorus2L = data[47].toInt();
    monoMultiL = data[48].toInt();
    modWheelLevelL = data[49].toInt();
    PitchBendLevelL = data[50].toInt();
    amDepthL = data[51].toInt();
    oct1AL = data[52].toInt();
    oct1BL = data[53].toInt();
    oct2AL = data[54].toInt();
    oct2BL = data[55].toInt();
    oldampAttackL = data[56].toInt();
    oldampDecayL = data[57].toInt();
    oldampSustainL = data[58].toInt();
    oldampReleaseL = data[59].toInt();
    AfterTouchDestL = data[60].toInt();
    filterLogLinL = data[61].toInt();
    ampLogLinL = data[62].toInt();
    osc2TriangleLevelL = data[63].toInt();
    osc1SubLevelL = data[64].toInt();
    keyTrackSWL = data[65].toInt();
    LFODelayL = data[66].toInt();

    oldfilterCutoffL = filterCutoffL;

    if (wholemode) {
      patchNameU = data[0];
      pwLFOU = data[1].toInt();
      fmDepthU = data[2].toInt();
      osc2PWU = data[3].toInt();
      osc2PWMU = data[4].toInt();
      osc1PWU = data[5].toInt();
      osc1PWMU = data[6].toInt();
      osc1RangeU = data[7].toInt();
      osc2RangeU = data[8].toInt();
      stackU = data[9].toInt();
      glideTimeU = data[10].toInt();
      osc2DetuneU = data[11].toInt();
      noiseLevelU = data[12].toInt();
      osc2SawLevelU = data[13].toInt();
      osc1SawLevelU = data[14].toInt();
      osc2PulseLevelU = data[15].toInt();
      osc1PulseLevelU = data[16].toInt();
      filterCutoffU = data[17].toInt();
      filterLFOU = data[18].toInt();
      filterResU = data[19].toInt();
      filterTypeU = data[20].toInt();
      filterdoubleLoopU = data[21].toInt();
      vcadoubleLoopU = data[22].toInt();
      LFODelayGoU = data[23].toInt();
      filterEGlevelU = data[24].toInt();
      LFORateU = data[25].toInt();
      LFOWaveformU = data[26].toInt();
      filterAttackU = data[27].toInt();
      filterDecayU = data[28].toInt();
      filterSustainU = data[29].toInt();
      filterReleaseU = data[30].toInt();
      ampAttackU = data[31].toInt();
      ampDecayU = data[32].toInt();
      ampSustainU = data[33].toInt();
      ampReleaseU = data[34].toInt();
      volumeControlU = data[35].toInt();
      glideSWU = data[36].toInt();
      keytrackU = data[37].toInt();
      filterPoleSWU = data[38].toInt();
      filterLoopU = data[39].toInt();
      filterEGinvU = data[40].toInt();
      filterVelU = data[41].toInt();
      vcaLoopU = data[42].toInt();
      vcaVelU = data[43].toInt();
      vcaGateU = data[44].toInt();
      lfoAltU = data[45].toInt();
      chorus1U = data[46].toInt();
      chorus2U = data[47].toInt();
      monoMultiU = data[48].toInt();
      modWheelLevelU = data[49].toInt();
      PitchBendLevelU = data[50].toInt();
      amDepthU = data[51].toInt();
      oct1AU = data[52].toInt();
      oct1BU = data[53].toInt();
      oct2AU = data[54].toInt();
      oct2BU = data[55].toInt();
      oldampAttackU = data[56].toInt();
      oldampDecayU = data[57].toInt();
      oldampSustainU = data[58].toInt();
      oldampReleaseU = data[59].toInt();
      AfterTouchDestU = data[60].toInt();
      filterLogLinU = data[61].toInt();
      ampLogLinU = data[62].toInt();
      osc2TriangleLevelU = data[63].toInt();
      osc1SubLevelU = data[64].toInt();
      keyTrackSWU = data[65].toInt();
      LFODelayU = data[66].toInt();

      oldfilterCutoffU = filterCutoffU;
    }
  }

  if (wholemode) {
    //Switches
    oldupperSW = upperSW;
    for (upperSW = 0; upperSW < 2; upperSW++) {
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
      updateosc1Range(0);
      updateosc2Range(0);
      updateFilterType(0);
      updateMonoMulti(0);
      updateFilterEnv(0);
      updateAmpEnv(0);
      updateglideSW(0);
      updatekeyTrackSW(0);
    }
    upperSW = oldupperSW;

  } else {
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
    updateosc1Range(0);
    updateosc2Range(0);
    updateFilterType(0);
    updateMonoMulti(0);
    updateFilterEnv(0);
    updateAmpEnv(0);
    updateglideSW(0);
    updatekeyTrackSW(0);
  }

  //Patchname
  updatePatchname();
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
    return patchNameU + "," + String(pwLFOU) + "," + String(fmDepthU) + "," + String(osc2PWU) + "," + String(osc2PWMU) + "," + String(osc1PWU) + "," + String(osc1PWMU) + "," + String(osc1RangeU) + "," + String(osc2RangeU) + "," + String(stackU) + "," + String(glideTimeU) + "," + String(osc2DetuneU) + "," + String(noiseLevelU) + "," + String(osc2SawLevelU) + "," + String(osc1SawLevelU) + "," + String(osc2PulseLevelU) + "," + String(osc1PulseLevelU) + "," + String(filterCutoffU) + "," + String(filterLFOU) + "," + String(filterResU) + "," + String(filterTypeU) + "," + String(filterdoubleLoopU) + "," + String(vcadoubleLoopU) + "," + String(LFODelayGoU) + "," + String(filterEGlevelU) + "," + String(LFORateU) + "," + String(LFOWaveformU) + "," + String(filterAttackU) + "," + String(filterDecayU) + "," + String(filterSustainU) + "," + String(filterReleaseU) + "," + String(ampAttackU) + "," + String(ampDecayU) + "," + String(ampSustainU) + "," + String(ampReleaseU) + "," + String(volumeControlU) + "," + String(glideSWU) + "," + String(keytrackU) + "," + String(filterPoleSWU) + "," + String(filterLoopU) + "," + String(filterEGinvU) + "," + String(filterVelU) + "," + String(vcaLoopU) + "," + String(vcaVelU) + "," + String(vcaGateU) + "," + String(lfoAltU) + "," + String(chorus1U) + "," + String(chorus2U) + "," + String(monoMultiU) + "," + String(modWheelLevelU) + "," + String(PitchBendLevelU) + "," + String(amDepthU) + "," + String(oct1AU) + "," + String(oct1BU) + "," + String(oct2AU) + "," + String(oct2BU) + "," + String(oldampAttackU) + "," + String(oldampDecayU) + "," + String(oldampSustainU) + "," + String(oldampReleaseU) + "," + String(AfterTouchDestU) + "," + String(filterLogLinU) + "," + String(ampLogLinU) + "," + String(osc2TriangleLevelU) + "," + String(osc1SubLevelU) + "," + String(keyTrackSWU) + "," + String(LFODelayU);
  } else {
    return patchNameL + "," + String(pwLFOL) + "," + String(fmDepthL) + "," + String(osc2PWL) + "," + String(osc2PWML) + "," + String(osc1PWL) + "," + String(osc1PWML) + "," + String(osc1RangeL) + "," + String(osc2RangeL) + "," + String(stackL) + "," + String(glideTimeL) + "," + String(osc2DetuneL) + "," + String(noiseLevelL) + "," + String(osc2SawLevelL) + "," + String(osc1SawLevelL) + "," + String(osc2PulseLevelL) + "," + String(osc1PulseLevelL) + "," + String(filterCutoffL) + "," + String(filterLFOL) + "," + String(filterResL) + "," + String(filterTypeL) + "," + String(filterdoubleLoopL) + "," + String(vcadoubleLoopL) + "," + String(LFODelayGoL) + "," + String(filterEGlevelL) + "," + String(LFORateL) + "," + String(LFOWaveformL) + "," + String(filterAttackL) + "," + String(filterDecayL) + "," + String(filterSustainL) + "," + String(filterReleaseL) + "," + String(ampAttackL) + "," + String(ampDecayL) + "," + String(ampSustainL) + "," + String(ampReleaseL) + "," + String(volumeControlL) + "," + String(glideSWL) + "," + String(keytrackL) + "," + String(filterPoleSWL) + "," + String(filterLoopL) + "," + String(filterEGinvL) + "," + String(filterVelL) + "," + String(vcaLoopL) + "," + String(vcaVelL) + "," + String(vcaGateL) + "," + String(lfoAltL) + "," + String(chorus1L) + "," + String(chorus2L) + "," + String(monoMultiL) + "," + String(modWheelLevelL) + "," + String(PitchBendLevelL) + "," + String(amDepthL) + "," + String(oct1AL) + "," + String(oct1BL) + "," + String(oct2AL) + "," + String(oct2BL) + "," + String(oldampAttackL) + "," + String(oldampDecayL) + "," + String(oldampSustainL) + "," + String(oldampReleaseL) + "," + String(AfterTouchDestL) + "," + String(filterLogLinL) + "," + String(ampLogLinL) + "," + String(osc2TriangleLevelL) + "," + String(osc1SubLevelL) + "," + String(keyTrackSWL) + "," + String(LFODelayL);
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
        midiCCOut(CCosc1PW, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc1PW, mux1Read);
        break;
      case MUX1_spare0:
        break;
      case MUX1_osc2PWM:
        midiCCOut(CCosc2PWM, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2PWM, mux1Read);
        break;
      case MUX1_osc2PW:
        midiCCOut(CCosc2PW, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2PW, mux1Read);
        break;
      case MUX1_spare4:
        break;
      case MUX1_osc1PWM:
        midiCCOut(CCosc1PWM, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc1PWM, mux1Read);
        break;
      case MUX1_pwLFO:
        midiCCOut(CCpwLFO, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCpwLFO, mux1Read);
        break;
      case MUX1_fmDepth:
        midiCCOut(CCfmDepth, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCfmDepth, mux1Read);
        break;
      case MUX1_osc2SawLevel:
        midiCCOut(CCosc2SawLevel, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2SawLevel, mux1Read);
        break;
      case MUX1_noiseLevel:
        midiCCOut(CCnoiseLevel, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCnoiseLevel, mux1Read);
        break;
      case MUX1_osc1SawLevel:
        midiCCOut(CCosc1SawLevel, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc1SawLevel, mux1Read);
        break;
      case MUX1_osc2Detune:
        midiCCOut(CCosc2Detune, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2Detune, mux1Read);
        break;
      case MUX1_osc1Range:
        midiCCOut(CCosc1Range, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc1Range, mux1Read);
        break;
      case MUX1_glideTime:
        midiCCOut(CCglideTime, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCglideTime, mux1Read);
        break;
      case MUX1_osc2Range:
        midiCCOut(CCosc2Range, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2Range, mux1Read);
        break;
      case MUX1_stack:
        midiCCOut(CCstack, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCstack, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;
    switch (muxInput) {
      case MUX2_osc2TriangleLevel:
        midiCCOut(CCosc2TriangleLevel, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCosc2TriangleLevel, mux2Read);
        break;
      case MUX2_filterLFO:
        midiCCOut(CCfilterLFO, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterLFO, mux2Read);
        break;
      case MUX2_filterCutoff:
        midiCCOut(CCfilterCutoff, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterCutoff, mux2Read);
        break;
      case MUX2_LFOWaveform:
        midiCCOut(CCLFOWaveform, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCLFOWaveform, mux2Read);
        break;
      case MUX2_osc1PulseLevel:
        midiCCOut(CCosc1PulseLevel, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCosc1PulseLevel, mux2Read);
        break;
      case MUX2_LFODelay:
        midiCCOut(CCLFODelay, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCLFODelay, mux2Read);
        break;
      case MUX2_osc2PulseLevel:
        midiCCOut(CCosc2PulseLevel, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCosc2PulseLevel, mux2Read);
        break;
      case MUX2_osc1SubLevel:
        midiCCOut(CCosc1SubLevel, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCosc1SubLevel, mux2Read);
        break;
      case MUX2_LFORate:
        midiCCOut(CCLFORate, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCLFORate, mux2Read);
        break;
      case MUX2_filterDecay:
        midiCCOut(CCfilterDecay, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterDecay, mux2Read);
        break;
      case MUX2_filterAttack:
        midiCCOut(CCfilterAttack, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterAttack, mux2Read);
        break;
      case MUX2_ampAttack:
        midiCCOut(CCampAttack, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCampAttack, mux2Read);
        break;
      case MUX2_filterRes:
        midiCCOut(CCfilterRes, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterRes, mux2Read);
        break;
      case MUX2_filterType:
        midiCCOut(CCfilterType, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterType, mux2Read);
        break;
      case MUX2_keyTrack:
        midiCCOut(CCkeyTrack, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCkeyTrack, mux2Read);
        break;
      case MUX2_filterEGlevel:
        midiCCOut(CCfilterEGlevel, mux2Read / midioutfrig);
        myControlChange(midiChannel, CCfilterEGlevel, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;
    switch (muxInput) {
      case MUX3_amDepth:
        midiCCOut(CCamDepth, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCamDepth, mux3Read);
        break;
      case MUX3_ampSustain:
        midiCCOut(CCampSustain, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCampSustain, mux3Read);
        break;
      case MUX3_ampDecay:
        midiCCOut(CCampDecay, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCampDecay, mux3Read);
        break;
      case MUX3_volumeControl:
        midiCCOut(CCvolumeControl, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCvolumeControl, mux3Read);
        break;
      case MUX3_filterSustain:
        midiCCOut(CCfilterSustain, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCfilterSustain, mux3Read);
        break;
      case MUX3_filterRelease:
        midiCCOut(CCfilterRelease, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCfilterRelease, mux3Read);
        break;
      case MUX3_ampRelease:
        midiCCOut(CCampRelease, mux3Read / midioutfrig);
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


void outputDAC(int CHIP_SELECT, uint32_t sample_data1, uint32_t sample_data2) {
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data1);
  //delayMicroseconds(5);
  digitalWriteFast(CHIP_SELECT, HIGH);
  digitalWriteFast(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data2);
  //delayMicroseconds(5);
  digitalWriteFast(CHIP_SELECT, HIGH);
  SPI.endTransaction();
}

void writeDemux() {

  switch (muxOutput) {
    case 0:
      switch (LFODelayGoU) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(fmDepthU * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (LFODelayGoL) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(fmDepthL * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterAttackU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterAttackL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 1:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc2PWMU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc2PWML * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterDecayU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterDecayL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 2:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc1PWMU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc1PWML * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterSustainU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterSustainL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 3:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(stackU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(stackL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterReleaseU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterReleaseL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 4:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc2DetuneU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc2DetuneL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(ampAttackU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(ampAttackL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 5:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(noiseLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(noiseLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(ampDecayU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(ampDecayL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 6:
      switch (LFODelayGoU) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(filterLFOU * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (LFODelayGoL) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(filterLFOL * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(ampSustainU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(ampSustainL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 7:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(volumeControlU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(volumeControlL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(ampReleaseU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(ampReleaseL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 8:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc1SawLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc1SawLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(pwLFOU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(pwLFOL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 9:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc1PulseLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc1PulseLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(LFORateU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(LFORateL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 10:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc2SawLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc2SawLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(LFOWaveformU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(LFOWaveformL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 11:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc2PulseLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc2PulseLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterEGlevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterEGlevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 12:  // was keytrack, but now moved to MIDI
      sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterCutoffU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterCutoffL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 13:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc1PWU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc1PWL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(filterResU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(filterResL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 14:
      sample_data1 = (channel_a & 0xFFF0000F) | (((int(osc2PWU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_b & 0xFFF0000F) | (((int(osc2PWL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(osc1SubLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(osc1SubLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;

    case 15:
      switch (LFODelayGoU) {
        case 1:
          sample_data1 = (channel_a & 0xFFF0000F) | (((int(amDepthU * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data1 = (channel_a & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      switch (LFODelayGoL) {
        case 1:
          sample_data2 = (channel_b & 0xFFF0000F) | (((int(amDepthL * DACMULT)) & 0xFFFF) << 4);
          break;

        case 0:
          sample_data2 = (channel_b & 0xFFF0000F) | ((0 & 0xFFFF) << 4);
          break;
      }
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_1, LOW);

      sample_data1 = (channel_c & 0xFFF0000F) | (((int(osc2TriangleLevelU * DACMULT)) & 0xFFFF) << 4);
      sample_data2 = (channel_d & 0xFFF0000F) | (((int(osc2TriangleLevelL * DACMULT)) & 0xFFFF) << 4);
      outputDAC(DAC_CS1, sample_data1, sample_data2);
      digitalWriteFast(DEMUX_EN_2, LOW);
      break;
  }
  delayMicroseconds(200);
  digitalWriteFast(DEMUX_EN_1, HIGH);
  digitalWriteFast(DEMUX_EN_2, HIGH);

  muxOutput++;
  if (muxOutput >= DEMUXCHANNELS)

    muxOutput = 0;

  digitalWriteFast(DEMUX_0, muxOutput & B0001);
  digitalWriteFast(DEMUX_1, muxOutput & B0010);
  digitalWriteFast(DEMUX_2, muxOutput & B0100);
  digitalWriteFast(DEMUX_3, muxOutput & B1000);
}


void checkEeprom() {

  if (oldsplitTrans != splitTrans) {
    setTranspose(splitTrans);
  }

  if (oldfilterLogLinU != filterLogLinU) {
    updateFilterEnv(0);
    oldfilterLogLinU = filterLogLinU;
  }

  if (oldfilterLogLinL != filterLogLinL) {
    updateFilterEnv(0);
    oldfilterLogLinL = filterLogLinL;
  }

  if (oldampLogLinU != ampLogLinU) {
    updateAmpEnv(0);
    oldampLogLinU = ampLogLinU;
  }

  if (oldampLogLinL != ampLogLinL) {
    updateAmpEnv(0);
    oldampLogLinL = ampLogLinL;
  }

  if (oldkeyTrackSWU != keyTrackSWU) {
    updatekeyTrackSW(0);
    oldkeyTrackSWU = keyTrackSWU;
  }

  if (oldkeyTrackSWL != keyTrackSWL) {
    updatekeyTrackSW(0);
    oldkeyTrackSWL = keyTrackSWL;
  }

  if (oldmonoMultiU != monoMultiU) {
    updateMonoMulti(0);
    oldmonoMultiU = monoMultiU;
  }

  if (oldmonoMultiL != monoMultiL) {
    updateMonoMulti(0);
    oldmonoMultiL = monoMultiL;
  }

  if (oldAfterTouchDestU != AfterTouchDestU) {
    oldAfterTouchDestU = AfterTouchDestU;
  }

  if (oldAfterTouchDestL != AfterTouchDestL) {
    oldAfterTouchDestL = AfterTouchDestL;
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

  if (btnIndex == UPPER_SW && btnType == ROX_PRESSED) {
    upperSW = !upperSW;
    myControlChange(midiChannel, CCupperSW, upperSW);
  }

  if (btnIndex == WHOLE_SW && btnType == ROX_PRESSED) {
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

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        break;
    }
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        if (patches.size() < PATCHES_LIMIT) {
          resetPatchesOrdering();  //Reset order of patches from first patch
          patches.push({ patches.size() + 1, INITPATCHNAME });
          state = SAVE;
        }
        break;
      case SAVE:
        //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
        if (upperSW) {
          patchNameU = patches.last().patchName;
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patches.last().patchName, "", "");
          patchNoU = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNoU);
          renamedPatch = "";
          state = PARAMETER;
        } else {
          patchNameL = patches.last().patchName;
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patches.last().patchName, "", "");
          patchNoL = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNoL);
          renamedPatch = "";
          state = PARAMETER;
        }
        break;
      case PATCHNAMING:
        if (upperSW) {
          if (renamedPatch.length() > 0) patchNameU = renamedPatch;  //Prevent empty strings
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patchName, "", "");
          patchNoU = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNoU);
          renamedPatch = "";
          state = PARAMETER;
        } else {
          if (renamedPatch.length() > 0) patchNameL = renamedPatch;  //Prevent empty strings
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patchNameL, "", "");
          patchNoL = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNoL);
          renamedPatch = "";
          state = PARAMETER;
        }
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();  //Hack
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
    allNotesOff();  //Hack
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
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
  } else if (recallButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = RECALL;  //show patch list
        break;
      case RECALL:
        state = PATCH;
        //Recall the current patch
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 13) {
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
    }
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

void checkEncoder() {
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        if (upperSW) {
          patches.push(patches.shift());
          patchNoU = patches.first().patchNo;
          recallPatch(patchNoU);
        } else {
          patches.push(patches.shift());
          patchNoL = patches.first().patchNo;
          recallPatch(patchNoL);
        }
        state = PARAMETER;
        break;
      case RECALL:
        patches.push(patches.shift());
        break;
      case SAVE:
        patches.push(patches.shift());
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;  //Wrap around
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.push(patches.shift());
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
    encPrevious = encRead;
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        if (upperSW) {
          patches.unshift(patches.pop());
          patchNoU = patches.first().patchNo;
          recallPatch(patchNoU);
        } else {
          patches.unshift(patches.pop());
          patchNoL = patches.first().patchNo;
          recallPatch(patchNoL);
        }
        state = PARAMETER;
        break;
      case RECALL:
        patches.unshift(patches.pop());
        break;
      case SAVE:
        patches.unshift(patches.pop());
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.unshift(patches.pop());
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
    encPrevious = encRead;
  }
}

void loop() {
  octoswitch.update();
  srp.update();
  sr.update();
  checkSwitches();
  checkEeprom();
  writeDemux();
  checkMux();
  checkEncoder();
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);
  LFODelayHandle();
}