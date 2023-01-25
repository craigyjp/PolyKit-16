// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

//Teensy 3.6 - Mux Pins
#define MUX_0 30
#define MUX_1 31
#define MUX_2 32
#define MUX_3 29

#define MUX1_S A0
#define MUX2_S A1
#define MUX3_S A2
#define MUX4_S A3
#define MUX5_S A4

#define DEMUX_0 36
#define DEMUX_1 35
#define DEMUX_2 34
#define DEMUX_3 33

#define DEMUX_EN_1 37
#define DEMUX_EN_2 38
#define DEMUX_EN_3 39

//Note DAC
#define DACMULT 25.75
#define DAC_NOTE1 7
#define NOTE_DAC(CH) (CH==0 ? DAC_NOTE1)
#define NOTE_AB(CH)  (CH==1 ? 1 : 0)

//Mux 1 Connections
#define MUX1_PBDepth 0
#define MUX1_pwLFO 1
#define MUX1_osc2PW 2
#define MUX1_osc1PW 3
#define MUX1_MWDepth 4
#define MUX1_fmDepth 5
#define MUX1_osc2PWM 6
#define MUX1_osc1PWM 7

#define MUX1_osc1Range 8
#define MUX1_noiseLevel 9
#define MUX1_osc1SawLevel 10
#define MUX1_osc2Detune 11
#define MUX1_osc2Range 12
#define MUX1_stack 13
#define MUX1_osc2SawLevel 14
#define MUX1_glideTime 15

//Mux 3 Connections
#define MUX2_filterType 0
#define MUX2_LFOWaveform 1
#define MUX2_LFODelay 2
#define MUX2_LFORate 3
#define MUX2_osc2TriangleLevel 4
#define MUX2_osc2PulseLevel 5 // spare mux output - decouple from DAC
#define MUX2_osc1PulseLevel 6
#define MUX2_osc1TriangleLevel 7 // spare mux output

#define MUX2_filterDecay 8
#define MUX2_ampAttack 9
#define MUX2_filterAttack 10
#define MUX2_filterRes 11
#define MUX2_filterCutoff 12 // spare mux output - decouple from DAC
#define MUX2_filterEGlevel 13
#define MUX2_keyTrack 14
#define MUX2_filterLFO 15

#define MUX3_volumeControl 0
#define MUX3_balance 1
#define MUX3_ampSustain 2
#define MUX3_ampRelease 3
#define MUX3_filterRelease 4 // spare mux output - decouple from DAC
#define MUX3_filterSustain 5
#define MUX3_ampDecay 6


//DeMux 1 Connections
#define DEMUX1_noiseLevel 0                 // 0-2v
#define DEMUX1_osc2PulseLevel 1             // 0-2v
#define DEMUX1_osc2Level 2                  // 0-2v
#define DEMUX1_LfoDepth 3                   // 0-2v
#define DEMUX1_osc1PWM 4                    // 0-2v
#define DEMUX1_PitchBend 5                  // 0-2v
#define DEMUX1_osc1PulseLevel 6             // 0-2v
#define DEMUX1_osc1Level 7                  // 0-2v
#define DEMUX1_modwheel 8                   // 0-2v
#define DEMUX1_volumeControl 9              // 0-2v
#define DEMUX1_osc2PWM 10                   // 0-2v
#define DEMUX1_spare 11                     // spare VCA 0-2v
#define DEMUX1_AmpEGLevel 12                // Amplevel 0-5v
#define DEMUX1_MasterTune 13                // -15v to +15v  control 12   +/-13v
#define DEMUX1_osc2Detune 14                // -15v to +15v control 17 +/-13v
#define DEMUX1_pitchEGLevel 15              // PitchEGlevel 0-5v

//DeMux 2 Connections
#define DEMUX2_Amprelease 0                 // 0-5v
#define DEMUX2_Ampsustain 1                 // 0-5v
#define DEMUX2_Ampdecay 2                   // 0-5v
#define DEMUX2_Ampattack 3                  // 0-5v
#define DEMUX2_LfoRate 4                    // 0-5v
#define DEMUX2_LFOMULTI 5                   // 0-5v
#define DEMUX2_LFOWaveform 6                // 0-5v
#define DEMUX2_LfoDelay 7                   // 0-5v
#define DEMUX2_osc2PW 8                     // 0-5v
#define DEMUX2_osc1PW 9                     // 0-5v
#define DEMUX2_filterEGlevel 10             // FilterEGlevel 0-5v  
#define DEMUX2_ADSRMode 11                  // 0-5v
#define DEMUX2_pwLFO 12                     // 0-5v 
#define DEMUX2_LfoSlope 13                  // 0-5v 
#define DEMUX2_filterCutoff 14              // 0-10v
#define DEMUX2_filterRes15                  // 0-10v

//DeMux 3 Connections
#define DEMUX3_filterA 0                    // filterA 0-5v switched
#define DEMUX3_filterB 1                    // filterB 0-5v switched
#define DEMUX3_filterC 2                    // filterC 0-5v switched
#define DEMUX3_filterLoopA0 3                     // syncA0 0-5v switched
#define DEMUX3_filterLoopA1 4                     // syncA1 0-5v switched    
#define DEMUX3_LFOalt 5                     // lFOalt 0-5v switched
#define DEMUX3_spare 6                      // spare 0-5v switched
#define DEMUX3_spare1 7                      // spare 0-5v switched
#define DEMUX3_pitchAttack 8                // Pitchattack 0-5v
#define DEMUX3_pitchDecay 9                 // Pitchdecay 0-5v
#define DEMUX3_pitchSustain 10              // Pitchsustain  0-5v
#define DEMUX3_PitchRelease 11              // Pitchrelease 0-5v
#define DEMUX3_filterAttack 12              // FilterAattack 0-5v
#define DEMUX3_filterDecay 13               // Filterdecay 0-5v
#define DEMUX3_filterSustain 14             // Filtersustain 0-5v
#define DEMUX3_filterEelease 14             // Filterrelease 0-5v

// 595 outputs

#define FILTER_EG_INV 10
#define FILTER_POLE 11
#define FILTER_KEYTRACK 12
#define AMP_VELOCITY 13
#define FILTER_VELOCITY 14
#define AMP_LIN_LOG 15
#define FILTER_LIN_LOG 16
#define LFO_ALT 17
#define AMP_LOOP 20
#define FILTER_LOOP 21
#define CHORUS1_OUT 0 
#define CHORUS2_OUT 1

// 595 LEDs


#define CHORUS1_LED 0 
#define CHORUS2_LED 1
#define GLIDE_LED 2
#define VCALOOP_LED 3
#define VCAGATE_LED 4
#define VCAVEL_LED 5
#define LFO_ALT_LED 6 
#define FILTERPOLE_LED 7

#define FILTERLOOP_LED 8
#define FILTERINV_LED 9
#define FILTERVEL_LED 10
#define UPPER_LED 11
#define SAVE_LED 12

//Switches
// roxmux 74HC165

#define CHORUS1_SW 0
#define CHORUS2_SW 1
#define GLIDE_SW 2
#define VCALOOP_SW 3
#define VCAGATE_SW 4
#define VCAVEL_SW 5
#define LFO_ALT_SW 6
#define FILTERPOLE_SW 7

#define FILTERLOOP_SW 8
#define FILTERINV_SW 9
#define FILTERVEL_SW 10
#define UPPER_SW 11

// System Switches etc

#define RECALL_SW 6
#define SAVE_SW 24
#define SETTINGS_SW 25
#define BACK_SW 28

#define ENCODER_PINA 4
#define ENCODER_PINB 5

#define MUXCHANNELS 16
#define DEMUXCHANNELS 16
#define QUANTISE_FACTOR 2

#define DEBOUNCE 30

static byte muxInput = 0;
static byte muxOutput = 0;
static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;


static long encPrevious = 0;

Bounce recallButton = Bounce(RECALL_SW, DEBOUNCE); //On encoder
boolean recall = true; //Hack for recall button
Bounce saveButton = Bounce(SAVE_SW, DEBOUNCE);
boolean del = true; //Hack for save button
Bounce settingsButton = Bounce(SETTINGS_SW, DEBOUNCE);
boolean reini = true; //Hack for settings button
Bounce backButton = Bounce(BACK_SW, DEBOUNCE);
boolean panic = true; //Hack for back button
Encoder encoder(ENCODER_PINB, ENCODER_PINA);//This often needs the pins swapping depending on the encoder

void setupHardware()
{
     //Volume Pot is on ADC0
  adc->adc0->setAveraging(32); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(8); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::LOW_SPEED); // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::LOW_SPEED); // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(32); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(8); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::LOW_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::LOW_SPEED); // change the sampling speed

  analogReadResolution(8);

  //Mux address pins

  pinMode(DAC_NOTE1, OUTPUT);
  digitalWrite(DAC_NOTE1, HIGH);

  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  pinMode(DEMUX_0, OUTPUT);
  pinMode(DEMUX_1, OUTPUT);
  pinMode(DEMUX_2, OUTPUT);
  pinMode(DEMUX_3, OUTPUT);

  digitalWrite(MUX_0, LOW);
  digitalWrite(MUX_1, LOW);
  digitalWrite(MUX_2, LOW);
  digitalWrite(MUX_3, LOW);

  digitalWrite(DEMUX_0, LOW);
  digitalWrite(DEMUX_1, LOW);
  digitalWrite(DEMUX_2, LOW);
  digitalWrite(DEMUX_3, LOW);

  pinMode(DEMUX_EN_1, OUTPUT);
  pinMode(DEMUX_EN_2, OUTPUT);
  pinMode(DEMUX_EN_3, OUTPUT);

  digitalWrite(DEMUX_EN_1, HIGH);
  digitalWrite(DEMUX_EN_2, HIGH);
  digitalWrite(DEMUX_EN_3, HIGH);


  //Switches

  pinMode(RECALL_SW, INPUT_PULLUP); //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);
  
}
