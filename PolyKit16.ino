/*
  PolyKit DUO MUX - Firmware Rev 1.3

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html

  Arduino IDE
  Tools Settings:
  Board: "Teensy3.6"
  USB Type: "Serial + MIDI + Audio"
  CPU Speed: "180"
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
#include <ShiftRegister74HC595.h>
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

#include "ST7735Display.h"

boolean cardStatus = false;

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);


//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);  //RX - Pin 0


int count = 0;  //For MIDI Clk Sync
int DelayForSH3 = 30;
int patchNo = 0;
int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now
unsigned long buttonDebounce = 0;

ShiftRegister74HC595<2> sr(23, 22, 21);
ShiftRegister74HC595<3> srp(52, 53, 54);

#define OCTO_TOTAL 2
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;

// pins for 74HC165
#define PIN_DATA 50  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 49  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 51   // pin 2 on 74HC165 (CLK))

void setup() {
  SPI.begin();
  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);
  //sr.begin(LED_DATA, LED_LATCH, LED_CLK, LED_PWM);
  setupDisplay();
  setUpSettings();
  setupHardware();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus) {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0) {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  } else {
    Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  MIDI.turnThruOn(midi::Thru::Mode::Off);
  Serial.println("MIDI In DIN Listening");


  //Read Aftertouch from EEPROM, this can be set individually by each patch.
  AfterTouchDest = getAfterTouch();
  // Serial.print("Aftertouch Value ");
  // Serial.println(AfterTouchDest);
  if (AfterTouchDest < 0 || AfterTouchDest > 3) {
    storeAfterTouch(0);
  }

  //Read Pitch Bend Range from EEPROM
  //pitchBendRange = getPitchBendRange();

  //Read Mod Wheel Depth from EEPROM
  modWheelDepth = getModWheelDepth();

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  filterLogLin = getFilterEnv();
  ampLogLin = getAmpEnv();
  patchNo = getLastPatch();
  recallPatch(patchNo);  //Load first patch

  //  reinitialiseToPanel();

  // pinMode(48, OUTPUT);
  // pinMode(49, OUTPUT);
  // pinMode(50, OUTPUT);
  // pinMode(51, OUTPUT);
  // pinMode(52, OUTPUT);
  // pinMode(53, OUTPUT);
  // pinMode(54, OUTPUT);

  // digitalWrite(48, LOW);
  // digitalWrite(49, LOW);
  // digitalWrite(50, LOW);
  // digitalWrite(51, LOW);
  // digitalWrite(52, LOW);
  // digitalWrite(53, LOW);
  // digitalWrite(54, HIGH);
}

void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV) {
  int command = channel ? 0x9000 : 0x1000;

  command |= gain ? 0x0000 : 0x2000;
  command |= (mV & 0x0FFF);

  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(dacpin, LOW);
  SPI.transfer(command >> 8);
  SPI.transfer(command & 0xFF);
  digitalWrite(dacpin, HIGH);
  SPI.endTransaction();
}

void allNotesOff() {
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

void updateosc1Range() {
  if (osc1Range > 100) {
    showCurrentParameterPage("Osc1 Range", String("8"));
    oct1A = 0;
    oct1B = 1023;
  } else if (osc1Range < 100 && osc1Range > 33) {
    showCurrentParameterPage("Osc1 Range", String("16"));
    oct1A = 1023;
    oct1B = 1023;
  } else {
    showCurrentParameterPage("Osc1 Range", String("32"));
    oct1A = 1023;
    oct1B = 0;
  }
}

void updateosc2Range() {
  if (osc2Range > 100) {
    showCurrentParameterPage("Osc2 Range", String("8"));
    oct2A = 0;
    oct2B = 1023;
  } else if (osc2Range < 100 && osc2Range > 33) {
    showCurrentParameterPage("Osc2 Range", String("16"));
    oct2A = 1023;
    oct2B = 1023;
  } else {
    showCurrentParameterPage("Osc2 Range", String("32"));
    oct2A = 1023;
    oct2B = 0;
  }
}

void updatestack() {
  if (stack > 120) {
  showCurrentParameterPage("Voice Stack", String("8 Note"));
  } else if (stack < 120 && stack > 105) {
    showCurrentParameterPage("Voice Stack", String("7 Note"));
  } else if (stack < 105 && stack > 88) {
    showCurrentParameterPage("Voice Stack", String("6 Note"));
  } else if (stack < 88 && stack > 66) {
    showCurrentParameterPage("Voice Stack", String("5 Note"));
  } else if (stack < 66 && stack > 44) {
    showCurrentParameterPage("Voice Stack", String("4 Note"));
  } else if (stack < 44 && stack > 22) {
    showCurrentParameterPage("Voice Stack", String("3 Note"));
  } else if (stack < 22 && stack > 12) {
    showCurrentParameterPage("Voice Stack", String("2 Note"));
  } else {
    showCurrentParameterPage("Voice Stack", String("Poly"));
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

void updateOsc1TriangleLevel() {
  showCurrentParameterPage("OSC1 Triangle", int(osc1TriangleLevelstr));
}

void updatebalance() {
  showCurrentParameterPage("Layer Balance", int(balancestr));
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

void updateFilterType() {
  Serial.print("Filter Type ");
  Serial.println(filterType);
  if (filterType < 12) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("3P LowPass"));
    } else {
      showCurrentParameterPage("Filter Type", String("4P LowPass"));
    }
    filterA = 0;
    filterB = 0;
    filterC = 0;

  } else if (filterType > 12 && filterType < 22) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("1P LowPass"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P LowPass"));
    }
    filterA = 1023;
    filterB = 0;
    filterC = 0;

  } else if (filterType > 22 && filterType < 44) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("3P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("4P HighPass"));
    }
    filterA = 0;
    filterB = 1023;
    filterC = 0;

  } else if (filterType > 44 && filterType < 66) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("1P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P HighPass"));
    }
    filterA = 1023;
    filterB = 1023;
    filterC = 0;

  } else if (filterType > 66 && filterType < 88) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("4P BandPass"));
    }
    filterA = 0;
    filterB = 0;
    filterC = 1023;

  } else if (filterType > 88 && filterType < 105) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P BP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P BandPass"));
    }
    filterA = 1023;
    filterB = 0;
    filterC = 1023;

  } else if (filterType > 105 && filterType < 120) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("3P AP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("3P AllPass"));
    }
    filterA = 0;
    filterB = 1023;
    filterC = 1023;

  } else {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P Notch + LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("Notch"));
    }
    filterA = 1023;
    filterB = 1023;
    filterC = 1023;
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
  getStratusLFOWaveform(LFOWaveform);
  showCurrentParameterPage("LFO Wave", StratusLFOWaveform);
}

int getStratusLFOWaveform(int value) {
    Serial.print("LFO Type ");
  Serial.println(value);
  if (lfoAlt == 1) {    
    if (value >= 0 && value < 12) {
      StratusLFOWaveform = "Saw +Oct";
    } else if (value >= 12 && value < 22) {
      StratusLFOWaveform = "Quad Saw";
    } else if (value >= 22 && value < 44) {
      StratusLFOWaveform = "Quad Pulse";
    } else if (value >= 44 && value < 66) {
      StratusLFOWaveform = "Tri Step";
    } else if (value >= 66 && value < 88) {
      StratusLFOWaveform = "Sine +Oct";
    } else if (value >= 88 && value < 105) {
      StratusLFOWaveform = "Sine +3rd";
    } else if (value >= 105 && value < 120) {
      StratusLFOWaveform = "Sine +4th";
    } else if (value >= 120 && value < 128) {
      StratusLFOWaveform = "Rand Slopes";
    }
  } else {
    if (value >= 0 && value < 12) {
      StratusLFOWaveform = "Sawtooth Up";
    } else if (value >= 12 && value < 22) {
      StratusLFOWaveform = "Sawtooth Down";
    } else if (value >= 22 && value < 44) {
      StratusLFOWaveform = "Squarewave";
    } else if (value >= 44 && value < 66) {
      StratusLFOWaveform = "Triangle";
    } else if (value >= 66 && value < 88) {
      StratusLFOWaveform = "Sinewave";
    } else if (value >= 88 && value < 105) {
      StratusLFOWaveform = "Sweeps";
    } else if (value >= 105 && value < 120) {
      StratusLFOWaveform = "Lumps";
    } else if (value >= 120 && value < 128) {
      StratusLFOWaveform = "Sample & Hold";
    }
  }
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

void updateglideSW() {
  if (glideSW == 0) {
    showCurrentParameterPage("Glide", "Off");
    sr.set(GLIDE_LED, LOW);  // LED off
    midiCCOut(CCglideSW, 1);
    delay(1);
    midiCCOut(CCglideTime, 0);
  } else {
    showCurrentParameterPage("Glide", "On");
    sr.set(GLIDE_LED, HIGH);  // LED on
    midiCCOut(CCglideTime, int(glideTime / 8));
    delay(1);
    midiCCOut(CCglideSW, 127);
  }
}

void updatefilterPoleSwitch() {
  if (filterPoleSW == 1) {
    showCurrentParameterPage("VCF Pole", "On");
    sr.set(FILTERPOLE_LED, HIGH);  // LED on
    srp.set(FILTER_POLE, HIGH);
    midiCCOut(CCfilterPoleSW, 127);
  } else {
    showCurrentParameterPage("VCF Pole", "Off");
    sr.set(FILTERPOLE_LED, LOW);  // LED off
    srp.set(FILTER_POLE, LOW);
    midiCCOut(CCfilterPoleSW, 1);
  }
}

void updatefilterLoop() {
  if (filterLoop == 1) {
    showCurrentParameterPage("VCF EG Loop", "On");
    sr.set(FILTERLOOP_LED, HIGH);  // LED on
    srp.set(FILTER_LOOP, HIGH);
    midiCCOut(CCfilterLoop, 127);
  } else {
    showCurrentParameterPage("VCF EG Loop", "Off");
    sr.set(FILTERLOOP_LED, LOW);  // LED off
    srp.set(FILTER_LOOP, LOW);
    midiCCOut(CCfilterLoop, 1);
  }
}

void updatefilterEGinv() {
  if (filterEGinv == 0) {
    showCurrentParameterPage("Filter Env", "Positive");
    sr.set(FILTERINV_LED, LOW);  // LED off
    srp.set(FILTER_EG_INV, LOW);
    midiCCOut(CCfilterEGinv, 1);
  } else {
    showCurrentParameterPage("Filter Env", "Negative");
    sr.set(FILTERINV_LED, HIGH);  // LED on
    srp.set(FILTER_EG_INV, HIGH);
    midiCCOut(CCfilterEGinv, 127);
  }
}

void updatefilterVel() {
  if (filterVel == 0) {
    showCurrentParameterPage("VCF Velocity", "Off");
    sr.set(FILTERVEL_LED, LOW);  // LED off
    srp.set(FILTER_VELOCITY, LOW);
    midiCCOut(CCfilterVel, 1);
  } else {
    showCurrentParameterPage("VCF Velocity", "On");
    sr.set(FILTERVEL_LED, HIGH);  // LED on
    srp.set(FILTER_VELOCITY, HIGH);
    midiCCOut(CCfilterVel, 127);
  }
}

void updatevcaLoop() {
  if (vcaLoop == 1) {
    showCurrentParameterPage("VCA EG Loop", "On");
    sr.set(VCALOOP_LED, HIGH);  // LED on
    srp.set(AMP_LOOP, HIGH);
    midiCCOut(CCvcaLoop, 127);
  } else {
    showCurrentParameterPage("VCA EG Loop", "Off");
    sr.set(VCALOOP_LED, LOW);  // LED off
    srp.set(AMP_LOOP, LOW);
    midiCCOut(CCvcaLoop, 1);
  }
}

void updatevcaVel() {
  if (vcaVel == 0) {
    showCurrentParameterPage("VCA Velocity", "Off");
    sr.set(VCAVEL_LED, LOW);  // LED off
    srp.set(AMP_VELOCITY, LOW);
    midiCCOut(CCvcaVel, 1);
  } else {
    showCurrentParameterPage("VCA Velocity", "On");
    sr.set(VCAVEL_LED, HIGH);  // LED on
    srp.set(AMP_VELOCITY, HIGH);
    midiCCOut(CCvcaVel, 127);
  }
}

void updatevcaGate() {
  if (vcaGate == 0) {
    showCurrentParameterPage("VCA Gate", "Off");
    sr.set(VCAGATE_LED, LOW);  // LED off
    ampAttack = oldampAttack;
    ampDecay = oldampDecay;
    ampSustain = oldampSustain;
    ampRelease = oldampRelease;
    midiCCOut(CCvcaGate, 1);

  } else {
    showCurrentParameterPage("VCA Gate", "On");
    sr.set(VCAGATE_LED, HIGH);  // LED on
    ampAttack = 0;
    ampDecay = 0;
    ampSustain = 1023;
    ampRelease = 0;
    midiCCOut(CCvcaGate, 127);
  }
}

void updatelfoAlt() {
  if (lfoAlt == 0) {
    showCurrentParameterPage("LFO Waveform", String("Original"));
    sr.set(LFO_ALT_LED, LOW);  // LED off
    srp.set(LFO_ALT, HIGH);
    midiCCOut(CClfoAlt, 1);
  } else {
    showCurrentParameterPage("LFO Waveform", String("Alternate"));
    sr.set(LFO_ALT_LED, HIGH);  // LED on
    srp.set(LFO_ALT, LOW);
    midiCCOut(CClfoAlt, 127);
  }
}

void updatechorus1() {
  if (chorus1 == 0) {
    showCurrentParameterPage("Chorus 1", String("Off"));
    sr.set(CHORUS1_LED, LOW);  // LED off
    srp.set(CHORUS1_OUT, LOW);
    midiCCOut(CCchorus1, 1);
  } else {
    showCurrentParameterPage("Chorus 1", String("On"));
    sr.set(CHORUS1_LED, HIGH);  // LED on
    srp.set(CHORUS1_OUT, HIGH);
    midiCCOut(CCchorus1, 127);
  }
}

void updatechorus2() {
  if (chorus2 == 0) {
    showCurrentParameterPage("Chorus 2", String("Off"));
    sr.set(CHORUS2_LED, LOW);  // LED off
    srp.set(CHORUS2_OUT, LOW);
    midiCCOut(CCchorus2, 1);
  } else {
    showCurrentParameterPage("Chorus 2", String("On"));
    sr.set(CHORUS2_LED, HIGH);  // LED on
    srp.set(CHORUS2_OUT, HIGH);
    midiCCOut(CCchorus2, 127);
  }
}

void updateFilterEnv() {
  if (filterLogLin == 0) {
    srp.set(FILTER_LIN_LOG, HIGH);
  } else {
    srp.set(FILTER_LIN_LOG, LOW);
  }
}

void updateAmpEnv() {
  if (ampLogLin == 0) {
    srp.set(AMP_LIN_LOG, HIGH);
  } else {
    srp.set(AMP_LIN_LOG, LOW);
  }
}

void updateMonoMulti() {
  if (monoMulti == 0) {
    showCurrentParameterPage("LFO Retrigger", "Off");

  } else {
    showCurrentParameterPage("LFO Retrigger", "On");
  }
}

void updatePitchBend() {
  showCurrentParameterPage("Bender Range", int(PitchBendLevelstr));
}

void updatemodWheel() {
  showCurrentParameterPage("Mod Range", int(modWheelLevelstr));
}

void updatePatchname() {
  showPatchPage(String(patchNo), patchName);
}

void myControlChange(byte channel, byte control, int value) {

  //  Serial.println("MIDI: " + String(control) + " : " + String(value));
  switch (control) {
    case CCpwLFO:
      pwLFO = value;
      pwLFOstr = value;  // for display
      updatepwLFO();
      break;

    case CCfmDepth:
      fmDepth = value;
      fmDepthstr = value;
      updatefmDepth();
      break;

    case CCosc2PW:
      osc2PW = value;
      osc2PWstr = PULSEWIDTH[value];
      updateosc2PW();
      break;

    case CCosc2PWM:
      osc2PWM = value;
      osc2PWMstr = value;
      updateosc2PWM();
      break;

    case CCosc1PW:
      osc1PW = value;
      osc1PWstr = PULSEWIDTH[value];
      updateosc1PW();
      break;

    case CCosc1PWM:
      osc1PWM = value;
      osc1PWMstr = value;
      updateosc1PWM();
      break;

    case CCosc1Range:
      osc1Range = value;
      updateosc1Range();
      break;

    case CCosc2Range:
      osc2Range = value;
      updateosc2Range();
      break;

    case CCstack:
      stack = value;
      updatestack();
      break;

    case CCglideTime:
      glideTimestr = LINEAR[value];
      glideTime = value;
      updateglideTime();
      break;

    case CCosc2Detune:
      osc2Detunestr = PULSEWIDTH[value];
      osc2Detune = value;
      updateosc2Detune();
      break;

    case CCnoiseLevel:
      noiseLevel = value;
      noiseLevelstr = LINEARCENTREZERO[value];
      updatenoiseLevel();
      break;

    case CCosc2SawLevel:
      osc2SawLevel = value;
      osc2SawLevelstr = value;  // for display
      updateOsc2SawLevel();
      break;

    case CCosc1SawLevel:
      osc1SawLevel = value;
      osc1SawLevelstr = value;  // for display
      updateOsc1SawLevel();
      break;

    case CCosc2PulseLevel:
      osc2PulseLevel = value;
      osc2PulseLevelstr = value;  // for display
      updateosc2PulseLevel();
      break;

    case CCosc1PulseLevel:
      osc1PulseLevel = value;
      osc1PulseLevelstr = value;  // for display
      updateOsc1PulseLevel();
      break;

    case CCosc2TriangleLevel:
      osc2TriangleLevel = value;
      osc2TriangleLevelstr = value;  // for display
      updateosc2TriangleLevel();
      break;

    case CCosc1TriangleLevel:
      osc1TriangleLevel = value;
      osc1TriangleLevelstr = value;  // for display
      updateOsc1TriangleLevel();
      break;

    case CCLFODelay:
      LFODelay = value;
      LFODelaystr = value;  // for display
      updateLFODelay();
      break;

    case CCfilterCutoff:
      filterCutoff = value;
      filterCutoffstr = FILTERCUTOFF[value];
      updateFilterCutoff();
      break;

    case CCfilterLFO:
      filterLFO = value;
      filterLFOstr = value;
      updatefilterLFO();
      break;

    case CCfilterRes:
      filterRes = value;
      filterResstr = int(value);
      updatefilterRes();
      break;

    case CCfilterType:
      filterType = value;
      updateFilterType();
      break;

    case CCfilterEGlevel:
      filterEGlevel = value;
      filterEGlevelstr = int(value);
      updatefilterEGlevel();
      break;

    case CCLFORate:
      LFORatestr = LFOTEMPO[value];  // for display
      LFORate = value;
      updateLFORate();
      break;

    case CCLFOWaveform:
      LFOWaveform = value;
      updateStratusLFOWaveform();
      break;

    case CCfilterAttack:
      filterAttack = value;
      filterAttackstr = ENVTIMES[value];
      updatefilterAttack();
      break;

    case CCfilterDecay:
      filterDecay = value;
      filterDecaystr = ENVTIMES[value];
      updatefilterDecay();
      break;

    case CCfilterSustain:
      filterSustain = value;
      filterSustainstr = LINEAR_FILTERMIXERSTR[value];
      updatefilterSustain();
      break;

    case CCfilterRelease:
      filterRelease = value;
      filterReleasestr = ENVTIMES[value];
      updatefilterRelease();
      break;

    case CCampAttack:
      ampAttack = value;
      oldampAttack = value;
      ampAttackstr = ENVTIMES[value];
      updateampAttack();
      break;

    case CCampDecay:
      ampDecay = value;
      oldampDecay = value;
      ampDecaystr = ENVTIMES[value];
      updateampDecay();
      break;

    case CCampSustain:
      ampSustain = value;
      oldampSustain = value;
      ampSustainstr = LINEAR_FILTERMIXERSTR[value];
      updateampSustain();
      break;

    case CCampRelease:
      ampRelease = value;
      oldampRelease = value;
      ampReleasestr = ENVTIMES[value];
      updateampRelease();
      break;

    case CCvolumeControl:
      volumeControl = value;
      volumeControlstr = value;
      updatevolumeControl();
      break;

    case CCkeyTrack:
      keytrack = value;
      keytrackstr = value;
      updatekeytrack();
      break;

      ////////////////////////////////////////////////

    case CCglideSW:
      value > 0 ? glideSW = 1 : glideSW = 0;
      updateglideSW();
      break;

    case CCfilterPoleSW:
      value > 0 ? filterPoleSW = 1 : filterPoleSW = 0;
      updatefilterPoleSwitch();
      break;

    case CCfilterVel:
      value > 0 ? filterVel = 1 : filterVel = 0;
      updatefilterVel();
      break;

    case CCfilterEGinv:
      value > 0 ? filterEGinv = 1 : filterEGinv = 0;
      updatefilterEGinv();
      break;

    case CCfilterLoop:
      value > 0 ? filterLoop = 1 : filterLoop = 0;
      updatefilterLoop();
      break;

    case CCvcaLoop:
      value > 0 ? vcaLoop = 1 : vcaLoop = 0;
      updatevcaLoop();
      break;

    case CCvcaVel:
      value > 0 ? vcaVel = 1 : vcaVel = 0;
      updatevcaVel();
      break;

    case CCvcaGate:
      value > 0 ? vcaGate = 1 : vcaGate = 0;
      updatevcaGate();
      break;

    case CCchorus1:
      value > 0 ? chorus1 = 1 : chorus1 = 0;
      updatechorus1();
      break;

    case CCchorus2:
      value > 0 ? chorus2 = 1 : chorus2 = 0;
      updatechorus2();
      break;

    case CCmonoMulti:
      value > 0 ? monoMulti = 1 : monoMulti = 0;
      updateMonoMulti();
      break;

    case CCPBDepth:
      PitchBendLevel = value;
      PitchBendLevelstr = PITCHBEND[value];  // for display
      updatePitchBend();
      break;

    case CCPitchBend:
      PitchBendLevel = value;
      PitchBendLevelstr = PITCHBEND[value];  // for display
      updatePitchBend();
      break;

    case CCMWDepth:
      PitchBendLevel = value;
      PitchBendLevelstr = PITCHBEND[value];  // for display
      //updatePitchBend();
      break;

    case CClfoAlt:
      lfoAlt = value;
      updatelfoAlt();
      break;

    case CCbalance:
      balance = value;
      balancestr = value;
      updatebalance();
      break;

    case CCupperSW:
      upperSW = value;
      break;

    case CCmodwheel:
      switch (modWheelDepth) {
      //   case 1:
      //     modWheelLevel = ((value) / 5);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 2:
      //     modWheelLevel = ((value) / 4);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 3:
      //     modWheelLevel = ((value) / 3.5);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 4:
      //     modWheelLevel = ((value) / 3);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 5:
      //     modWheelLevel = ((value) / 2.5);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 6:
      //     modWheelLevel = ((value) / 2);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 7:
      //     modWheelLevel = ((value) / 1.75);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 8:
      //     modWheelLevel = ((value) / 1.5);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 9:
      //     modWheelLevel = ((value) / 1.25);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;

      //   case 10:
      //     modWheelLevel = (value);
      //     fmDepth = (int(modWheelLevel));
      //     //          Serial.print("ModWheel Depth ");
      //     //          Serial.println(modWheelLevel);
      //     break;
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
  Serial.print("MIDI Pgm Change:");
  Serial.println(patchNo);
  state = PARAMETER;
}

void myAfterTouch(byte channel, byte value) {
  afterTouch = (value * 8);
  switch (AfterTouchDest) {
    case 1:
      fmDepth = (int(afterTouch));
      break;
    case 2:
      filterCutoff = (filterCutoff + (int(afterTouch)));
      if (int(afterTouch) <= 8) {
        filterCutoff = oldfilterCutoff;
      }
      break;
    case 3:
      filterLFO = (int(afterTouch));
      break;
  }
}

void recallPatch(int patchNo) {
  allNotesOff();
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];  //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
    storeLastPatch(patchNo);
  }
}

void setCurrentPatchData(String data[]) {
  patchName = data[0];
  pwLFO = data[1].toFloat();
  fmDepth = data[2].toFloat();
  osc2PW = data[3].toFloat();
  osc2PWM = data[4].toFloat();
  osc1PW = data[5].toFloat();
  osc1PWM = data[6].toFloat();
  osc1Range = data[7].toFloat();
  osc2Range = data[8].toFloat();
  stack = data[9].toFloat();
  glideTime = data[10].toFloat();
  osc2Detune = data[11].toFloat();
  noiseLevel = data[12].toFloat();
  osc2SawLevel = data[13].toFloat();
  osc1SawLevel = data[14].toFloat();
  osc2PulseLevel = data[15].toFloat();
  osc1PulseLevel = data[16].toFloat();
  filterCutoff = data[17].toFloat();
  filterLFO = data[18].toFloat();
  filterRes = data[19].toFloat();
  filterType = data[20].toFloat();
  filterA = data[21].toFloat();
  filterB = data[22].toFloat();
  filterC = data[23].toFloat();
  filterEGlevel = data[24].toFloat();
  LFORate = data[25].toFloat();
  LFOWaveform = data[26].toFloat();
  filterAttack = data[27].toFloat();
  filterDecay = data[28].toFloat();
  filterSustain = data[29].toFloat();
  filterRelease = data[30].toFloat();
  ampAttack = data[31].toFloat();
  ampDecay = data[32].toFloat();
  ampSustain = data[33].toFloat();
  ampRelease = data[34].toFloat();
  volumeControl = data[35].toFloat();
  glideSW = data[36].toInt();
  keytrack = data[37].toFloat();
  filterPoleSW = data[38].toInt();
  filterLoop = data[39].toInt();
  filterEGinv = data[40].toInt();
  filterVel = data[41].toInt();
  vcaLoop = data[42].toInt();
  vcaVel = data[43].toInt();
  vcaGate = data[44].toInt();
  lfoAlt = data[45].toInt();
  chorus1 = data[46].toInt();
  chorus2 = data[47].toInt();
  monoMulti = data[48].toInt();
  modWheelLevel = data[49].toFloat();
  PitchBendLevel = data[50].toFloat();
  linLog = data[51].toInt();
  oct1A = data[52].toFloat();
  oct1B = data[53].toFloat();
  oct2A = data[54].toFloat();
  oct2A = data[55].toFloat();
  oldampAttack = data[56].toFloat();
  oldampDecay = data[57].toFloat();
  oldampSustain = data[58].toFloat();
  oldampRelease = data[59].toFloat();
  AfterTouchDest = data[60].toInt();
  filterLogLin = data[61].toInt();
  ampLogLin = data[62].toInt();
  oldfilterCutoff = filterCutoff;
  //Switches

  updatefilterPoleSwitch();
  updatefilterLoop();
  updatefilterEGinv();
  updatefilterVel();
  updatevcaLoop();
  updatevcaVel();
  updatevcaGate();
  updatelfoAlt();
  updatechorus1();
  updatechorus2();
  updateosc1Range();
  updateosc2Range();
  updateFilterType();
  updateMonoMulti();
  updateFilterEnv();
  updateAmpEnv();
  updateglideSW();


  //Patchname
  updatePatchname();

  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData() {
  return patchName + "," + String(pwLFO) + "," + String(fmDepth) + "," + String(osc2PW) + "," + String(osc2PWM) + "," + String(osc1PW) + "," + String(osc1PWM) + "," + String(osc1Range) + "," + String(osc2Range) + "," + String(stack) + "," + String(glideTime) + "," + String(osc2Detune) + "," + String(noiseLevel) + "," + String(osc2SawLevel) + "," + String(osc1SawLevel) + "," + String(osc2PulseLevel) + "," + String(osc1PulseLevel) + "," + String(filterCutoff) + "," + String(filterLFO) + "," + String(filterRes) + "," + String(filterType) + "," + String(filterA) + "," + String(filterB) + "," + String(filterC) + "," + String(filterEGlevel) + "," + String(LFORate) + "," + String(LFOWaveform) + "," + String(filterAttack) + "," + String(filterDecay) + "," + String(filterSustain) + "," + String(filterRelease) + "," + String(ampAttack) + "," + String(ampDecay) + "," + String(ampSustain) + "," + String(ampRelease) + "," + String(volumeControl) + "," + String(glideSW) + "," + String(keytrack) + "," + String(filterPoleSW) + "," + String(filterLoop) + "," + String(filterEGinv) + "," + String(filterVel) + "," + String(vcaLoop) + "," + String(vcaVel) + "," + String(vcaGate) + "," + String(lfoAlt) + "," + String(chorus1) + "," + String(chorus2) + "," + String(monoMulti) + "," + String(modWheelLevel) + "," + String(PitchBendLevel) + "," + String(linLog) + "," + String(oct1A) + "," + String(oct1B) + "," + String(oct2A) + "," + String(oct2B) + "," + String(oldampAttack) + "," + String(oldampDecay) + "," + String(oldampSustain) + "," + String(oldampRelease) + "," + String(AfterTouchDest) + "," + String(filterLogLin) + "," + String(ampLogLin);
}

void checkMux() {

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);


  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    mux1Read = (mux1Read >> 1); //Change range to 0-127
    switch (muxInput) {
      case MUX1_osc1PW:
        midiCCOut(CCosc1PW, mux1Read);
        myControlChange(midiChannel, CCosc1PW, mux1Read);
        break;
      case MUX1_MWDepth:
        midiCCOut(CCMWDepth, mux1Read);
        myControlChange(midiChannel, CCMWDepth, mux1Read);
        break;
      case MUX1_osc2PWM:
        midiCCOut(CCosc2PWM, mux1Read);
        myControlChange(midiChannel, CCosc2PWM, mux1Read);
        break;
      case MUX1_osc2PW:
        midiCCOut(CCosc2PW, mux1Read);
        myControlChange(midiChannel, CCosc2PW, mux1Read);
        break;
      case MUX1_PBDepth:
        midiCCOut(CCPBDepth, mux1Read);
        myControlChange(midiChannel, CCPBDepth, mux1Read);
        break;
      case MUX1_osc1PWM:
        midiCCOut(CCosc1PWM, mux1Read);
        myControlChange(midiChannel, CCosc1PWM, mux1Read);
        break;
      case MUX1_pwLFO:
        midiCCOut(CCpwLFO, mux1Read);
        myControlChange(midiChannel, CCpwLFO, mux1Read);
        break;
      case MUX1_fmDepth:
        midiCCOut(CCfmDepth, mux1Read);
        myControlChange(midiChannel, CCfmDepth, mux1Read);
        break;
      case MUX1_osc2SawLevel:
        midiCCOut(CCosc2SawLevel, mux1Read);
        myControlChange(midiChannel, CCosc2SawLevel, mux1Read);
        break;
      case MUX1_noiseLevel:
        midiCCOut(CCnoiseLevel, mux1Read);
        myControlChange(midiChannel, CCnoiseLevel, mux1Read);
        break;
      case MUX1_osc1SawLevel:
        midiCCOut(CCosc1SawLevel, mux1Read);
        myControlChange(midiChannel, CCosc1SawLevel, mux1Read);
        break;
      case MUX1_osc2Detune:
        midiCCOut(CCosc2Detune, mux1Read);
        myControlChange(midiChannel, CCosc2Detune, mux1Read);
        break;
      case MUX1_osc1Range:
        midiCCOut(CCosc1Range, mux1Read);
        myControlChange(midiChannel, CCosc1Range, mux1Read);
        break;
      case MUX1_glideTime:
        midiCCOut(CCglideTime, mux1Read);
        myControlChange(midiChannel, CCglideTime, mux1Read);
        break;
      case MUX1_osc2Range:
        midiCCOut(CCosc2Range, mux1Read);
        myControlChange(midiChannel, CCosc2Range, mux1Read);
        break;
      case MUX1_stack:
        midiCCOut(CCstack, mux1Read);
        myControlChange(midiChannel, CCstack, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;
mux2Read = (mux2Read >> 1); //Change range to 0-127
    switch (muxInput) {
      case MUX2_osc2TriangleLevel:
        midiCCOut(CCosc2TriangleLevel, mux2Read);
        myControlChange(midiChannel, CCosc2TriangleLevel, mux2Read);
        break;
      case MUX2_filterLFO:
        midiCCOut(CCfilterLFO, mux2Read);
        myControlChange(midiChannel, CCfilterLFO, mux2Read);
        break;
      case MUX2_filterCutoff:
        midiCCOut(CCfilterCutoff, mux2Read);
        myControlChange(midiChannel, CCfilterCutoff, mux2Read);
        break;
      case MUX2_LFOWaveform:
        midiCCOut(CCLFOWaveform, mux2Read);
        myControlChange(midiChannel, CCLFOWaveform, mux2Read);
        break;
      case MUX2_osc1PulseLevel:
        midiCCOut(CCosc1PulseLevel, mux2Read);
        myControlChange(midiChannel, CCosc1PulseLevel, mux2Read);
        break;
      case MUX2_LFODelay:
        midiCCOut(CCLFODelay, mux2Read);
        myControlChange(midiChannel, CCLFODelay, mux2Read);
        break;
      case MUX2_osc2PulseLevel:
        midiCCOut(CCosc2PulseLevel, mux2Read);
        myControlChange(midiChannel, CCosc2PulseLevel, mux2Read);
        break;
      case MUX2_osc1TriangleLevel:
        midiCCOut(CCosc1TriangleLevel, mux2Read);
        myControlChange(midiChannel, CCosc1TriangleLevel, mux2Read);
        break;
      case MUX2_LFORate:
        midiCCOut(CCLFORate, mux2Read);
        myControlChange(midiChannel, CCLFORate, mux2Read);
        break;
      case MUX2_filterDecay:
        midiCCOut(CCfilterDecay, mux2Read);
        myControlChange(midiChannel, CCfilterDecay, mux2Read);
        break;
      case MUX2_filterAttack:
        midiCCOut(CCfilterAttack, mux2Read);
        myControlChange(midiChannel, CCfilterAttack, mux2Read);
        break;
      case MUX2_ampAttack:
        midiCCOut(CCampAttack, mux2Read);
        myControlChange(midiChannel, CCampAttack, mux2Read);
        break;
      case MUX2_filterRes:
        midiCCOut(CCfilterRes, mux2Read);
        myControlChange(midiChannel, CCfilterRes, mux2Read);
        break;
      case MUX2_filterType:
        midiCCOut(CCfilterType, mux2Read);
        myControlChange(midiChannel, CCfilterType, mux2Read);
        break;
      case MUX2_keyTrack:
        midiCCOut(CCkeyTrack, mux2Read);
        myControlChange(midiChannel, CCkeyTrack, mux2Read);
        break;
      case MUX2_filterEGlevel:
        midiCCOut(CCfilterEGlevel, mux2Read);
        myControlChange(midiChannel, CCfilterEGlevel, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;
mux3Read = (mux3Read >> 1); //Change range to 0-127
    switch (muxInput) {
      case MUX3_balance:
        midiCCOut(CCbalance, mux3Read);
        myControlChange(midiChannel, CCbalance, mux3Read);
        break;
      case MUX3_ampSustain:
        midiCCOut(CCampSustain, mux3Read);
        myControlChange(midiChannel, CCampSustain, mux3Read);
        break;
      case MUX3_ampDecay:
        midiCCOut(CCampDecay, mux3Read);
        myControlChange(midiChannel, CCampDecay, mux3Read);
        break;
      case MUX3_volumeControl:
        midiCCOut(CCvolumeControl, mux3Read);
        myControlChange(midiChannel, CCvolumeControl, mux3Read);
        break;
      case MUX3_filterSustain:
        midiCCOut(CCfilterSustain, mux3Read);
        myControlChange(midiChannel, CCfilterSustain, mux3Read);
        break;
      case MUX3_filterRelease:
        midiCCOut(CCfilterRelease, mux3Read);
        myControlChange(midiChannel, CCfilterRelease, mux3Read);
        break;
      case MUX3_ampRelease:
        midiCCOut(CCampRelease, mux3Read);
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
  //usbMIDI.sendControlChange(cc, value, midiChannel); //MIDI DIN is set to Out
  //midi1.sendControlChange(cc, value, midiChannel);
  MIDI.sendControlChange(cc, value, midiChannel);  //MIDI DIN is set to Out
}

void writeDemux() {
  delayMicroseconds(DelayForSH3);
  //analogWrite(A21, 0);

  //DEMUX 1
  digitalWriteFast(DEMUX_EN_1, LOW);
  switch (muxOutput) {
    case 0:
      //analogWrite(A21, int(fmDepth / 1.57));
      break;
    case 1:
      //analogWrite(A21, int(osc2PWM / 1.57));
      break;
    case 2:
      //analogWrite(A21, int(osc1PWM / 1.57));
      break;
    case 3:
      //analogWrite(A21, int(stack));
      break;
    case 4:
      //analogWrite(A21, int(osc2Detune));
      break;
    case 5:
      //analogWrite(A21, int(noiseLevel / 1.57));
      break;
    case 6:
      //analogWrite(A21, int(filterLFO / 1.57));
      break;
    case 7:
      //analogWrite(A21, int(volumeControl / 1.57));
      break;
    case 8:
      //analogWrite(A21, int(osc1SawLevel / 1.57));
      break;
    case 9:
      //analogWrite(A21, int(osc1PulseLevel / 1.57));
      break;
    case 10:
      //analogWrite(A21, int(osc2SawLevel / 1.57));
      break;
    case 11:
      //analogWrite(A21, int(osc2PulseLevel / 1.57));
      break;
    case 12:
      //analogWrite(A21, int(PitchBendLevel / 1.57));
      break;
    case 13:
      //analogWrite(A21, int(osc1PW));
      break;
    case 14:
      //analogWrite(A21, int(osc2PW));
      break;
    case 15:
      //analogWrite(A21, int(modWheelLevel / 1.57));
      break;
  }
  delayMicroseconds(DelayForSH3);
  digitalWriteFast(DEMUX_EN_1, HIGH);
  //analogWrite(A21, 0);
  //setVoltage(DAC_NOTE1, 0, 1, 0);

  //DEMUX 2

  digitalWriteFast(DEMUX_EN_2, LOW);
  switch (muxOutput) {
    case 0:
      //analogWrite(A21, int(filterAttack));
      break;
    case 1:
      //analogWrite(A21, int(filterDecay));
      break;
    case 2:
      //analogWrite(A21, int(filterSustain));
      break;
    case 3:
      //analogWrite(A21, int(filterRelease));
      break;
    case 4:
      //analogWrite(A21, int(ampAttack));
      break;
    case 5:
      //analogWrite(A21, int(ampDecay));
      break;
    case 6:
      //analogWrite(A21, int(ampSustain));
      break;
    case 7:
      //analogWrite(A21, int(ampRelease));
      break;
    case 8:
      //analogWrite(A21, int(pwLFO));
      break;
    case 9:
      //analogWrite(A21, int(LFORate));
      break;
    case 10:
      //analogWrite(A21, int(LFOWaveform));
      break;
    case 11:
      //analogWrite(A21, int(filterEGlevel));
      break;
    case 12:
      //analogWrite(A21, int(filterCutoff));
      break;
    case 13:
      //analogWrite(A21, int(filterRes / 2.5));
      break;
    case 14:
      //analogWrite(A21, int(filterCutoff));
      break;
    case 15:
      //analogWrite(A21, int(filterRes / 2.5));
      break;
  }
  delayMicroseconds(DelayForSH3);
  digitalWriteFast(DEMUX_EN_2, HIGH);
  //analogWrite(A21, 0);
  //setVoltage(DAC_NOTE1, 0, 1, 0);

  //DEMUX 3

  digitalWriteFast(DEMUX_EN_3, LOW);
  switch (muxOutput) {
    case 0:
      //analogWrite(A21, int(filterA));
      break;
    case 1:
      //analogWrite(A21, int(filterB));
      break;
    case 2:
      //analogWrite(A21, int(filterC));
      break;
    case 3:
      //analogWrite(A21, 0);
      break;
    case 4:
      //analogWrite(A21, int(oct1A));
      break;
    case 5:
      //analogWrite(A21, int(oct1B));
      break;
    case 6:
      //analogWrite(A21, int(oct2A));
      break;
    case 7:
      //analogWrite(A21, int(oct2B));
      break;
    case 8:
      //analogWrite(A21, 0);
      break;
    case 9:
      //analogWrite(A21, 0);
      break;
    case 10:
      //analogWrite(A21, 0);
      break;
    case 11:
      //analogWrite(A21, 0);
      break;
    case 12:
      //analogWrite(A21, 0);
      break;
    case 13:
      //analogWrite(A21, 0);
      break;
    case 14:
      //analogWrite(A21, 0);
      break;
    case 15:
      //analogWrite(A21, 0);
      break;
  }
  delayMicroseconds(DelayForSH3);
  digitalWriteFast(DEMUX_EN_3, HIGH);
  //analogWrite(A21, 0);

  muxOutput++;
  if (muxOutput >= DEMUXCHANNELS)
    muxOutput = 0;

  digitalWriteFast(DEMUX_0, muxOutput & B0001);
  digitalWriteFast(DEMUX_1, muxOutput & B0010);
  digitalWriteFast(DEMUX_2, muxOutput & B0100);
  digitalWriteFast(DEMUX_3, muxOutput & B1000);
}

void updateDAC() {
  setVoltage(DAC_NOTE1, 0, 1, int(osc1PW * 2));
  setVoltage(DAC_NOTE1, 1, 1, int(osc2PW * 2));
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
    vcaLoop = !vcaLoop;
    myControlChange(midiChannel, CCvcaLoop, vcaLoop);
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

  if (btnIndex == FILTERLOOP_SW && btnType == ROX_PRESSED) {
    filterLoop = !filterLoop;
    myControlChange(midiChannel, CCfilterLoop, filterLoop);
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
}

void checkSwitches() {


  saveButton.update();
  if (saveButton.read() == LOW && saveButton.duration() > HOLD_DURATION) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        saveButton.write(HIGH);  //Come out of this state
        del = true;              //Hack
        break;
    }
  } else if (saveButton.risingEdge()) {
    if (!del) {
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
          patchName = patches.last().patchName;
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patches.last().patchName);
          patchNo = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNo);
          renamedPatch = "";
          state = PARAMETER;
          break;
        case PATCHNAMING:
          if (renamedPatch.length() > 0) patchName = renamedPatch;  //Prevent empty strings
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patchName);
          patchNo = patches.last().patchNo;
          loadPatches();  //Get rid of pushed patch if it wasn't saved
          setPatchesOrdering(patchNo);
          renamedPatch = "";
          state = PARAMETER;
          break;
      }
    } else {
      del = false;
    }
  }

  settingsButton.update();
  if (settingsButton.read() == LOW && settingsButton.duration() > HOLD_DURATION) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
    settingsButton.write(HIGH);              //Come out of this state
    reini = true;                            //Hack
  } else if (settingsButton.risingEdge()) {  //cannot be fallingEdge because holding button won't work
    if (!reini) {
      switch (state) {
        case PARAMETER:
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
        case SETTINGS:
          settingsOptions.push(settingsOptions.shift());
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
        case SETTINGSVALUE:
          //Same as pushing Recall - store current settings item and go back to options
          settingsHandler(settingsOptions.first().value[settingsValueIndex], settingsOptions.first().handler);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    } else {
      reini = false;
    }
  }

  backButton.update();
  if (backButton.read() == LOW && backButton.duration() > HOLD_DURATION) {
    //If Back button held, Panic - all notes off
    allNotesOff();
    backButton.write(HIGH);              //Come out of this state
    panic = true;                        //Hack
  } else if (backButton.risingEdge()) {  //cannot be fallingEdge because holding button won't work
    if (!panic) {
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
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    } else {
      panic = false;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.read() == LOW && recallButton.duration() > HOLD_DURATION) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
    recallButton.write(HIGH);  //Come out of this state
    recall = true;             //Hack
  } else if (recallButton.risingEdge()) {
    if (!recall) {
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
          //Choose this option and allow value choice
          settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGSVALUE);
          state = SETTINGSVALUE;
          break;
        case SETTINGSVALUE:
          //Store current settings item and go back to options
          settingsHandler(settingsOptions.first().value[settingsValueIndex], settingsOptions.first().handler);
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
          state = SETTINGS;
          break;
      }
    } else {
      recall = false;
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
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder() {
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
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
        settingsOptions.push(settingsOptions.shift());
        settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
        showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
        break;
      case SETTINGSVALUE:
        if (settingsOptions.first().value[settingsValueIndex + 1] != '\0')
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[++settingsValueIndex], SETTINGSVALUE);
        break;
    }
    encPrevious = encRead;
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
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
        settingsOptions.unshift(settingsOptions.pop());
        settingsValueIndex = getCurrentIndex(settingsOptions.first().currentIndex);
        showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[settingsValueIndex], SETTINGS);
        break;
      case SETTINGSVALUE:
        if (settingsValueIndex > 0)
          showSettingsPage(settingsOptions.first().option, settingsOptions.first().value[--settingsValueIndex], SETTINGSVALUE);
        break;
    }
    encPrevious = encRead;
  }
}

void loop() {
  octoswitch.update();
  checkSwitches();
  writeDemux();
  updateDAC();
  checkMux();
  checkEncoder();
  //sr.update();
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);
}