#include <EEPROM.h>

#define EEPROM_MIDI_CH 0

#define EEPROM_ENCODER_DIR 2
#define EEPROM_MODWHEEL_DEPTH 3
#define EEPROM_FILTERENV_U 4
#define EEPROM_FILTERENV_L 5
#define EEPROM_AMPENV_U 6
#define EEPROM_AMPENV_L 7
#define EEPROM_LAST_PATCHU 8
#define EEPROM_LAST_PATCHL 9
#define EEPROM_AFTERTOUCH_U 10
#define EEPROM_AFTERTOUCH_L 11
#define EEPROM_SPLITPOINT 12
#define EEPROM_KEYTRACK_U 13
#define EEPROM_KEYTRACK_L 14

int getMIDIChannel() {
  byte midiChannel = EEPROM.read(EEPROM_MIDI_CH);
  if (midiChannel < 0 || midiChannel > 16) midiChannel = MIDI_CHANNEL_OMNI;//If EEPROM has no MIDI channel stored
  return midiChannel;
}

void storeMidiChannel(byte channel)
{
  EEPROM.update(EEPROM_MIDI_CH, channel);
}

int getSplitPoint() {
  byte splitpoint = EEPROM.read(EEPROM_SPLITPOINT);
  return splitpoint;
}

void storeSplitPoint(byte splitpoint)
{
  EEPROM.update(EEPROM_SPLITPOINT, splitpoint);
}

float getAfterTouchU() {
 byte AfterTouchDestU = EEPROM.read(EEPROM_AFTERTOUCH_U);
 if (AfterTouchDestU == 0) return 0;
 if (AfterTouchDestU == 1) return 1;
 return AfterTouchDestU; //If EEPROM has no key tracking stored
}

float getAfterTouchL() {
 byte AfterTouchDestL = EEPROM.read(EEPROM_AFTERTOUCH_L);
 if (AfterTouchDestL == 0) return 0;
 if (AfterTouchDestL == 1) return 1;
 return AfterTouchDestL; //If EEPROM has no key tracking stored
}

void storeAfterTouchU(byte AfterTouchDestU)
{
 EEPROM.update(EEPROM_AFTERTOUCH_U, AfterTouchDestU);
}

void storeAfterTouchL(byte AfterTouchDestL)
{
 EEPROM.update(EEPROM_AFTERTOUCH_L, AfterTouchDestL);
}

//int getPitchBendRange() {
//  byte pitchbend = EEPROM.read(EEPROM_PITCHBEND);
//  if (pitchbend < 1 || pitchbend > 12) return pitchBendRange; //If EEPROM has no pitchbend stored
//  return pitchbend;
//}
//
//void storePitchBendRange(byte pitchbend)
//{
//  EEPROM.update(EEPROM_PITCHBEND, pitchbend);
//}

float getModWheelDepth() {
  int mw = EEPROM.read(EEPROM_MODWHEEL_DEPTH);
  if (mw < 1 || mw > 10) return modWheelDepth; //If EEPROM has no mod wheel depth stored
  return mw;
}

void storeModWheelDepth(float mwDepth)
{
  int mw =  mwDepth;
  EEPROM.update(EEPROM_MODWHEEL_DEPTH, mw);
}

boolean getEncoderDir() {
  byte ed = EEPROM.read(EEPROM_ENCODER_DIR); 
  if (ed < 0 || ed > 1)return true; //If EEPROM has no encoder direction stored
  return ed == 1 ? true : false;
}

void storeEncoderDir(byte encoderDir)
{
  EEPROM.update(EEPROM_ENCODER_DIR, encoderDir);
}

boolean getFilterEnvU() {
  byte fenv = EEPROM.read(EEPROM_FILTERENV_U); 
  if (fenv < 0 || fenv > 1)return true;
  return fenv == 0 ? false : true;
}

boolean getFilterEnvL() {
  byte fenv = EEPROM.read(EEPROM_FILTERENV_L); 
  if (fenv < 0 || fenv > 1)return true;
  return fenv == 0 ? false : true;
}

void storeFilterEnvU(byte filterLogLinU)
{
  EEPROM.update(EEPROM_FILTERENV_U, filterLogLinU);
}

void storeFilterEnvL(byte filterLogLinL)
{
  EEPROM.update(EEPROM_FILTERENV_L, filterLogLinL);
}

boolean getAmpEnvU() {
  byte aenv = EEPROM.read(EEPROM_AMPENV_U); 
  if (aenv < 0 || aenv > 1)return true;
  return aenv == 0 ? false : true;
}

boolean getAmpEnvL() {
  byte aenv = EEPROM.read(EEPROM_AMPENV_L); 
  if (aenv < 0 || aenv > 1)return true;
  return aenv == 0 ? false : true;
}

void storeAmpEnvU(byte ampLogLinU)
{
  EEPROM.update(EEPROM_AMPENV_U, ampLogLinU);
}

void storeAmpEnvL(byte ampLogLinL)
{
  EEPROM.update(EEPROM_AMPENV_L, ampLogLinL);
}

boolean getKeyTrackU() {
  byte keyTrackSWU = EEPROM.read(EEPROM_KEYTRACK_U); 
  if (keyTrackSWU < 0 || keyTrackSWU > 1)return true;
  return keyTrackSWU == 0 ? false : true;
}

boolean getKeyTrackL() {
  byte keyTrackSWL = EEPROM.read(EEPROM_KEYTRACK_L); 
  if (keyTrackSWL < 0 || keyTrackSWL > 1)return true;
  return keyTrackSWL == 0 ? false : true;
}

void storeKeyTrackU(byte keyTrackSWU)
{
  EEPROM.update(EEPROM_KEYTRACK_U, keyTrackSWU);
}

void storeKeyTrackL(byte keyTrackSWL)
{
  EEPROM.update(EEPROM_KEYTRACK_L, keyTrackSWL);
}

int getLastPatchU() {
  int lastPatchNumberU = EEPROM.read(EEPROM_LAST_PATCHU);
  if (lastPatchNumberU < 1 || lastPatchNumberU > 999) lastPatchNumberU = 1;
  return lastPatchNumberU;
}

int getLastPatchL() {
  int lastPatchNumberL = EEPROM.read(EEPROM_LAST_PATCHL);
  if (lastPatchNumberL < 1 || lastPatchNumberL > 999) lastPatchNumberL = 1;
  return lastPatchNumberL;
}

void storeLastPatchU(int lastPatchNumber)
{ 
EEPROM.update(EEPROM_LAST_PATCHU, lastPatchNumber);
}

void storeLastPatchL(int lastPatchNumber)
{
EEPROM.update(EEPROM_LAST_PATCHL, lastPatchNumber);
}
