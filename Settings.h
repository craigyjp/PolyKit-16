#include "SettingsService.h"

void settingsMIDICh();
void settingsSplitPoint();
void settingsSplitTrans();
void settingsMonoMultiU();
void settingsMonoMultiL();
void settingsAfterTouchU();
void settingsAfterTouchL();
void settingsPitchBend();
void settingsModWheelDepth();
void settingsEncoderDir();
void settingsFilterEnvU();
void settingsFilterEnvL();
void settingsAmpEnvU();
void settingsAmpEnvL();
void settingsKeyTrackU();
void settingsKeyTrackL();


int currentIndexMIDICh();
int currentIndexSplitPoint();
int currentIndexSplitTrans();
int currentIndexMonoMultiU();
int currentIndexMonoMultiL();
int currentIndexAfterTouchU();
int currentIndexAfterTouchL();
int currentIndexPitchBend();
int currentIndexModWheelDepth();
int currentIndexEncoderDir();
int currentIndexFilterEnvU();
int currentIndexFilterEnvL();
int currentIndexAmpEnvU();
int currentIndexAmpEnvL();
int currentIndexKeyTrackU();
int currentIndexKeyTrackL();

void settingsSplitPoint(int index, const char *value) {
  if (strcmp(value, "36") == 0) newsplitPoint = 0;
  if (strcmp(value, "37") == 0) newsplitPoint = 1;
  if (strcmp(value, "38") == 0) newsplitPoint = 2;
  if (strcmp(value, "39") == 0) newsplitPoint = 3;
  if (strcmp(value, "40") == 0) newsplitPoint = 4;
  if (strcmp(value, "41") == 0) newsplitPoint = 5;
  if (strcmp(value, "42") == 0) newsplitPoint = 6;
  if (strcmp(value, "43") == 0) newsplitPoint = 7;
  if (strcmp(value, "44") == 0) newsplitPoint = 8;
  if (strcmp(value, "45") == 0) newsplitPoint = 9;
  if (strcmp(value, "46") == 0) newsplitPoint = 10;
  if (strcmp(value, "47") == 0) newsplitPoint = 11;
  if (strcmp(value, "48") == 0) newsplitPoint = 12;
  if (strcmp(value, "49") == 0) newsplitPoint = 13;
  if (strcmp(value, "50") == 0) newsplitPoint = 14;
  if (strcmp(value, "51") == 0) newsplitPoint = 15;
  if (strcmp(value, "52") == 0) newsplitPoint = 16;
  if (strcmp(value, "53") == 0) newsplitPoint = 17;
  if (strcmp(value, "54") == 0) newsplitPoint = 18;
  if (strcmp(value, "55") == 0) newsplitPoint = 19;
  if (strcmp(value, "56") == 0) newsplitPoint = 20;
  if (strcmp(value, "57") == 0) newsplitPoint = 21;
  if (strcmp(value, "58") == 0) newsplitPoint = 22;
  if (strcmp(value, "59") == 0) newsplitPoint = 23;
  if (strcmp(value, "60") == 0) newsplitPoint = 24;
  storeSplitPoint(newsplitPoint);
}

void settingsSplitTrans(int index, const char *value) {
  if (strcmp(value, "-2 Octave") == 0) splitTrans = 0;
  if (strcmp(value, "-1 Octave") == 0) splitTrans = 1;
  if (strcmp(value, "Original") == 0) splitTrans = 2;
  if (strcmp(value, "+1 Octave") == 0) splitTrans = 3;
  if (strcmp(value, "+2 Octave") == 0) splitTrans = 4;
  storeSplitTrans(splitTrans);
}

void settingsMonoMultiU(int index, const char *value) {
  if (strcmp(value, "Off") == 0) monoMultiU = 0;
  if (strcmp(value, "On") == 0) monoMultiU = 1;
  storeMonoMultiU(monoMultiU);
}

void settingsMonoMultiL(int index, const char *value) {
  if (strcmp(value, "Off") == 0) monoMultiL = 0;
  if (strcmp(value, "On") == 0) monoMultiL = 1;
  storeMonoMultiL(monoMultiL);
}

void settingsMIDICh(int index, const char *value) {
  if (strcmp(value, "ALL") == 0) {
    midiChannel = MIDI_CHANNEL_OMNI;
  } else {
    midiChannel = atoi(value);
  }
  storeMidiChannel(midiChannel);
}

void settingsAfterTouchU(int index, const char *value) {
  if (strcmp(value, "Off") == 0) AfterTouchDestU = 0;
  if (strcmp(value, "DCO Mod") == 0) AfterTouchDestU = 1;
  if (strcmp(value, "CutOff Freq") == 0) AfterTouchDestU = 2;
  if (strcmp(value, "VCF Mod") == 0) AfterTouchDestU = 3;
  if (strcmp(value, "VCA Mod") == 0) AfterTouchDestU = 4;
  storeAfterTouchU(AfterTouchDestU);
}

void settingsAfterTouchL(int index, const char *value) {
  if (strcmp(value, "Off") == 0) AfterTouchDestL = 0;
  if (strcmp(value, "DCO Mod") == 0) AfterTouchDestL = 1;
  if (strcmp(value, "CutOff Freq") == 0) AfterTouchDestL = 2;
  if (strcmp(value, "VCF Mod") == 0) AfterTouchDestL = 3;
  if (strcmp(value, "VCA Mod") == 0) AfterTouchDestL = 4;
  storeAfterTouchL(AfterTouchDestL);
}

void settingsPitchBend(int index, const char *value) {
  pitchBendRange = atoi(value);
  storePitchBendRange(pitchBendRange);
}

void settingsModWheelDepth(int index, const char *value) {
  modWheelDepth = atoi(value);
  storeModWheelDepth(modWheelDepth);
}

void settingsEncoderDir(int index, const char *value) {
  if (strcmp(value, "Type 1") == 0) {
    encCW = true;
  } else {
    encCW = false;
  }
  storeEncoderDir(encCW ? 1 : 0);
}

void settingsFilterEnvU(int index, const char *value) {
  if (strcmp(value, "Log") == 0) {
    filterLogLinU = true;
  } else {
    filterLogLinU = false;
  }
  storeFilterEnvU(filterLogLinU ? 1 : 0);
}

void settingsFilterEnvL(int index, const char *value) {
  if (strcmp(value, "Log") == 0) {
    filterLogLinL = true;
  } else {
    filterLogLinL = false;
  }
  storeFilterEnvL(filterLogLinL ? 1 : 0);
}

void settingsAmpEnvU(int index, const char *value) {
  if (strcmp(value, "Log") == 0) {
    ampLogLinU = true;
  } else {
    ampLogLinU = false;
  }
  storeAmpEnvU(ampLogLinU ? 1 : 0);
}

void settingsAmpEnvL(int index, const char *value) {
  if (strcmp(value, "Log") == 0) {
    ampLogLinL = true;
  } else {
    ampLogLinL = false;
  }
  storeAmpEnvL(ampLogLinL ? 1 : 0);
}

void settingsKeyTrackU(int index, const char *value) {
  if (strcmp(value, "Off") == 0) keyTrackSWU = 0;
  if (strcmp(value, "On") == 0) keyTrackSWU = 1;
  storeKeyTrackU(keyTrackSWU);
}

void settingsKeyTrackL(int index, const char *value) {
  if (strcmp(value, "Off") == 0) keyTrackSWL = 0;
  if (strcmp(value, "On") == 0) keyTrackSWL = 1;
  storeKeyTrackL(keyTrackSWL);
}

int currentIndexSplitTrans() {
  return getSplitTrans();
}

int currentIndexMIDICh() {
  return getMIDIChannel();
}

int currentIndexSplitPoint() {
  return getSplitPoint();
}

int currentIndexMonoMultiU() {
  return getMonoMultiU();
}

int currentIndexMonoMultiL() {
  return getMonoMultiL();
}

int currentIndexAfterTouchU() {
  return getAfterTouchU();
}

int currentIndexAfterTouchL() {
  return getAfterTouchL();
}

int currentIndexPitchBend() {
  return getPitchBendRange() - 1;
}

int currentIndexModWheelDepth() {
  return getModWheelDepth() - 1;
}

int currentIndexEncoderDir() {
  return getEncoderDir() ? 0 : 1;
}

int currentIndexFilterEnvU() {
  return getFilterEnvU() ? 0 : 1;
}

int currentIndexFilterEnvL() {
  return getFilterEnvL() ? 0 : 1;
}

int currentIndexAmpEnvU() {
  return getAmpEnvU() ? 0 : 1;
}

int currentIndexAmpEnvL() {
  return getAmpEnvL() ? 0 : 1;
}

int currentIndexKeyTrackU() {
  return getKeyTrackU();
}

int currentIndexKeyTrackL() {
  return getKeyTrackL();
}

// add settings to the circular buffer
void setUpSettings() {
  settings::append(settings::SettingsOption{ "MIDI Ch.", { "All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "\0" }, settingsMIDICh, currentIndexMIDICh });
  settings::append(settings::SettingsOption{ "Split Point", { "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "\0" }, settingsSplitPoint, currentIndexSplitPoint });
  settings::append(settings::SettingsOption{ "Split Trans", { "-2 Octave", "-1 Octave", "Original", "+1 Octave", "+2 Octave", "\0" }, settingsSplitTrans, currentIndexSplitTrans });
  settings::append(settings::SettingsOption{ "Pitch Bend", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "\0" }, settingsPitchBend, currentIndexPitchBend });
  settings::append(settings::SettingsOption{ "MW Depth", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "\0" }, settingsModWheelDepth, currentIndexModWheelDepth });
  settings::append(settings::SettingsOption{ "Multi Trig U", { "Off", "On", "\0" }, settingsMonoMultiU, currentIndexMonoMultiU });
  settings::append(settings::SettingsOption{ "Multi Trig L", { "Off", "On", "\0" }, settingsMonoMultiL, currentIndexMonoMultiL });
  settings::append(settings::SettingsOption{ "AfterTouch U", { "Off", "DCO Mod", "CutOff Freq", "VCF Mod", "VCA Mod", "\0" }, settingsAfterTouchU, currentIndexAfterTouchU });
  settings::append(settings::SettingsOption{ "AfterTouch L", { "Off", "DCO Mod", "CutOff Freq", "VCF Mod", "VCA Mod", "\0" }, settingsAfterTouchL, currentIndexAfterTouchL });
  settings::append(settings::SettingsOption{ "Filter Env U", { "Log", "Lin", "\0" }, settingsFilterEnvU, currentIndexFilterEnvU });
  settings::append(settings::SettingsOption{ "Filter Env L", { "Log", "Lin", "\0" }, settingsFilterEnvL, currentIndexFilterEnvL });
  settings::append(settings::SettingsOption{ "Amp Env U", { "Log", "Lin", "\0" }, settingsAmpEnvU, currentIndexAmpEnvU });
  settings::append(settings::SettingsOption{ "Amp Env L", { "Log", "Lin", "\0" }, settingsAmpEnvL, currentIndexAmpEnvL });
  settings::append(settings::SettingsOption{ "Keytrack U", { "Off", "On", "\0" }, settingsKeyTrackU, currentIndexKeyTrackU });
  settings::append(settings::SettingsOption{ "Keytrack L", { "Off", "On", "\0" }, settingsKeyTrackL, currentIndexKeyTrackL });
  settings::append(settings::SettingsOption{ "Encoder", { "Type 1", "Type 2", "\0" }, settingsEncoderDir, currentIndexEncoderDir });
}