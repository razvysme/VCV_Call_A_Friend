# Call a Friend - an Eurorack Rythm generator

A modular synthesizer hardware sequencer and anti-aliased audio generator. The module constructs complex, syncopated loop sequences in additive chunks of $n/4$ timing using a vintage rotary telephone dial as the primary input mechanism. It prioritizes an instant, sub-menu-free, What-You-See-Is-What-You-Get performance workflow.

## Jacks
### Input
1. **Clock In:** Hardware Interrupt (`RISING`). Drives step progression.
2. **Reset In:** Hardware Interrupt (`RISING`).  Jumps to Group 0, Step 0. 
### Output 
1. **Phrase Start Out:** $10\text{ms}$ pulse fired exactly at Group 0, Step 0.
2. **Bar Start Out:** $10\text{ms}$ pulse fired every 4 clock pulses ($4/4$ downbeat grid metric).
3. **Group Start Out:** $10\text{ms}$ pulse fired on Step 0 of _any_ active `RhythmicGroup`.
4. **Accent Out:** $10\text{ms}$ pulse fired on internal steps based on the **Symmetry/Accent Hierarchy Engine**.
5. **Gate Out ($0\text{V}$ to $+10\text{V}$ Velocity-Gating):** Dynamic voltage gate output whose peak amplitude represents note velocity ($0\text{V}$ to $+5\text{V}$ baseline, scaled up to $+10\text{V}$ max on accents). Governed by the **Global Gate Length Knob** and **Note Density** threshold. Drops to $0\text{V}$ on rests or when filtered by Note Density.
6. **Unipolar CV Out ($0\text{V}$ to $+10\text{V}$):** Sample-and-hold linear unquantized pitch voltage. Updates only when a note triggers (holds its previous pitch when steps are silenced by Note Density). Attenuated directly toward $0\text{V}$ via the Pitch Attenuator knob.
7. **Bipolar CV Out ($-5\text{V}$ to $+5\text{V}$):** Zero-centered sample-and-hold pitch voltage. Updates only when a note triggers (holds its previous pitch when steps are silenced by Note Density). Attenuated symmetrically toward $0\text{V}$ via the Pitch Attenuator knob.
## H I D

** Two digit segment screen **

**Rotary Phone Dial**

Counts pulse trains on rotation release. Lockout debounce: $20\text{ms}$.
• **Normal Mode:** Appends new `RhythmicGroup` to array.
• **Save/Recall Mode:** Targets index slots 0-9.

**Symmetry Knob**
Reads $0-100\%$. Sampled **only** at the precise trailing edge of a phone dial pulse train. Baked directly into the active group's struct.

**Note Density Knob**
Reads $0-100\%$ ($0 = \text{no notes}$, $100 = \text{all notes}$). Acts as a real-time velocity threshold across the sequence. Each step generates an inner velocity ($0\text{V}-5\text{V}$); if a step's velocity meets or exceeds the threshold set by the knob, the note is active and triggers the Gate output at its velocity level.

**Global Gate Length**
Real-time continuous sweep. Modulates the duty cycle ($5\%$ to $95\%$) of the active Gate output pin.

**Triplet / Straight Knob**
Reads $0-100\%$ ($0 = \text{only triplets}$, $100 = \text{only straight}$). Controls the poly-metering crossfade between the parallel Straight and Triplet generation lanes.

**Pitch Attenuator**
Dual-axis hardware attenuation. Scales analog output voltages post-DAC. _Does not hit the microprocessor._

**`[ CLEAR ]` Button**

Tactile Switch

Instantly flushes array, zeros variables, resets display to `00`. Acts as a universal "Escape/Cancel" while display is flashing command prompts.

**`[ RECALL ]` Button**

Tactile Switch

DOES NOT INTERPUT OUTPUT; flashes `r-` on display. Awaits single dial input to parse Flash address load sequence - starts new sequences once the current phrase is done.

**`[ SAVE ]` Button**

Tactile Switch

DOES NOT INTERPUT OUTPUT; flashes `S-` on display. Awaits single dial input to serialize array and commit to Flash memory.

**`[ REST ]` Button**

Tactile Switch

DOES NOT INTERPUT OUTPUT; flashes `- -`. The next number dialed appends a block to the array marked `TYPE_REST` with a length equal to the dialed digit.

Potential Dual core implementation if necessary.
### Core 0: Human Interface & State Manager

-   Maintains the primary program execution state machine.
    
-   Decodes the mechanical dial inputs, button debounce routines, and handles persistent storage serialization to the onboard Flash memory block.
    
-   Directly outputs data strings to the external **TM1637 display driver IC** using a simple two-wire digital serial pipeline.
    

### Core 1: High-Speed Rhythmic & Audio Engine

Runs an ultra-low latency hardware timer interrupt locked to **$44.1\text{ kHz}$**.

-   **Time-Delta Measurement:** Continuously measures the exact microsecond interval between external clock pulses.
    
-   **Sequencer Mode ($>50\text{ms}$ intervals / $<20\text{Hz}$ LFO):** Advances the loop pointer step-by-step on every raw rising edge. Fires standard digital triggers and outputs discrete stepping voltages to the MCP4822 SPI DAC.
    
-   **Generator Mode ($<50\text{ms}$ intervals / $>20\text{Hz}$ Audio):** Dynamically builds a virtual 256-sample circular wavetable loop in RAM by executing linear or Hermite spline interpolations between the active sequence steps. Decouples from step-by-step clocking and uses the calculated time-delta frequency to run a virtual phase accumulator, driving the high-frequency PIO PDM audio pin.
    
-   **The Clock Shutter Mechanism:** Every arriving clock pulse forces an absolute phase reset (`phaseAccumulator = 0`) on the audio wave loop. This locks the fundamental pitch output perfectly to the external clock frequency, transforming the per-group **Symmetry** calculations into shifting, evolving waveshapes and raw timbral modulations.
    

## 5. Rhythmic Syncopation & Accent Engine

When evaluating internal accents within a playing group, the code bypasses raw random engines and runs the active step context through the following structural subdivision filter:

C++

```
// Hard-coded musical subdivision maps for odd-meter grounding
bool getTemplateAccents(uint8_t length, uint8_t step) {
    switch(length) {
        case 4:  return (step == 2);
        case 5:  return (step == 3);
        case 6:  return (step == 3);
        case 7:  return (step == 3 || step == 5);
        case 8:  return (step == 3 || step == 6);
        case 9:  return (step == 3 || step == 6);
        case 10: return (step == 3 || step == 6 || step == 8);
        default: return false; // Lengths 1, 2, 3 feature no internal sub-accents
    }
}

```
### The Mutation Logic Rule

On every step execution inside a block, the core reads the baked-in `group.symmetry` value:

1.  Generate a pseudo-random integer from $0$ to $99$.
    
2.  If the random number is **less than** the symmetry value, the accent pin state mirrors `getTemplateAccents()`. This locks in rigid, repeating mathematical grooves.
    
3.  If the random number is **greater than or equal to** the symmetry value, the accent state pulls directly from the lowest bit of a standard looping 16-bit Turing Machine shift register, introducing organic variations and controlled chaos.

## 6. Velocity Gating & Note Density Threshold Engine

Unlike traditional sequencers with fixed $+5\text{V}$ binary gate outputs, **Call a Friend** generates an inner velocity ($0\text{V}$ to $+5\text{V}$) for every active step during pattern generation.

> **Note:** The **Note Density** knob acts as a dynamic threshold comparator:
> $$\text{Threshold} = 5.0\text{V} \times \left(1 - \frac{\text{Density}}{100}\right)$$
> When a step's inner velocity is greater than or equal to this threshold, the note plays. At $100\%$, the threshold is $0\text{V}$ (all notes play); at $0\%$, the threshold is $5\text{V}$ (no notes play).

When a note is active, its velocity directly drives the amplitude of **Gate Out**:
- **Standard Notes**: Output gate voltage equals the step's inner velocity ($0\text{V}$ to $+5\text{V}$).
- **Accented Notes**: Applied with a $1.1\times$ multiplier ($V_{\text{gate}} = V_{\text{inner}} \times 1.1$), adding dynamic punch up to $+5.5\text{V}$ (clamped to $+10\text{V}$ max).

## 7. Tuplet Poly-metering & Parallel Lanes

To achieve polyrhythmic and polymetric structures without external clock dividers, **Call a Friend** runs two parallel generation playheads simultaneously over the stored sequence array:
1. **Straight Lane**: Clocked directly at the incoming $1/1$ step rate.
2. **Triplet Lane**: Clocked at a $3:2$ tuplet subdivision rate ($\text{Period}_{\text{triplet}} = \text{Period}_{\text{straight}} \times \frac{2}{3}$).

> **Note:** To prevent drift over long performances, the Triplet Lane executes an automatic phase-lock alignment against the Straight Lane every 2 straight clock cycles ($2\text{ beats} = 3\text{ triplet steps}$).

The **Triplet / Straight** knob continuously crossfades CV, Gate, and Trigger outputs between the two parallel lanes, allowing seamless morphing from pure triplet feels ($0\%$) to straight metric grids ($100\%$) or hybrid poly-metric blends.