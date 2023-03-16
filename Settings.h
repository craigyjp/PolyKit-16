#define SETTINGSOPTIONSNO 11
#define SETTINGSVALUESNO 18  //Maximum number of settings option values needed
int settingsValueIndex = 0;  //currently selected settings option value index

struct SettingsOption {
  char *option;                   //Settings option string
  char *value[SETTINGSVALUESNO];  //Array of strings of settings option values
  int handler;                    //Function to handle the values for this settings option
  int currentIndex;               //Function to array index of current value for this settings option
};

void settingsMIDICh(char *value);
//void settingsSplitPoint(char *value);
void settingsAfterTouchU(char *value);
void settingsAfterTouchL(char *value);
//void settingsPitchBend(char * value);
void settingsModWheelDepth(char *value);
void settingsEncoderDir(char *value);
void settingsFilterEnvU(char *value);
void settingsFilterEnvL(char *value);
void settingsAmpEnvU(char *value);
void settingsAmpEnvL(char *value);
void settingsKeyTrackU(char *value);
void settingsKeyTrackL(char *value);
void settingsHandler(char *s, void (*f)(char *));

int currentIndexMIDICh();
//int currentIndexSplitPoint();
int currentIndexAfterTouchU();
int currentIndexAfterTouchL();
//int currentIndexPitchBend();
int currentIndexModWheelDepth();
int currentIndexEncoderDir();
int currentIndexFilterEnvU();
int currentIndexFilterEnvL();
int currentIndexAmpEnvU();
int currentIndexAmpEnvL();
int currentIndexKeyTrackU();
int currentIndexKeyTrackL();
int getCurrentIndex(int (*f)());


void settingsMIDICh(char *value) {
  if (strcmp(value, "ALL") == 0) {
    midiChannel = MIDI_CHANNEL_OMNI;
  } else {
    midiChannel = atoi(value);
  }
  storeMidiChannel(midiChannel);
}

void settingsSplitPoint(char *value) {
    splitPoint = atoi(value);
  storeSplitPoint(splitPoint);
}

void settingsAfterTouchU(char *value) {
  if (strcmp(value, "Off") == 0) AfterTouchDestU = 0;
  if (strcmp(value, "DCO Mod") == 0) AfterTouchDestU = 1;
  if (strcmp(value, "CutOff Freq") == 0) AfterTouchDestU = 2;
  if (strcmp(value, "VCF Mod") == 0) AfterTouchDestU = 3;
  storeAfterTouchU(AfterTouchDestU);
}

void settingsAfterTouchL(char *value) {
  if (strcmp(value, "Off") == 0) AfterTouchDestL = 0;
  if (strcmp(value, "DCO Mod") == 0) AfterTouchDestL = 1;
  if (strcmp(value, "CutOff Freq") == 0) AfterTouchDestL = 2;
  if (strcmp(value, "VCF Mod") == 0) AfterTouchDestL = 3;
  storeAfterTouchL(AfterTouchDestL);
}

//
//void settingsPitchBend(char * value) {
//  pitchBendRange = atoi(value);
//  storePitchBendRange(pitchBendRange);
//}
//
void settingsModWheelDepth(char *value) {
  modWheelDepth = atoi(value);
  storeModWheelDepth(modWheelDepth);
}

void settingsEncoderDir(char *value) {
  if (strcmp(value, "Type 1") == 0) {
    encCW = true;
  } else {
    encCW = false;
  }
  storeEncoderDir(encCW ? 1 : 0);
}

void settingsFilterEnvU(char *value) {
  if (strcmp(value, "Log") == 0) {
    filterLogLinU = true;
  } else {
    filterLogLinU = false;
  }
  storeFilterEnvU(filterLogLinU ? 1 : 0);
}

void settingsFilterEnvL(char *value) {
  if (strcmp(value, "Log") == 0) {
    filterLogLinL = true;
  } else {
    filterLogLinL = false;
  }
  storeFilterEnvL(filterLogLinL ? 1 : 0); 
}

void settingsAmpEnvU(char *value) {
  if (strcmp(value, "Log") == 0) {
    ampLogLinU = true;
  } else {
    ampLogLinU = false;
  }
  storeAmpEnvU(ampLogLinU ? 1 : 0);
}

void settingsAmpEnvL(char *value) {
  if (strcmp(value, "Log") == 0) {
    ampLogLinL = true;
  } else {
    ampLogLinL = false;
  }
  storeAmpEnvL(ampLogLinL ? 1 : 0);
}

void settingsKeyTrackU(char *value) {
  if (strcmp(value, "Off") == 0) keyTrackSWU = 0;
  if (strcmp(value, "On") == 0) keyTrackSWU = 1;
  storeKeyTrackU(keyTrackSWU);
}

void settingsKeyTrackL(char *value) {
  if (strcmp(value, "Off") == 0) keyTrackSWL = 0;
  if (strcmp(value, "On") == 0) keyTrackSWL = 1;
  storeKeyTrackL(keyTrackSWL);
}

//Takes a pointer to a specific method for the settings option and invokes it.
void settingsHandler(char *s, void (*f)(char *)) {
  f(s);
}

int currentIndexMIDICh() {
  return getMIDIChannel();
}

int currentIndexSplitPoint() {
  return getSplitPoint();
}

int currentIndexAfterTouchU() {
  return getAfterTouchU();
}

int currentIndexAfterTouchL() {
  return getAfterTouchL();
}

//
//int currentIndexPitchBend() {
//  return  getPitchBendRange() - 1;
//}
//
int currentIndexModWheelDepth() {
  return (getModWheelDepth()) - 1;
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

//Takes a pointer to a specific method for the current settings option value and invokes it.
int getCurrentIndex(int (*f)()) {
  return f();
}

CircularBuffer<SettingsOption, SETTINGSOPTIONSNO> settingsOptions;

// add settings to the circular buffer
void setUpSettings() {
  settingsOptions.push(SettingsOption{ "MIDI Ch.", { "All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", '\0' }, settingsMIDICh, currentIndexMIDICh });
  //settingsOptions.push(SettingsOption{ "Split Point", { "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", '\0' }, settingsSplitPoint, currentIndexSplitPoint });
  settingsOptions.push(SettingsOption{ "AfterTouch U", { "Off", "DCO Mod", "CutOff Freq", "VCF Mod", '\0' }, settingsAfterTouchU, currentIndexAfterTouchU });
  settingsOptions.push(SettingsOption{ "AfterTouch L", { "Off", "DCO Mod", "CutOff Freq", "VCF Mod", '\0' }, settingsAfterTouchL, currentIndexAfterTouchL });
  //  settingsOptions.push(SettingsOption{"Pitch Bend", {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", '\0'}, settingsPitchBend, currentIndexPitchBend});
  settingsOptions.push(SettingsOption{ "MW Depth", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", '\0' }, settingsModWheelDepth, currentIndexModWheelDepth });
  settingsOptions.push(SettingsOption{ "Encoder", { "Type 1", "Type 2", '\0' }, settingsEncoderDir, currentIndexEncoderDir });
  settingsOptions.push(SettingsOption{ "Filter Env U", { "Log", "Lin", '\0' }, settingsFilterEnvU, currentIndexFilterEnvU });
  settingsOptions.push(SettingsOption{ "Filter Env L", { "Log", "Lin", '\0' }, settingsFilterEnvL, currentIndexFilterEnvL });
  settingsOptions.push(SettingsOption{ "Amp Env U", { "Log", "Lin", '\0' }, settingsAmpEnvU, currentIndexAmpEnvU });
  settingsOptions.push(SettingsOption{ "Amp Env L", { "Log", "Lin", '\0' }, settingsAmpEnvL, currentIndexAmpEnvL });
  settingsOptions.push(SettingsOption{ "Keytrack U", { "Off", "On", '\0' }, settingsKeyTrackU, currentIndexKeyTrackU });
  settingsOptions.push(SettingsOption{ "Keytrack L", { "Off", "On", '\0' }, settingsKeyTrackL, currentIndexKeyTrackL });
}