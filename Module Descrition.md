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
5. **Gate Out ($0\text{V}$ to $+10\text{V}$ Velocity-Gating):** Dynamic voltage gate output whose peak amplitude represents note velocity ($0\text{V}$ to $+5\text{V}$ baseline, scaled up to $+10\text{V}$ max on accents, attenuated to $\sim 0.5\text{V}-1.5\text{V}$ on ghost notes). Governed by the **Global Gate Length Knob** and **Note Density** threshold. Drops to $0\text{V}$ on rests.
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

**Rest Bias Knob (Prototyping Phase)**
Reads $0-100\%$. Controls the chained probability of rests. When a step is of type rest, the next step has a higher chance to also be evaluated as a rest, biasing the likelihood of extended rests from $0\%$ (independent steps) to $100\%$ (strongly clustered pauses).

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
> When a step's inner velocity is greater than or equal to this threshold, the note plays as a standard or accented note.

### Ghost Notes (Option A: Soft-Knee Threshold Window)
To provide organic dynamics where notes softly fade before disappearing into complete rests, a **Soft-Knee Ghost Window** ($\Delta V = 1.0\text{V}$) is applied immediately below the current Note Density threshold:
- **Standard Notes** ($V_{\text{inner}} \ge \text{Threshold}$): Output gate voltage equals the step's inner velocity ($0\text{V}$ to $+5\text{V}$), with normal gate duty cycle ($5\% - 95\%$).
- **Accented Notes** ($V_{\text{inner}} \ge \text{Threshold}$ AND step is accented): Applied with a $1.1\times$ multiplier ($V_{\text{gate}} = V_{\text{inner}} \times 1.1$, clamped to $+10\text{V}$ max) and triggers the **Accent Out** $10\text{ms}$ pulse.
- **Ghost Notes** ($\text{Threshold} - 1.0\text{V} \le V_{\text{inner}} < \text{Threshold}$): Plays as an attenuated, snappy ghost note:
  - Gate amplitude is scaled down to $30\%$ of inner velocity ($V_{\text{gate}} = V_{\text{inner}} \times 0.3$, producing $\sim 0.3\text{V}-1.5\text{V}$).
  - Gate length is shortened to $50\%$ of the current Global Gate Length for crisp articulation.
  - **Accent Out** remains silent ($0\text{V}$).
- **Rests** ($V_{\text{inner}} < \text{Threshold} - 1.0\text{V}$ or explicit `TYPE_REST`): $V_{\text{gate}} = 0\text{V}$.

<!-- 
COMMENT / DESIGN ALTERNATIVE FOR PROTOTYPING:
Option B: Structural / Subdivision Ghost Note Placement
Instead of (or in combination with) threshold soft-knee, ghost notes could be intentionally placed based on musical position:
1. High Symmetry: Ghost notes are deterministically placed on weak off-beat subdivisions / pickup steps preceding an accent or group boundary.
2. Low Symmetry: The Turing Machine shift register evaluates a secondary bit condition to introduce evolving, syncopated ghost notes.
(Kept as an alternative prototype consideration to compare against Option A for musical meaningfulness).
-->

## 7. Tuplet Poly-metering & Parallel Lanes

To achieve polyrhythmic and polymetric structures without external clock dividers, **Call a Friend** runs two parallel generation playheads simultaneously over the stored sequence array:
1. **Straight Lane**: Clocked directly at the incoming $1/1$ step rate.
2. **Triplet Lane**: Clocked at a $3:2$ tuplet subdivision rate ($\text{Period}_{\text{triplet}} = \text{Period}_{\text{straight}} \times \frac{2}{3}$).

> **Note:** To prevent drift over long performances, the Triplet Lane executes an automatic phase-lock alignment against the Straight Lane every 2 straight clock cycles ($2\text{ beats} = 3\text{ triplet steps}$).

The **Triplet / Straight** knob continuously crossfades CV, Gate, and Trigger outputs between the two parallel lanes, allowing seamless morphing from pure triplet feels ($0\%$) to straight metric grids ($100\%$) or hybrid poly-metric blends.

## 8. Rest Bias & Chained Rest Probabilities (Prototyping Phase)

To facilitate natural phrasing and encourage longer, more musical pauses, step generation incorporates a Markov/chained rest bias mechanism:
- When any step is evaluated as a **rest** (either as an explicit `TYPE_REST` step or filtered out via the **Note Density** threshold), the subsequent step is biased toward remaining a rest.
- The **Rest Bias Knob** ($0-100\%$) scales this extra rest probability:
  - At **$0\%$ Bias**: Step rest evaluations remain completely independent.
  - At **$100\%$ Bias**: Following a rest, the next step has a maximum probability of continuing as a rest, creating extended contiguous silent blocks.

## 9. Future Suggestion: Preset / Group Chaining (Song Form & A/B Section Structures)

To elevate **Call a Friend** from a single-phrase loop generator into an expressive live song-form sequencer, a rapid **Preset & Group Chaining** mechanism is proposed:

### Concept Overview
Allow the user to quickly link saved preset slots (or distinct rhythmic sections) into macro song arrangements (e.g., standard verse/chorus or AABA structures):
* **Section A (e.g., Slot 1):** Additive groove like $4 + 2 + 2 + 4 + 2$ (14 steps)
* **Section B (e.g., Slot 2):** Contrasting fill or turnaround like $3 + 3$ (6 steps)
* **Chained Macro-Phrase:** Sequenced as **$\text{A} \rightarrow \text{A} \rightarrow \text{B} \rightarrow \text{A}$** before looping.

### Proposed Hardware Interaction Workflow
1. **Chain Input Mode:**
   - Double-tap or hold **`[ RECALL ]`** (e.g., display flashes `C-` for Chain Mode).
   - Dial a sequence of preset slot digits in order (e.g., dialing `1`, then `1`, then `2`, then `1`).
   - Press **`[ RECALL ]`** or allow timeout to confirm.
2. **Execution & Transition Logic:**
   - Playback transitions seamlessly from one slot to the next upon completion of each section's phrase (on the phrase boundary / downbeat).
   - The 2-digit display dynamically reflects the active playback block (e.g., `A1` $\rightarrow$ `A2` $\rightarrow$ `b1` $\rightarrow$ `A1`).
   - **Phrase Start Out** can fire on every individual section reset or at the complete macro-chain loop origin.