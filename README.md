# PolyKit-16-polyphonic-synthesizer

Based on the PolyKit DCO and PolyKit-x-voice board projects by PolyKit (Jan Knipper). This design takes Jan's work and expands it into a fully programmable 16 voice DCO based synthesizer. 

![Synth](photos/synth.jpg)

The synth is comparable to a dual Juno 106 combined with a Crumar Bit 01 rack unit but with full control.

This repository contains my versions of the filter/adsr/lfo with added velocity, keytracking and additional 8 filter configurations and programmer section for the Polykit 16.


## Key features

- Digitally controlled oscillators similar in design to the Roland Juno series using dual Raspberry Pi Pico's.
- Dual DCO with Sawtooth, PWM, SUB oscillator & Triangle waveforms individual mixing of each.
- Digital Noise source for White or Pink noise mix.
- Multipole filters based on the Oberheim Matrix 12. 
- Dual LFOs for PWM and main synth, main LFO 16 waveforms, PWM LFO Sinewave only.
- 16 voice polyphonic dual DCOs per voice.
- Whole, Dual and Split modes of operation
- Aftertouch control over DCO LFO depth, AM Depth, filter LFO depth or filter cutoff.
- Log and lin envelopes.
- Gated or LFO Looping envelopes.
- Velocity sensitivity for filters and amplifiers.
- Analogue chorus similar to Juno 60.
- 999 memory locations to store patches.
- 128 Performance patches
- MIDI in/out/thru - channels 1-16
- USB MIDI

- How it sounds...

[https://youtu.be/yFFuoJo9930](https://youtu.be/pIsAd5T5sVk)

## Things to fix/improve

- Initial startup can cause issues in stack more and 32 foot mode, need to fix
- DCO1 and DCO1 are the wrong way around.
- Additional poly mode (POLY 2) available by press and holding the WHOLE button

## The source of the inspiration for these modifications and acknowledgements

Polykit, also known as Jan Knipper

https://github.com/polykit/pico-dco

This is how it sounds: [Ramp sample](https://soundcloud.com/polykit/pico-dco-ramp) [Pulse sample](https://soundcloud.com/polykit/pico-dco-pulse) [Polyphonic sample](https://soundcloud.com/polykit/pico-dco-polyphonic)

Freddie Renyard for his coding of the FM inputs and patience with me whilst I debugged the voice sync of two boards together.

https://github.com/freddie-renyard

## Component sections used to build the final synth

https://github.com/craigyjp/pico-dco

https://github.com/craigyjp/Pico-DCO-DAC

TC Electronics June-60 Chorus V2
