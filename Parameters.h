//Values below are just for initialising and will be changed when synth is initialised to current panel controls & EEPROM settings
byte midiChannel = MIDI_CHANNEL_OMNI;//(EEPROM)
String patchNameU = INITPATCHNAME;
String patchNameL = INITPATCHNAME;
String patchName = INITPATCHNAME;
int upperpatchtag = 0;
int lowerpatchtag = 1;
byte splitPoint = 0;
byte oldsplitPoint = 0;
byte newsplitPoint = 0;
byte splitTrans = 0;
byte oldsplitTrans = 0;
int lowerTranspose = 0;
float keytrackingAmount = 0.5;

//Delayed LFO
int numberOfNotes = 0;
int oldnumberOfNotes = 0;
unsigned long previousMillisL = 0;
unsigned long intervalL = 1; //10 seconds
long delaytimeL  = 0;
unsigned long previousMillisU = 0;
unsigned long intervalU = 1; //10 seconds
long delaytimeU  = 0;

boolean encCW = true;//This is to set the encoder to increment when turned CW - Settings Option
boolean announce = true;
// polykit parameters in order of mux

float pwLFO = 0;
float pwLFOU = 0;
float pwLFOL= 0;
float pwLFOstr = 0; // for display
float fmDepth = 0;
float fmDepthU = 0;
float fmDepthL = 0;
float fmDepthstr = 0;
float osc2PW = 0;
float osc2PWU = 0;
float osc2PWL = 0;
float osc2PWstr = 0;
float osc2PWM = 0;
float osc2PWMU = 0;
float osc2PWML = 0;
float osc2PWMstr = 0;
float osc1PW = 0;
float osc1PWU = 0;
float osc1PWL = 0;
float osc1PWstr = 0;
float osc1PWM = 0;
float osc1PWMU = 0;
float osc1PWML = 0;
float osc1PWMstr = 0;
float osc1Range = 0;
float osc1RangeU = 0;
float osc1RangeL = 0;
float osc1Rangestr = 0;
float oct1A = 0;
float oct1AU = 0;
float oct1AL = 0;
float oct1B = 0;
float oct1BU = 0;
float oct1BL = 0;
float osc2Range = 0;
float osc2RangeU = 0;
float osc2RangeL = 0;
float osc2Rangestr = 0;
float oct2A = 0;
float oct2AU = 0;
float oct2AL = 0;
float oct2B = 0;
float oct2BU = 0;
float oct2BL = 0;
float stack = 0;
float stackU = 0;
float stackL = 0;
float stackstr = 0;
float glideTime = 0;
float glideTimeU = 0;
float glideTimeL = 0;
float glideTimestr = 0;
float osc2Detune = 0;
float osc2DetuneU = 0;
float osc2DetuneL = 0;
float osc2Detunestr = 0;
float noiseLevel = 0;
float noiseLevelU = 0;
float noiseLevelL = 0;
float noiseLevelstr = 0;
float osc2SawLevelstr = 0;
float osc2SawLevel = 0;
float osc2SawLevelU = 0;
float osc2SawLevelL = 0;
float osc1SawLevel = 0;
float osc1SawLevelU = 0;
float osc1SawLevelL = 0;
float osc1SawLevelstr = 0;
float osc2PulseLevel = 0;
float osc2PulseLevelU = 0;
float osc2PulseLevelL = 0;
float osc2PulseLevelstr = 0;
float osc1PulseLevel = 0;
float osc1PulseLevelU = 0;
float osc1PulseLevelL = 0;
float osc1PulseLevelstr = 0;
float osc2TriangleLevel = 0;
float osc2TriangleLevelU = 0;
float osc2TriangleLevelL = 0;
float osc2TriangleLevelstr = 0; // for display
float osc1SubLevel = 0;
float osc1SubLevelU = 0;
float osc1SubLevelL = 0;
float osc1SubLevelstr = 0; // for display

float filterCutoff = 0;
float filterCutoffU = 0;
float filterCutoffL = 0;
float oldfilterCutoff = 0;
float oldfilterCutoffU = 0;
float oldfilterCutoffL = 0;
float filterCutoffstr = 0; // for display
float filterLFO = 0;
float filterLFOU = 0;
float filterLFOL = 0;
float filterLFOstr = 0; // for display
float filterRes = 0;
float filterResU = 0;
float filterResL = 0;
float filterResstr = 0;
float filterType = 0;
float filterTypeU = 0;
float filterTypeL = 0;
float filterTypestr = 0;
float filterEGlevel = 0;
float filterEGlevelU = 0;
float filterEGlevelL = 0;
float filterEGlevelstr = 0;
float LFORate = 0;
float LFORateU = 0;
float LFORateL = 0;
float LFORatestr = 0; //for display
float LFODelay = 0;
float LFODelayU = 0;
float LFODelayL = 0;
int LFODelayGoU = 0;
int LFODelayGoL = 0;
float LFODelaystr = 0; //for display
String StratusLFOWaveform = "                ";
float LFOWaveformstr = 0;
float LFOWaveform = 0;
float LFOWaveformU = 0;
float LFOWaveformL = 0;
float filterAttack = 0;
float filterAttackU = 0;
float filterAttackL = 0;
float filterAttackstr = 0;
float filterDecay = 0;
float filterDecayU = 0;
float filterDecayL = 0;
float filterDecaystr = 0;
float filterSustain = 0;
float filterSustainU = 0;
float filterSustainL = 0;
float filterSustainstr = 0;
float filterRelease = 0;
float filterReleaseU = 0;
float filterReleaseL = 0;
float filterReleasestr = 0;
float ampAttack = 0;
float ampAttackU = 0;
float ampAttackL = 0;
float oldampAttack = 0;
float oldampAttackU = 0;
float oldampAttackL = 0;
float ampAttackstr = 0;
float ampDecay = 0;
float ampDecayU = 0;
float ampDecayL = 0;
float oldampDecay = 0;
float oldampDecayU = 0;
float oldampDecayL = 0;
float ampDecaystr = 0;
float ampSustain = 0;
float ampSustainU = 0;
float ampSustainL = 0;
float oldampSustain = 0;
float oldampSustainU = 0;
float oldampSustainL = 0;
float ampSustainstr = 0;
float ampRelease = 0;
float ampReleaseU = 0;
float ampReleaseL = 0;
float oldampRelease = 0;
float oldampReleaseU = 0;
float oldampReleaseL = 0;
float ampReleasestr = 0;
float volumeControl = 0;
float volumeControlU = 0;
float volumeControlL = 0;
float volumeControlstr = 0; // for display
float amDepth = 0;
float amDepthU = 0;
float amDepthL = 0;
float amDepthstr = 0; // for display
float keytrack = 0;
float keytrackU = 0;
float keytrackL = 0;
float keytrackstr = 0;
int keyTrackSW = 0;
int keyTrackSWU = 0;
int keyTrackSWL = 0;

int pitchBendRange = 0;
int PitchBendLevel = 0;
int PitchBendLevelU = 0;
int PitchBendLevelL = 0;
int PitchBendLevelstr = 0; // for display
int modWheelDepth = 0;
int modWheelDepthU = 0;
int modWheelDepthL = 0;
float modWheelLevel = 0;
float modWheelLevelU = 0;
float modWheelLevelL = 0;
float modWheelLevelstr = 0;

int glideSW = 0;
int glideSWU = 0;
int glideSWL = 0;
int vcaLoop = 0;
int statevcaLoop = 0;
int oldvcaLoop = 0;
int vcaLoopU = 0;
int statevcaLoopU = 0;
int oldvcaLoopU = 0;
int vcaLoopL = 0;
int statevcaLoopL = 0;
int oldvcaLoopL = 0;
int vcadoubleLoop = 0;
int vcadoubleLoopU = 0;
int vcadoubleLoopL = 0;
int vcaVel = 0;
int vcaVelU = 0;
int vcaVelL = 0;
int vcaGate = 0;
int vcaGateU = 0;
int vcaGateL = 0;
int chorus1 = 0;
int chorus1U = 0;
int chorus1L = 0;
int chorus2 = 0;
int chorus2U = 0;
int chorus2L = 0;
int lfoAlt = 0;
int lfoAltU = 0;
int lfoAltL = 0;
int monoMulti = 0;
int monoMultiU = 0;
int monoMultiL = 0;
int oldmonoMultiU = 0;
int oldmonoMultiL = 0;
int filterPoleSWU = 0;
int filterPoleSWL = 0;
int filterPoleSW = 0;
int filterVel = 0;
int filterVelU = 0;
int filterVelL = 0;
int filterLoop = 0;
int statefilterLoop = 0;
int oldfilterLoop = 0;
int filterLoopU = 0;
int statefilterLoopU = 0;
int oldfilterLoopU = 0;
int filterLoopL = 0;
int statefilterLoopL = 0;
int oldfilterLoopL = 0;
int filterdoubleLoop = 0;
int filterdoubleLoopU = 0;
int filterdoubleLoopL = 0;
int filterEGinv = 0;
int filterEGinvU = 0;
int filterEGinvL = 0;
int upperSW = 0;
int oldupperSW = 0;
int filterLogLin = 0;
int filterLogLinU = 0;
int filterLogLinL = 0;
int ampLogLin = 0;
int ampLogLinU = 0;
int ampLogLinL = 0;

float afterTouch = 0;
float afterTouchU = 0;
float afterTouchL = 0;
int AfterTouchDest = 0;
int AfterTouchDestU = 0;
int oldAfterTouchDestU = 0;
int AfterTouchDestL = 0;
int oldAfterTouchDestL = 0;
int oldfilterLogLinU;
int oldfilterLogLinL;
int oldampLogLinU;
int oldampLogLinL;
int oldkeyTrackSWU;
int oldkeyTrackSWL;

int wholemode = 1;
int dualmode = 0;
int splitmode = 0;

int returnvalue = 0;
