/*
  PolyKit 16 MUX - Firmware Rev 1.0

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
int DelayForSH3 = 50;
int midioutfrig = 8;
int patchNo = 0;
int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now
unsigned long buttonDebounce = 0;

ShiftRegister74HC595<2> sr(23, 22, 21);
ShiftRegister74HC595<6> srp(52, 53, 54);

#define OCTO_TOTAL 2
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;

// pins for 74HC165
#define PIN_DATA 50  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 49  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 51   // pin 2 on 74HC165 (CLK))

void setup() {
  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);
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
}

// void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV) {
//   int command = channel ? 0x9000 : 0x1000;

//   command |= gain ? 0x0000 : 0x2000;
//   command |= (mV & 0x0FFF);

//   SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
//   digitalWrite(dacpin, LOW);
//   SPI.transfer(command >> 8);
//   SPI.transfer(command & 0xFF);
//   digitalWrite(dacpin, HIGH);
//   SPI.endTransaction();
// }

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
    srp.set(OCT1A, LOW);
    srp.set(OCT1B, HIGH);
    ;
  } else if (osc1Range < 100 && osc1Range > 33) {
    showCurrentParameterPage("Osc1 Range", String("16"));
    srp.set(OCT1A, HIGH);
    srp.set(OCT1B, HIGH);
  } else {
    showCurrentParameterPage("Osc1 Range", String("32"));
    srp.set(OCT1A, HIGH);
    srp.set(OCT1B, LOW);
  }
}

void updateosc2Range() {
  if (osc2Range > 100) {
    showCurrentParameterPage("Osc2 Range", String("8"));
    srp.set(OCT2A, LOW);
    srp.set(OCT2B, HIGH);
  } else if (osc2Range < 100 && osc2Range > 33) {
    showCurrentParameterPage("Osc2 Range", String("16"));
    srp.set(OCT2A, HIGH);
    srp.set(OCT2B, HIGH);
  } else {
    showCurrentParameterPage("Osc2 Range", String("32"));
    srp.set(OCT2A, HIGH);
    srp.set(OCT2B, LOW);
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

void updateOsc1SubLevel() {
  showCurrentParameterPage("OSC1 Sub", int(osc1SubLevelstr));
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
    srp.set(FILTERA, LOW);
    srp.set(FILTERB, LOW);
    srp.set(FILTERC, LOW);

  } else if (filterType > 12 && filterType < 22) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("1P LowPass"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P LowPass"));
    }
    srp.set(FILTERA, HIGH);
    srp.set(FILTERB, LOW);
    srp.set(FILTERC, LOW);

  } else if (filterType > 22 && filterType < 44) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("3P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("4P HighPass"));
    }
    srp.set(FILTERA, LOW);
    srp.set(FILTERB, HIGH);
    srp.set(FILTERC, LOW);

  } else if (filterType > 44 && filterType < 66) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("1P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P HighPass"));
    }
    srp.set(FILTERA, HIGH);
    srp.set(FILTERB, HIGH);
    srp.set(FILTERC, LOW);

  } else if (filterType > 66 && filterType < 88) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P HP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("4P BandPass"));
    }
    srp.set(FILTERA, LOW);
    srp.set(FILTERB, LOW);
    srp.set(FILTERC, HIGH);

  } else if (filterType > 88 && filterType < 105) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P BP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("2P BandPass"));
    }
    srp.set(FILTERA, HIGH);
    srp.set(FILTERB, LOW);
    srp.set(FILTERC, HIGH);

  } else if (filterType > 105 && filterType < 120) {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("3P AP + 1P LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("3P AllPass"));
    }
    srp.set(FILTERA, LOW);
    srp.set(FILTERB, HIGH);
    srp.set(FILTERC, HIGH);

  } else {
    if (filterPoleSW == 1) {
      showCurrentParameterPage("Filter Type", String("2P Notch + LP"));
    } else {
      showCurrentParameterPage("Filter Type", String("Notch"));
    }
    srp.set(FILTERA, HIGH);
    srp.set(FILTERB, HIGH);
    srp.set(FILTERC, HIGH);
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
  if (lfoAlt == 1) {
    if (value >= 0 && value < 127) {
      StratusLFOWaveform = "Saw +Oct";
    } else if (value >= 128 && value < 255) {
      StratusLFOWaveform = "Quad Saw";
    } else if (value >= 256 && value < 383) {
      StratusLFOWaveform = "Quad Pulse";
    } else if (value >= 384 && value < 511) {
      StratusLFOWaveform = "Tri Step";
    } else if (value >= 512 && value < 639) {
      StratusLFOWaveform = "Sine +Oct";
    } else if (value >= 640 && value < 767) {
      StratusLFOWaveform = "Sine +3rd";
    } else if (value >= 768 && value < 895) {
      StratusLFOWaveform = "Sine +4th";
    } else if (value >= 896 && value < 1024) {
      StratusLFOWaveform = "Rand Slopes";
    }
  } else {
    if (value >= 0 && value < 127) {
      StratusLFOWaveform = "Sawtooth Up";
    } else if (value >= 128 && value < 255) {
      StratusLFOWaveform = "Sawtooth Down";
    } else if (value >= 256 && value < 383) {
      StratusLFOWaveform = "Squarewave";
    } else if (value >= 384 && value < 511) {
      StratusLFOWaveform = "Triangle";
    } else if (value >= 512 && value < 639) {
      StratusLFOWaveform = "Sinewave";
    } else if (value >= 640 && value < 767) {
      StratusLFOWaveform = "Sweeps";
    } else if (value >= 768 && value < 895) {
      StratusLFOWaveform = "Lumps";
    } else if (value >= 896 && value < 1024) {
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
    srp.set(FILTER_MODE_BIT0, HIGH);
    srp.set(FILTER_MODE_BIT1, HIGH);

    midiCCOut(CCfilterLoop, 127);
  } else {
    showCurrentParameterPage("VCF EG Loop", "Off");
    sr.set(FILTERLOOP_LED, LOW);  // LED off
    srp.set(FILTER_MODE_BIT0, LOW);
    srp.set(FILTER_MODE_BIT1, LOW);
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
    srp.set(AMP_MODE_BIT0, HIGH);
    srp.set(AMP_MODE_BIT1, HIGH);
    midiCCOut(CCvcaLoop, 127);
  } else {
    showCurrentParameterPage("VCA EG Loop", "Off");
    sr.set(VCALOOP_LED, LOW);  // LED off
    srp.set(AMP_MODE_BIT0, LOW);
    srp.set(AMP_MODE_BIT1, LOW);
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
    srp.set(UPPER_CHORUS1_OUT, LOW);
    midiCCOut(CCchorus1, 1);
  } else {
    showCurrentParameterPage("Chorus 1", String("On"));
    sr.set(CHORUS1_LED, HIGH);  // LED on
    srp.set(CHORUS1_OUT, HIGH);
    srp.set(UPPER_CHORUS1_OUT, HIGH);
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
      pwLFOstr = value / midioutfrig;  // for display
      updatepwLFO();
      break;

    case CCfmDepth:
      fmDepth = value;
      fmDepthstr = value / midioutfrig;
      updatefmDepth();
      break;

    case CCosc2PW:
      osc2PW = value;
      osc2PWstr = PULSEWIDTH[value / midioutfrig];
      updateosc2PW();
      break;

    case CCosc2PWM:
      osc2PWM = value;
      osc2PWMstr = value / midioutfrig;
      updateosc2PWM();
      break;

    case CCosc1PW:
      osc1PW = value;
      osc1PWstr = PULSEWIDTH[value / midioutfrig];
      updateosc1PW();
      break;

    case CCosc1PWM:
      osc1PWM = value;
      osc1PWMstr = value / midioutfrig;
      updateosc1PWM();
      break;

    case CCosc1Range:
      osc1Range = value / midioutfrig;
      updateosc1Range();
      break;

    case CCosc2Range:
      osc2Range = value / midioutfrig;
      updateosc2Range();
      break;

    case CCstack:
      stack = value;
      updatestack();
      break;

    case CCglideTime:
      glideTimestr = LINEAR[value / midioutfrig];
      glideTime = value;
      updateglideTime();
      break;

    case CCosc2Detune:
      osc2Detunestr = PULSEWIDTH[value / midioutfrig];
      osc2Detune = value;
      updateosc2Detune();
      break;

    case CCnoiseLevel:
      noiseLevel = value;
      noiseLevelstr = LINEARCENTREZERO[value / midioutfrig];
      updatenoiseLevel();
      break;

    case CCosc2SawLevel:
      osc2SawLevel = value;
      osc2SawLevelstr = value / midioutfrig;  // for display
      updateOsc2SawLevel();
      break;

    case CCosc1SawLevel:
      osc1SawLevel = value;
      osc1SawLevelstr = value / midioutfrig;  // for display
      updateOsc1SawLevel();
      break;

    case CCosc2PulseLevel:
      osc2PulseLevel = value;
      osc2PulseLevelstr = value / midioutfrig;  // for display
      updateosc2PulseLevel();
      break;

    case CCosc1PulseLevel:
      osc1PulseLevel = value;
      osc1PulseLevelstr = value / midioutfrig;  // for display
      updateOsc1PulseLevel();
      break;

    case CCosc2TriangleLevel:
      osc2TriangleLevel = value;
      osc2TriangleLevelstr = value / midioutfrig;  // for display
      updateosc2TriangleLevel();
      break;

    case CCosc1SubLevel:
      osc1SubLevel = value;
      osc1SubLevelstr = value / midioutfrig;  // for display
      updateOsc1SubLevel();
      break;

    case CCLFODelay:
      LFODelay = value;
      LFODelaystr = value / midioutfrig;  // for display
      updateLFODelay();
      break;

    case CCfilterCutoff:
      filterCutoff = value;
      filterCutoffstr = FILTERCUTOFF[value / midioutfrig];
      updateFilterCutoff();
      break;

    case CCfilterLFO:
      filterLFO = value;
      filterLFOstr = value / midioutfrig;
      updatefilterLFO();
      break;

    case CCfilterRes:
      filterRes = value;
      filterResstr = int(value / midioutfrig);
      updatefilterRes();
      break;

    case CCfilterType:
      filterType = value / midioutfrig;
      updateFilterType();
      break;

    case CCfilterEGlevel:
      filterEGlevel = value;
      filterEGlevelstr = int(value / midioutfrig);
      updatefilterEGlevel();
      break;

    case CCLFORate:
      LFORatestr = LFOTEMPO[value / midioutfrig];  // for display
      LFORate = value;
      updateLFORate();
      break;

    case CCLFOWaveform:
      LFOWaveform = value;
      updateStratusLFOWaveform();
      break;

    case CCfilterAttack:
      filterAttack = value;
      filterAttackstr = ENVTIMES[value / midioutfrig];
      updatefilterAttack();
      break;

    case CCfilterDecay:
      filterDecay = value;
      filterDecaystr = ENVTIMES[value / midioutfrig];
      updatefilterDecay();
      break;

    case CCfilterSustain:
      filterSustain = value;
      filterSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updatefilterSustain();
      break;

    case CCfilterRelease:
      filterRelease = value;
      filterReleasestr = ENVTIMES[value / midioutfrig];
      updatefilterRelease();
      break;

    case CCampAttack:
      ampAttack = value;
      oldampAttack = value;
      ampAttackstr = ENVTIMES[value / midioutfrig];
      updateampAttack();
      break;

    case CCampDecay:
      ampDecay = value;
      oldampDecay = value;
      ampDecaystr = ENVTIMES[value / midioutfrig];
      updateampDecay();
      break;

    case CCampSustain:
      ampSustain = value;
      oldampSustain = value;
      ampSustainstr = LINEAR_FILTERMIXERSTR[value / midioutfrig];
      updateampSustain();
      break;

    case CCampRelease:
      ampRelease = value;
      oldampRelease = value;
      ampReleasestr = ENVTIMES[value / midioutfrig];
      updateampRelease();
      break;

    case CCvolumeControl:
      volumeControl = value;
      volumeControlstr = value / midioutfrig;
      updatevolumeControl();
      break;

    case CCkeyTrack:
      keytrack = value;
      keytrackstr = value / midioutfrig;
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
      PitchBendLevelstr = PITCHBEND[value / midioutfrig];  // for display
      updatePitchBend();
      break;

    case CCPitchBend:
      PitchBendLevel = value;
      PitchBendLevelstr = PITCHBEND[value / midioutfrig];  // for display
      updatePitchBend();
      break;

    case CCMWDepth:
      PitchBendLevel = value;
      PitchBendLevelstr = PITCHBEND[value / midioutfrig];  // for display
      //updatePitchBend();
      break;

    case CClfoAlt:
      lfoAlt = value;
      updatelfoAlt();
      break;

    case CCbalance:
      balance = value;
      balancestr = value / midioutfrig;
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
  osc2TriangleLevel = data[63].toFloat();
  osc1SubLevel = data[64].toFloat();

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
  return patchName + "," + String(pwLFO) + "," + String(fmDepth) + "," + String(osc2PW) + "," + String(osc2PWM) + "," + String(osc1PW) + "," + String(osc1PWM) + "," + String(osc1Range) + "," + 
  String(osc2Range) + "," + String(stack) + "," + String(glideTime) + "," + String(osc2Detune) + "," + String(noiseLevel) + "," + String(osc2SawLevel) + "," + String(osc1SawLevel) + "," + 
  String(osc2PulseLevel) + "," + String(osc1PulseLevel) + "," + String(filterCutoff) + "," + String(filterLFO) + "," + String(filterRes) + "," + String(filterType) + "," + String(filterA) + "," + 
  String(filterB) + "," + String(filterC) + "," + String(filterEGlevel) + "," + String(LFORate) + "," + String(LFOWaveform) + "," + String(filterAttack) + "," + String(filterDecay) + "," + 
  String(filterSustain) + "," + String(filterRelease) + "," + String(ampAttack) + "," + String(ampDecay) + "," + String(ampSustain) + "," + String(ampRelease) + "," + 
  String(volumeControl) + "," + String(glideSW) + "," + String(keytrack) + "," + String(filterPoleSW) + "," + String(filterLoop) + "," + String(filterEGinv) + "," + 
  String(filterVel) + "," + String(vcaLoop) + "," + String(vcaVel) + "," + String(vcaGate) + "," + String(lfoAlt) + "," + String(chorus1) + "," + String(chorus2) + "," + 
  String(monoMulti) + "," + String(modWheelLevel) + "," + String(PitchBendLevel) + "," + String(linLog) + "," + String(oct1A) + "," + String(oct1B) + "," + 
  String(oct2A) + "," + String(oct2B) + "," + String(oldampAttack) + "," + String(oldampDecay) + "," + String(oldampSustain) + "," + String(oldampRelease) + "," + 
  String(AfterTouchDest) + "," + String(filterLogLin) + "," + String(ampLogLin) + "," + String(osc2TriangleLevel) + "," + String(osc1SubLevel);
}

void checkMux() {

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);


  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    //    mux1Read = (mux1Read >> 3); //Change range to 0-127
    switch (muxInput) {
      case MUX1_osc1PW:
        midiCCOut(CCosc1PW, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc1PW, mux1Read);
        break;
      case MUX1_MWDepth:
        midiCCOut(CCMWDepth, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCMWDepth, mux1Read);
        break;
      case MUX1_osc2PWM:
        midiCCOut(CCosc2PWM, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2PWM, mux1Read);
        break;
      case MUX1_osc2PW:
        midiCCOut(CCosc2PW, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCosc2PW, mux1Read);
        break;
      case MUX1_PBDepth:
        midiCCOut(CCPBDepth, mux1Read / midioutfrig);
        myControlChange(midiChannel, CCPBDepth, mux1Read);
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
    //mux2Read = (mux2Read >> 1); //Change range to 0-127
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
    //mux3Read = (mux3Read >> 1); //Change range to 0-127
    switch (muxInput) {
      case MUX3_balance:
        midiCCOut(CCbalance, mux3Read / midioutfrig);
        myControlChange(midiChannel, CCbalance, mux3Read);
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
  //usbMIDI.sendControlChange(cc, value, midiChannel); //MIDI DIN is set to Out
  //midi1.sendControlChange(cc, value, midiChannel);
  MIDI.sendControlChange(cc, value, midiChannel);  //MIDI DIN is set to Out
}

void MCP4922_write(const int &slavePin, const int &value1, const int &value2) {
  int value = 0;
  byte configByte = 0;
  byte data = 0;
  int channel = 0;

  for (channel = 0; channel < 2; channel++) {
    digitalWrite(slavePin, LOW);  //set DAC ready to accept commands
    if (channel == 0) {
      configByte = B01110000;  //channel 0, Vref buffered, Gain of 1x, Active Mode
      value = value1;
    } else {
      configByte = B11110000;  //channel 1, Vref buffered, Gain of 1x, Active Mode
      value = value2;
    }

    //write first byte
    data = highByte(value);
    data = B00001111 & data;   //clear out the 4 command bits
    data = configByte | data;  //set the first four command bits
    SPI.transfer(data);

    //write second byte
    data = lowByte(value);
    SPI.transfer(data);

    //close the transfer
    digitalWrite(slavePin, HIGH);  //set DAC ready to accept commands
  }
}

void writeDemux() {
      //DEMUX 1
      digitalWriteFast(DEMUX_EN_1, LOW);

      switch (muxOutput) {
        case 0:
          MCP4922_write(DAC_CS1, int(fmDepth * DACMULT), int(fmDepth * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 1:
        MCP4922_write(DAC_CS1, int(osc2PWM * DACMULT), int(osc2PWM * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 2:
          MCP4922_write(DAC_CS1, int(osc1PWM * DACMULT), int(osc1PWM * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 3:
          MCP4922_write(DAC_CS1, int(stack * DACMULT), int(stack * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 4:
          MCP4922_write(DAC_CS1, int(osc2Detune * DACMULT), int(osc2Detune * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 5:
          MCP4922_write(DAC_CS1, int(noiseLevel * DACMULT), int(noiseLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 6:
          MCP4922_write(DAC_CS1, int(filterLFO * DACMULT), int(filterLFO * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 7:
          MCP4922_write(DAC_CS1, int(volumeControl * DACMULT), int(volumeControl * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 8:
          MCP4922_write(DAC_CS1, int(osc1SawLevel * DACMULT), int(osc1SawLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 9:
          MCP4922_write(DAC_CS1, int(osc1PulseLevel * DACMULT), int(osc1PulseLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 10:
          MCP4922_write(DAC_CS1, int(osc2SawLevel * DACMULT), int(osc2SawLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 11:
          MCP4922_write(DAC_CS1, int(osc2PulseLevel * DACMULT), int(osc2PulseLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 12:
          MCP4922_write(DAC_CS1, int(keytrack * DACMULT), int(keytrack * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 13:
          MCP4922_write(DAC_CS1, int(osc1PW * DACMULT), int(osc1PW * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 14:
          MCP4922_write(DAC_CS1, int(osc2PW * DACMULT), int(osc2PW * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 15:
          MCP4922_write(DAC_CS1, int(balance * DACMULT), int(balance * DACMULT));  // use for balance
          delayMicroseconds(DelayForSH3);
          break;
      }
      digitalWriteFast(DEMUX_EN_1, HIGH);

      digitalWriteFast(DEMUX_EN_2, LOW);
      switch (muxOutput) {

        case 0:
          MCP4922_write(DAC_CS2, int(filterAttack * DACMULT), int(filterAttack * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 1:
          MCP4922_write(DAC_CS2, int(filterDecay * DACMULT), int(filterDecay * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 2:
          MCP4922_write(DAC_CS2, int(filterSustain * DACMULT), int(filterSustain * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 3:
          MCP4922_write(DAC_CS2, int(filterRelease * DACMULT), int(filterRelease * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 4:
          MCP4922_write(DAC_CS2, int(ampAttack * DACMULT), int(ampAttack * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 5:
          MCP4922_write(DAC_CS2, int(ampDecay * DACMULT), int(ampDecay * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 6:
          MCP4922_write(DAC_CS2, int(ampSustain * DACMULT), int(ampSustain * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 7:
          MCP4922_write(DAC_CS2, int(ampRelease * DACMULT), int(ampRelease * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 8:
          MCP4922_write(DAC_CS2, int(pwLFO * DACMULT), int(pwLFO * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 9:
          MCP4922_write(DAC_CS2, int(LFORate * DACMULT), int(LFORate * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 10:
          MCP4922_write(DAC_CS2, int(LFOWaveform * DACMULT), int(LFOWaveform * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 11:
          MCP4922_write(DAC_CS2, int(filterEGlevel * DACMULT), int(filterEGlevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 12:
          MCP4922_write(DAC_CS2, int(filterCutoff * DACMULT), int(filterCutoff * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 13:
          MCP4922_write(DAC_CS2, int(filterRes * DACMULT), int(filterRes * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 14:
          MCP4922_write(DAC_CS2, int(osc1SubLevel * DACMULT), int(osc1SubLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
        case 15:
          MCP4922_write(DAC_CS2, int(osc2TriangleLevel * DACMULT), int(osc2TriangleLevel * DACMULT));
          delayMicroseconds(DelayForSH3);
          break;
      }
      digitalWriteFast(DEMUX_EN_2, HIGH);

  muxOutput++;
  if (muxOutput >= DEMUXCHANNELS)

    muxOutput = 0;

  digitalWriteFast(DEMUX_0, muxOutput & B0001);
  digitalWriteFast(DEMUX_1, muxOutput & B0010);
  digitalWriteFast(DEMUX_2, muxOutput & B0100);
  digitalWriteFast(DEMUX_3, muxOutput & B1000);

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
  checkMux();
  checkEncoder();
  //sr.update();
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);
}