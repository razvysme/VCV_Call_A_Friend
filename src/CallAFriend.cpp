#include "plugin.hpp"
#include "RotaryDialWidget.hpp"
#include "SegmentDisplayWidget.hpp"
#include "TelephonePadButton.hpp"
#include <vector>
#include <string>
#include <cstdlib>
#include <cmath>

using namespace rack;

struct RhythmicGroup {
	uint8_t length = 4;
	bool isRest = false;
	float symmetry = 50.f;
	std::vector<float> stepCVs;
	std::vector<bool> stepAccents;
	std::vector<float> stepVelocities;
};

struct WhiteBlueLEDButton : ParamWidget {
	int lightId = -1;
	WhiteBlueLEDButton() {
		box.size = mm2px(Vec(11.6f, 11.6f));
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (getParamQuantity()) getParamQuantity()->setValue(getParamQuantity()->getMaxValue());
			e.consume(this);
		} else if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (getParamQuantity()) getParamQuantity()->setValue(getParamQuantity()->getMinValue());
			e.consume(this);
		}
	}
	void draw(const DrawArgs& args) override {
		float radius = box.size.x / 2.0f;
		
		// Button body
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, radius, radius, radius);
		nvgFillColor(args.vg, nvgRGB(240, 240, 240));
		nvgFill(args.vg);
		
		// Border
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStrokeColor(args.vg, nvgRGB(200, 200, 200));
		nvgStroke(args.vg);

		// LED
		if (module && lightId >= 0) {
			float brightness = module->lights[lightId].getBrightness();
			if (brightness > 0.0f) {
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, radius, radius, radius * 0.6f);
				NVGcolor color = nvgRGB(0, 100, 255); // Blue
				color.a = brightness;
				nvgFillColor(args.vg, color);
				nvgFill(args.vg);

				// Glow
				NVGpaint paint = nvgRadialGradient(args.vg, radius, radius, radius*0.2f, radius, nvgRGBA(0, 100, 255, 100 * brightness), nvgRGBA(0, 100, 255, 0));
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, radius, radius, radius);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
			}
		}

		// When pressed, make it slightly darker
		if (getParamQuantity() && getParamQuantity()->getValue() > 0.5f) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius, radius, radius);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 30));
			nvgFill(args.vg);
		}
	}
};

struct CallAFriend : Module {
	enum ParamId {
		// Knobs
		SYMMETRY_PARAM,
		NOTE_DENSITY_PARAM,
		GATE_LENGTH_PARAM,
		PITCH_ATTEN_PARAM,
		SLEW_PARAM,
		
		// Top Buttons
		CLEAR_PARAM,
		SAVE_PARAM,
		RECALL_PARAM,
		LINK_PARAM,
		REST_PARAM,
		
		// Telephone Pad Buttons (0-9)
		DIGIT_0_PARAM,
		DIGIT_1_PARAM,
		DIGIT_2_PARAM,
		DIGIT_3_PARAM,
		DIGIT_4_PARAM,
		DIGIT_5_PARAM,
		DIGIT_6_PARAM,
		DIGIT_7_PARAM,
		DIGIT_8_PARAM,
		DIGIT_9_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		SYMMETRY_INPUT,
		DENSITY_INPUT,
		GATE_LENGTH_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PHRASE_START_OUTPUT,
		BAR_START_OUTPUT,
		GROUP_START_OUTPUT,
		ACCENT_OUTPUT,
		GATE_OUTPUT,
		UNIPOLAR_CV_OUTPUT,
		BIPOLAR_CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		CLEAR_LIGHT,
		SAVE_LIGHT,
		RECALL_LIGHT,
		LINK_LIGHT,
		REST_LIGHT,
		LIGHTS_LEN
	};

	// Sequence State
	std::vector<RhythmicGroup> groups;
	std::vector<RhythmicGroup> saveSlots[10];

	struct PlayheadState {
		int currentGroupIdx = 0;
		int currentStepInGroup = 0;
		bool stepJustFired = false;
		float currentSlewCV = 0.f;

		int totalStepCounter = 0;
		int barClockCounter = 0;
		float gateTimeRemaining = 0.f;
		float currentGateVoltage = 0.f;
		float currentCV = 0.f;
		bool isAccent = false;
		bool isNoteActive = false;

		dsp::PulseGenerator phrasePulse;
		dsp::PulseGenerator barPulse;
		dsp::PulseGenerator groupPulse;
		dsp::PulseGenerator accentPulse;

		void reset() {
			currentGroupIdx = 0;
			currentStepInGroup = 0;
			stepJustFired = false;
			currentSlewCV = 0.f;
			totalStepCounter = 0;
			barClockCounter = 0;
			gateTimeRemaining = 0.f;
			currentGateVoltage = 0.f;
			currentCV = 0.f;
			isAccent = false;
			isNoteActive = false;
		}
	} playhead;

	// Mode State
	enum InputMode {
		MODE_NORMAL,
		MODE_RECALL,
		MODE_SAVE,
		MODE_REST,
		MODE_LINK_EDIT
	} mode = MODE_NORMAL;

	// Tuning Variables
	float buttonBlinkRateMs = 500.f;        // Regular mode blink period
	float buttonConfirmBlinkRateMs = 150.f; // Confirm effect blink period (2 blinks)
	float displayOffTimeMs = 150.f;         // Display off time when a number is entered

	std::vector<int> patternChain;
	bool linkModeActive = false;
	int currentChainIdx = -1;
	
	// Confirm effect timers
	float saveConfirmTimer = 0.f;
	float recallConfirmTimer = 0.f;
	float linkConfirmTimer = 0.f;
	float restConfirmTimer = 0.f;

	// Display state
	std::string displayStr = "00";
	std::string actualDisplayStr = "00";
	float displayOffTimer = 0.f;

	// Clock Input Schmitt Triggers & Button Triggers
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::SchmittTrigger saveTrigger;
	dsp::SchmittTrigger recallTrigger;
	dsp::SchmittTrigger linkTrigger;
	dsp::SchmittTrigger restTrigger;
	dsp::SchmittTrigger digitTriggers[10];

	// Wobbler Clock Synchronization State
	int timesincesync = 0;
	int syncindex = 0;
	int synctime[3] = {0, 0, 0};
	uint32_t SyncedPeriodTime = 0;
	uint32_t SyncDP = 0;
	bool extsync = false;

	// Turing Machine 16-bit shift register
	uint16_t turingRegister = 0xACE1;

	// Audio Generator Phase Accumulator (for Generator Mode)
	uint32_t phaseAccumulator = 0;

	CallAFriend() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CallAFriend::CLEAR_PARAM, 0.f, 1.f, 0.f, "Clear");
		configParam(CallAFriend::SAVE_PARAM, 0.f, 1.f, 0.f, "Save");
		configParam(CallAFriend::RECALL_PARAM, 0.f, 1.f, 0.f, "Recall");
		configParam(CallAFriend::LINK_PARAM, 0.f, 1.f, 0.f, "Link");
		configParam(CallAFriend::REST_PARAM, 0.f, 1.f, 0.f, "Rest");
		configParam(CallAFriend::SYMMETRY_PARAM, 0.f, 100.f, 50.f, "Symmetry", "%");
		configParam(CallAFriend::NOTE_DENSITY_PARAM, 0.f, 100.f, 100.f, "Note Density", "%");
		configParam(CallAFriend::GATE_LENGTH_PARAM, 10.f, 90.f, 50.f, "Gate Length", "%");
		configParam(CallAFriend::PITCH_ATTEN_PARAM, 0.f, 1.f, 1.f, "Pitch Attenuation");
		configParam(CallAFriend::SLEW_PARAM, 0.f, 1.f, 0.f, "Slew");

		for (int dig = 0; dig <= 9; dig++) {
			configParam(DIGIT_0_PARAM + dig, 0.f, 1.f, 0.f, "Digit " + std::to_string(dig));
		}

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(SYMMETRY_INPUT, "Symmetry CV");
		configInput(DENSITY_INPUT, "Density CV");
		configInput(GATE_LENGTH_INPUT, "Gate Length CV");

		configOutput(PHRASE_START_OUTPUT, "Phrase Start");
		configOutput(BAR_START_OUTPUT, "Bar Start");
		configOutput(GROUP_START_OUTPUT, "Group Start");
		configOutput(ACCENT_OUTPUT, "Accent");
		configOutput(GATE_OUTPUT, "Gate");
		configOutput(UNIPOLAR_CV_OUTPUT, "Unipolar CV");
		configOutput(BIPOLAR_CV_OUTPUT, "Bipolar CV");

		// Initialize default sequence
		RhythmicGroup defaultGroup;
		defaultGroup.length = 4;
		defaultGroup.isRest = false;
		defaultGroup.symmetry = 50.f;
		generateGroupPattern(defaultGroup);
		groups.push_back(defaultGroup);
		updateDisplay();
	}

	// Hard-coded musical subdivision maps for odd-meter grounding
	bool getTemplateAccents(uint8_t length, uint8_t step) {
		switch (length) {
			case 4:  return (step == 2);
			case 5:  return (step == 3);
			case 6:  return (step == 3);
			case 7:  return (step == 3 || step == 5);
			case 8:  return (step == 3 || step == 6);
			case 9:  return (step == 3 || step == 6);
			case 10: return (step == 3 || step == 6 || step == 8);
			default: return false;
		}
	}

	void generateGroupPattern(RhythmicGroup& g) {
		g.stepCVs.resize(g.length);
		g.stepAccents.resize(g.length);
		g.stepVelocities.resize(g.length);

		const int pentatonicScale[5] = {0, 2, 4, 7, 9};
		for (int s = 0; s < g.length; s++) {
			if (g.isRest) {
				g.stepCVs[s] = 0.f;
				g.stepAccents[s] = false;
				g.stepVelocities[s] = 0.f;
			} else {
				int octave = std::rand() % 2;
				int noteIdx = std::rand() % 5;
				int semitone = octave * 12 + pentatonicScale[noteIdx];
				g.stepCVs[s] = (float)semitone / 12.0f; // 1V/Octave

				int randVal = std::rand() % 100;
				if (randVal < (int)g.symmetry) {
					g.stepAccents[s] = getTemplateAccents(g.length, s);
				} else {
					g.stepAccents[s] = (std::rand() & 1) != 0;
				}

				// Inner velocity: 0V to 5V
				g.stepVelocities[s] = ((float)std::rand() / (float)RAND_MAX) * 5.0f;
			}
		}
	}

	int getTotalSteps() const {
		int total = 0;
		for (const auto& g : groups) {
			total += g.length;
		}
		return total;
	}

	void updateDisplay() {
		if (mode == MODE_RECALL) {
			actualDisplayStr = "r-";
		} else if (mode == MODE_SAVE) {
			actualDisplayStr = "S-";
		} else if (mode == MODE_REST) {
			actualDisplayStr = "--";
		} else if (mode == MODE_LINK_EDIT) {
			actualDisplayStr = "L-";
		} else {
			int total = getTotalSteps();
			char buf[8];
			snprintf(buf, sizeof(buf), "%02d", total % 100);
			actualDisplayStr = buf;
		}
		if (displayOffTimer <= 0.f) {
			displayStr = actualDisplayStr;
		}
	}

	void onDialDigit(int digit) {
		int dialed = (digit == 0) ? 10 : digit;
		
		displayOffTimer = displayOffTimeMs / 1000.f; // Blink the display

		if (mode == MODE_RECALL) {
			int slot = (digit == 10) ? 0 : digit;
			if (slot >= 0 && slot < 10 && !saveSlots[slot].empty()) {
				groups = saveSlots[slot];
				linkModeActive = false; // Override link mode
			}
			mode = MODE_NORMAL;
			recallConfirmTimer = 2.0f * (buttonConfirmBlinkRateMs / 1000.f);
		} else if (mode == MODE_SAVE) {
			int slot = (digit == 10) ? 0 : digit;
			if (slot >= 0 && slot < 10) {
				saveSlots[slot] = groups;
			}
			mode = MODE_NORMAL;
			saveConfirmTimer = 2.0f * (buttonConfirmBlinkRateMs / 1000.f);
		} else if (mode == MODE_REST) {
			RhythmicGroup g;
			g.length = dialed;
			g.isRest = true;
			g.symmetry = params[SYMMETRY_PARAM].getValue();
			generateGroupPattern(g);
			groups.push_back(g);
			mode = MODE_NORMAL;
			restConfirmTimer = 2.0f * (buttonConfirmBlinkRateMs / 1000.f);
		} else if (mode == MODE_LINK_EDIT) {
			int slot = (digit == 10) ? 0 : digit;
			patternChain.push_back(slot);
			char buf[8];
			snprintf(buf, sizeof(buf), "%d-", slot);
			actualDisplayStr = buf;
			if (displayOffTimer <= 0.f) displayStr = actualDisplayStr;
			return; // Don't fall through to updateDisplay()
		} else {
			RhythmicGroup g;
			g.length = dialed;
			g.isRest = false;
			g.symmetry = params[SYMMETRY_PARAM].getValue();
			
			bool copied = false;
			for (const auto& existing : groups) {
				if (!existing.isRest && existing.length == dialed) {
					g.stepCVs = existing.stepCVs;
					g.stepAccents = existing.stepAccents;
					g.stepVelocities = existing.stepVelocities;
					copied = true;
					break;
				}
			}
			if (!copied) {
				generateGroupPattern(g);
			}
			groups.push_back(g);
		}

		updateDisplay();
	}

	// Ported Wobbler2_SyncPulse
	void updateWobblerClockSync() {
		int Delta = timesincesync;
		timesincesync = 0;
		if (Delta < 10) return;

		// Timeout threshold ~ 2 seconds at current sample rate
		int timeoutSamples = (int)(APP->engine->getSampleRate() * 2.0f);
		if (Delta > timeoutSamples) {
			syncindex = 0;
		} else {
			syncindex++;
		}

		if (syncindex > 2) {
			synctime[0] = synctime[1];
			synctime[1] = synctime[2];
			syncindex = 2;

			SyncedPeriodTime = (synctime[0] + synctime[1] + synctime[2]) / 3;
			if (SyncedPeriodTime > 0) {
				SyncDP = 0xffffffff / SyncedPeriodTime;
				extsync = true;
			}
		}
		synctime[syncindex] = Delta;
	}

	void advanceStep(float sampleTime, float periodSec) {
		if (groups.empty()) return;

		playhead.currentStepInGroup++;
		if (playhead.currentStepInGroup >= groups[playhead.currentGroupIdx].length) {
			playhead.currentStepInGroup = 0;
			playhead.currentGroupIdx++;
			if (playhead.currentGroupIdx >= (int)groups.size()) {
				playhead.currentGroupIdx = 0;
				if (linkModeActive && !patternChain.empty()) {
					currentChainIdx = (currentChainIdx + 1) % (int)patternChain.size();
					int slot = patternChain[currentChainIdx];
					if (!saveSlots[slot].empty()) {
						groups = saveSlots[slot];
					}
				}
			}
		}

		playhead.totalStepCounter++;
		playhead.barClockCounter++;

		// 1. Phrase Start Out: fired at Group 0, Step 0
		if (playhead.currentGroupIdx == 0 && playhead.currentStepInGroup == 0) {
			playhead.phrasePulse.trigger(0.010f);
		}

		// 2. Bar Start Out: fired every 4 clock pulses
		if (playhead.barClockCounter % 4 == 1 || playhead.barClockCounter == 1) {
			playhead.barPulse.trigger(0.010f);
		}

		// 3. Group Start Out: fired on Step 0 of any active RhythmicGroup
		if (playhead.currentStepInGroup == 0) {
			playhead.groupPulse.trigger(0.010f);
		}

		// 4. Accent & Drift Out: Symmetry/Accent Hierarchy Engine & Turing Machine Mutation
		playhead.isAccent = false;
		RhythmicGroup& curGroup = groups[playhead.currentGroupIdx];
		float sym = params[SYMMETRY_PARAM].getValue();
		if (inputs[SYMMETRY_INPUT].isConnected()) {
			sym += inputs[SYMMETRY_INPUT].getVoltage() * 10.0f;
		}
		sym = clamp(sym, 0.f, 100.f);

		int randVal = std::rand() % 100;
		if (randVal < (int)sym) {
			playhead.isAccent = getTemplateAccents(curGroup.length, playhead.currentStepInGroup);
		} else {
			playhead.isAccent = (turingRegister & 1) != 0;
			// Advance Turing Machine
			bool newBit = ((turingRegister & 1) ^ ((turingRegister >> 1) & 1));
			turingRegister = (turingRegister >> 1) | (newBit ? 0x8000 : 0);

			// Organic Drifting: mutate pitch CV slightly when symmetry < 100%
			if (playhead.currentStepInGroup < (int)curGroup.stepCVs.size()) {
				int driftSemitones = ((turingRegister & 0x7) - 3); // -3..+4 semitones drift
				float newCV = curGroup.stepCVs[playhead.currentStepInGroup] + driftSemitones / 12.0f;
				if (newCV < 0.f) newCV += 1.0f;
				if (newCV > 2.0f) newCV -= 1.0f;
				curGroup.stepCVs[playhead.currentStepInGroup] = newCV;

				// Sync drift to all other groups of the same length (non-rests)
				for (auto& otherGroup : groups) {
					if (!otherGroup.isRest && otherGroup.length == curGroup.length) {
						otherGroup.stepCVs[playhead.currentStepInGroup] = newCV;
					}
				}
			}
		}

		// 5. Note Density threshold check & Velocity Gate
		playhead.isNoteActive = false;
		float stepVel = 0.f;
		if (!curGroup.isRest && playhead.currentStepInGroup < (int)curGroup.stepVelocities.size()) {
			stepVel = curGroup.stepVelocities[playhead.currentStepInGroup];
			float density = params[NOTE_DENSITY_PARAM].getValue();
			if (inputs[DENSITY_INPUT].isConnected()) {
				density += inputs[DENSITY_INPUT].getVoltage() * 10.0f;
			}
			density = clamp(density, 0.f, 100.f);

			if (density > 0.f) {
				if (density >= 100.f) {
					playhead.isNoteActive = true;
				} else {
					float threshold = 5.0f * (1.0f - density / 100.f);
					playhead.isNoteActive = (stepVel >= threshold);
				}
			}
		}

		if (playhead.isNoteActive) {
			float gateLenPercent = params[GATE_LENGTH_PARAM].getValue();
			if (inputs[GATE_LENGTH_INPUT].isConnected()) {
				// Modulate gate length by CV (0-10V sweeps 0-100%, summed with knob)
				gateLenPercent += inputs[GATE_LENGTH_INPUT].getVoltage() * 10.0f;
			}
			gateLenPercent = clamp(gateLenPercent, 5.0f, 95.0f);
			float duty = gateLenPercent / 100.f;
			playhead.gateTimeRemaining = periodSec * duty;

			float mult = playhead.isAccent ? 1.1f : 1.0f;
			playhead.currentGateVoltage = std::min(stepVel * mult, 10.f);

			if (playhead.currentStepInGroup < (int)curGroup.stepCVs.size()) {
				playhead.currentCV = curGroup.stepCVs[playhead.currentStepInGroup];
			}
		} else {
			playhead.gateTimeRemaining = 0.f;
			playhead.currentGateVoltage = 0.f;
		}

		if (playhead.isAccent && playhead.isNoteActive) {
			playhead.accentPulse.trigger(0.010f);
		}
	}

	void process(const ProcessArgs& args) override {
		timesincesync++;

		// Update display off timer
		if (displayOffTimer > 0.f) {
			displayOffTimer -= args.sampleTime;
			displayStr = "  "; // Use spaces so the SegmentDisplayWidget renders it completely dark
		} else {
			displayStr = actualDisplayStr;
		}

		// Button Handlers
		if (clearTrigger.process(params[CLEAR_PARAM].getValue())) {
			if (mode != MODE_NORMAL) {
				// Acts as universal Escape/Cancel when display is flashing command prompts
				mode = MODE_NORMAL;
				updateDisplay();
			} else {
				// Instantly flushes array, zeros variables, resets display to 00
				groups.clear();
				playhead.reset();
				linkModeActive = false;
				mode = MODE_NORMAL;
				updateDisplay();
			}
		}
		if (saveTrigger.process(params[SAVE_PARAM].getValue())) {
			mode = (mode == MODE_SAVE) ? MODE_NORMAL : MODE_SAVE;
			updateDisplay();
		}
		if (recallTrigger.process(params[RECALL_PARAM].getValue())) {
			mode = (mode == MODE_RECALL) ? MODE_NORMAL : MODE_RECALL;
			updateDisplay();
		}
		if (linkTrigger.process(params[LINK_PARAM].getValue())) {
			if (mode == MODE_LINK_EDIT) {
				mode = MODE_NORMAL;
				if (!patternChain.empty()) {
					linkModeActive = true;
					currentChainIdx = -1; // Starts on the next phrase wrap-around
					linkConfirmTimer = 2.0f * (buttonConfirmBlinkRateMs / 1000.f);
				}
				updateDisplay();
			} else {
				mode = MODE_LINK_EDIT;
				linkModeActive = false;
				patternChain.clear();
				displayStr = "L-";
				actualDisplayStr = "L-";
			}
		}
		if (restTrigger.process(params[REST_PARAM].getValue())) {
			mode = (mode == MODE_REST) ? MODE_NORMAL : MODE_REST;
			updateDisplay();
		}
		for (int dig = 0; dig <= 9; dig++) {
			if (digitTriggers[dig].process(params[DIGIT_0_PARAM + dig].getValue())) {
				onDialDigit(dig);
			}
		}

		// Light Blinking Logic
		float blinkPeriod = buttonBlinkRateMs / 1000.f;
		static float blinkPhase = 0.f;
		blinkPhase += args.sampleTime / blinkPeriod;
		if (blinkPhase >= 1.0f) blinkPhase -= 1.0f;
		bool blinkState = blinkPhase < 0.5f;

		float confirmPeriod = buttonConfirmBlinkRateMs / 1000.f;

		// LINK Light
		if (linkConfirmTimer > 0.f) {
			linkConfirmTimer -= args.sampleTime;
			float fastPhase = std::fmod(linkConfirmTimer / confirmPeriod, 1.0f);
			lights[LINK_LIGHT].setBrightness((fastPhase > 0.5f) ? 1.f : 0.f);
		} else {
			lights[LINK_LIGHT].setBrightness((mode == MODE_LINK_EDIT) ? (blinkState ? 1.f : 0.f) : 0.f);
		}

		// REST Light
		if (restConfirmTimer > 0.f) {
			restConfirmTimer -= args.sampleTime;
			float fastPhase = std::fmod(restConfirmTimer / confirmPeriod, 1.0f);
			lights[REST_LIGHT].setBrightness((fastPhase > 0.5f) ? 1.f : 0.f);
		} else {
			lights[REST_LIGHT].setBrightness((mode == MODE_REST) ? (blinkState ? 1.f : 0.f) : 0.f);
		}

		// RECALL Light
		if (recallConfirmTimer > 0.f) {
			recallConfirmTimer -= args.sampleTime;
			float fastPhase = std::fmod(recallConfirmTimer / confirmPeriod, 1.0f);
			lights[RECALL_LIGHT].setBrightness((fastPhase > 0.5f) ? 1.f : 0.f);
		} else {
			lights[RECALL_LIGHT].setBrightness((mode == MODE_RECALL) ? (blinkState ? 1.f : 0.f) : 0.f);
		}

		// CLEAR Light
		lights[CLEAR_LIGHT].setBrightness(params[CLEAR_PARAM].getValue() > 0.5f ? 1.f : 0.f);

		// SAVE Light
		if (saveConfirmTimer > 0.f) {
			saveConfirmTimer -= args.sampleTime;
			float fastPhase = std::fmod(saveConfirmTimer / confirmPeriod, 1.0f);
			lights[SAVE_LIGHT].setBrightness((fastPhase > 0.5f) ? 1.f : 0.f);
		} else {
			lights[SAVE_LIGHT].setBrightness((mode == MODE_SAVE) ? (blinkState ? 1.f : 0.f) : 0.f);
		}

		// Check Reset Input
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
			playhead.reset();
			if (linkModeActive && !patternChain.empty()) {
				currentChainIdx = 0;
				int slot = patternChain[0];
				if (!saveSlots[slot].empty()) {
					groups = saveSlots[slot];
				}
			}
			playhead.phrasePulse.trigger(0.010f);
			playhead.groupPulse.trigger(0.010f);
		}

		// Check Clock Input
		bool clockRisen = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
		if (clockRisen) {
			updateWobblerClockSync();

			// Clock Shutter Mechanism: force absolute phase reset on audio wave loop
			phaseAccumulator = 0;

			float periodSec = (extsync && SyncedPeriodTime > 0) ? ((float)SyncedPeriodTime * args.sampleTime) : 0.25f;
			advanceStep(args.sampleTime, periodSec);
		}

		// Timeout check for extsync
		int timeoutSamples = (int)(args.sampleRate * 2.0f);
		if (timesincesync > timeoutSamples || syncindex == 0) {
			extsync = false;
		}

		// Update gate durations
		if (playhead.gateTimeRemaining > 0.f) {
			playhead.gateTimeRemaining -= args.sampleTime;
		} else {
			playhead.currentGateVoltage = 0.f;
		}

		// Determine if we are in Generator Mode (< 50ms period / > 20 Hz clock)
		bool generatorMode = (extsync && SyncedPeriodTime < (args.sampleRate * 0.05f));

		float atten = params[PITCH_ATTEN_PARAM].getValue();

		if (generatorMode) {
			// GENERATOR MODE: Audio-rate wavetable synthesis from stored stepCVs
			phaseAccumulator += SyncDP;
			float normalizedPhase = (float)phaseAccumulator / 4294967296.0f; // 0..1

			std::vector<float> allCVs;
			for (const auto& g : groups) {
				allCVs.insert(allCVs.end(), g.stepCVs.begin(), g.stepCVs.end());
			}
			if (allCVs.empty()) allCVs.push_back(0.f);

			int totalSteps = (int)allCVs.size();
			float exactStep = normalizedPhase * totalSteps;
			int step0 = (int)exactStep % totalSteps;
			int step1 = (step0 + 1) % totalSteps;
			float frac = exactStep - (int)exactStep;

			float val0 = allCVs[step0];
			float val1 = allCVs[step1];
			float interpVal = val0 + (val1 - val0) * frac;

			outputs[UNIPOLAR_CV_OUTPUT].setVoltage(interpVal * 5.f * atten);
			outputs[BIPOLAR_CV_OUTPUT].setVoltage((interpVal - 1.f) * 5.f * atten);
			outputs[GATE_OUTPUT].setVoltage(5.f);
		} else {
			// SEQUENCER MODE
			float unipolar = playhead.currentCV * 5.f * atten;
			float bipolar = (playhead.currentCV - 1.f) * 5.f * atten;

			outputs[UNIPOLAR_CV_OUTPUT].setVoltage(unipolar);
			outputs[BIPOLAR_CV_OUTPUT].setVoltage(bipolar);
			outputs[GATE_OUTPUT].setVoltage(playhead.currentGateVoltage);
		}

		// Process Trigger Pulses (10V active for 10ms)
		outputs[PHRASE_START_OUTPUT].setVoltage(playhead.phrasePulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[BAR_START_OUTPUT].setVoltage(playhead.barPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[GROUP_START_OUTPUT].setVoltage(playhead.groupPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ACCENT_OUTPUT].setVoltage(playhead.accentPulse.process(args.sampleTime) ? 10.f : 0.f);
	}

	json_t* groupToJson(const RhythmicGroup& g) {
		json_t* gJ = json_object();
		json_object_set_new(gJ, "length", json_integer(g.length));
		json_object_set_new(gJ, "isRest", json_boolean(g.isRest));
		json_object_set_new(gJ, "symmetry", json_real(g.symmetry));

		json_t* cvJ = json_array();
		for (float cv : g.stepCVs) {
			json_array_append_new(cvJ, json_real(cv));
		}
		json_object_set_new(gJ, "stepCVs", cvJ);

		json_t* accJ = json_array();
		for (bool acc : g.stepAccents) {
			json_array_append_new(accJ, json_boolean(acc));
		}
		json_object_set_new(gJ, "stepAccents", accJ);

		json_t* velJ = json_array();
		for (float vel : g.stepVelocities) {
			json_array_append_new(velJ, json_real(vel));
		}
		json_object_set_new(gJ, "stepVelocities", velJ);
		return gJ;
	}

	RhythmicGroup groupFromJson(json_t* gJ) {
		RhythmicGroup g;
		json_t* lenJ = json_object_get(gJ, "length");
		if (lenJ) g.length = json_integer_value(lenJ);
		json_t* restJ = json_object_get(gJ, "isRest");
		if (restJ) g.isRest = json_is_true(restJ);
		json_t* symJ = json_object_get(gJ, "symmetry");
		if (symJ) g.symmetry = json_number_value(symJ);

		json_t* cvJ = json_object_get(gJ, "stepCVs");
		if (cvJ && json_is_array(cvJ)) {
			size_t idx;
			json_t* vJ;
			json_array_foreach(cvJ, idx, vJ) {
				g.stepCVs.push_back(json_number_value(vJ));
			}
		}
		json_t* accJ = json_object_get(gJ, "stepAccents");
		if (accJ && json_is_array(accJ)) {
			size_t idx;
			json_t* aJ;
			json_array_foreach(accJ, idx, aJ) {
				g.stepAccents.push_back(json_is_true(aJ));
			}
		}
		json_t* velJ = json_object_get(gJ, "stepVelocities");
		if (velJ && json_is_array(velJ)) {
			size_t idx;
			json_t* vJ;
			json_array_foreach(velJ, idx, vJ) {
				g.stepVelocities.push_back(json_number_value(vJ));
			}
		}
		if ((int)g.stepCVs.size() != g.length || (int)g.stepAccents.size() != g.length) {
			generateGroupPattern(g);
		} else if ((int)g.stepVelocities.size() != g.length) {
			g.stepVelocities.resize(g.length);
			for (int s = 0; s < g.length; s++) {
				g.stepVelocities[s] = g.isRest ? 0.f : (((float)std::rand() / (float)RAND_MAX) * 5.0f);
			}
		}
		return g;
	}

	// Persistent JSON Serialization
	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		// Save current sequence
		json_t* groupsJ = json_array();
		for (const auto& g : groups) {
			json_array_append_new(groupsJ, groupToJson(g));
		}
		json_object_set_new(rootJ, "groups", groupsJ);

		// Save 10 slots
		json_t* slotsJ = json_array();
		for (int s = 0; s < 10; s++) {
			json_t* slotArrJ = json_array();
			for (const auto& g : saveSlots[s]) {
				json_array_append_new(slotArrJ, groupToJson(g));
			}
			json_array_append_new(slotsJ, slotArrJ);
		}
		json_object_set_new(rootJ, "saveSlots", slotsJ);

		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* groupsJ = json_object_get(rootJ, "groups");
		if (groupsJ) {
			groups.clear();
			size_t i;
			json_t* gJ;
			json_array_foreach(groupsJ, i, gJ) {
				groups.push_back(groupFromJson(gJ));
			}
		}

		json_t* slotsJ = json_object_get(rootJ, "saveSlots");
		if (slotsJ) {
			size_t s;
			json_t* slotArrJ;
			json_array_foreach(slotsJ, s, slotArrJ) {
				if (s >= 10) break;
				saveSlots[s].clear();
				size_t i;
				json_t* gJ;
				json_array_foreach(slotArrJ, i, gJ) {
					saveSlots[s].push_back(groupFromJson(gJ));
				}
			}
		}

		updateDisplay();
	}
};

struct CallAFriendWidget : ModuleWidget {
	CallAFriendWidget(CallAFriend* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/CallAFriend.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// 1. Top Buttons (x = 12.0, 31.4, 50.8, 70.2, 89.6 mm; y = 16 mm)
		auto addButton = [&](Vec centerMm, int paramId, int lightId) {
			WhiteBlueLEDButton* btn = createParamCentered<WhiteBlueLEDButton>(mm2px(centerMm), module, paramId);
			btn->lightId = lightId;
			addParam(btn);
		};
		addButton(Vec(12.0f, 16.f), CallAFriend::CLEAR_PARAM, CallAFriend::CLEAR_LIGHT);
		addButton(Vec(31.4f, 16.f), CallAFriend::SAVE_PARAM, CallAFriend::SAVE_LIGHT);
		addButton(Vec(50.8f, 16.f), CallAFriend::RECALL_PARAM, CallAFriend::RECALL_LIGHT);
		addButton(Vec(70.2f, 16.f), CallAFriend::LINK_PARAM, CallAFriend::LINK_LIGHT);
		addButton(Vec(89.6f, 16.f), CallAFriend::REST_PARAM, CallAFriend::REST_LIGHT);

		// 2. Old Telephone Keypad Buttons (Row 1: 1,2,3; Row 2: 4,5,6; Row 3: 7,8,9; Row 4: 0 at bottom center)
		auto addPadKey = [&](Vec centerMm, int digId, const std::string& lbl) {
			TelephonePadButton* btn = createParamCentered<TelephonePadButton>(mm2px(centerMm), module, CallAFriend::DIGIT_0_PARAM + digId);
			btn->label = lbl;
			addParam(btn);
		};
		addPadKey(Vec(36.3f, 31.0f), 1, "1");
		addPadKey(Vec(50.8f, 31.0f), 2, "2");
		addPadKey(Vec(65.3f, 31.0f), 3, "3");

		addPadKey(Vec(36.3f, 42.0f), 4, "4");
		addPadKey(Vec(50.8f, 42.0f), 5, "5");
		addPadKey(Vec(65.3f, 42.0f), 6, "6");

		addPadKey(Vec(36.3f, 53.0f), 7, "7");
		addPadKey(Vec(50.8f, 53.0f), 8, "8");
		addPadKey(Vec(65.3f, 53.0f), 9, "9");

		addPadKey(Vec(50.8f, 64.0f), 0, "0");

		// 3. Knobs below dial (y = 78 mm; x = 12.0, 31.4, 50.8, 70.2, 89.6 mm)
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.0f, 78.f)), module, CallAFriend::SYMMETRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(31.4f, 78.f)), module, CallAFriend::NOTE_DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8f, 78.f)), module, CallAFriend::GATE_LENGTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(70.2f, 78.f)), module, CallAFriend::PITCH_ATTEN_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(89.6f, 78.f)), module, CallAFriend::SLEW_PARAM));

		// 4. 2-Digit 7-Segment Display (x = 2.5 mm, y = 92 mm)
		SegmentDisplayWidget* disp = createWidget<SegmentDisplayWidget>(mm2px(Vec(2.5f, 92.f)));
		if (module) {
			disp->displayStr = &module->displayStr;
		}
		addChild(disp);

		// 6. Bottom Right 4x3 I/O Jack Grid (Row x = 45.0, 59.0, 73.0, 87.0 mm; Row y = 96.0, 107.0, 118.0 mm)
		// Row 1
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(45.0f, 96.f)), module, CallAFriend::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(59.0f, 96.f)), module, CallAFriend::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(73.0f, 96.f)), module, CallAFriend::SYMMETRY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(87.0f, 96.f)), module, CallAFriend::DENSITY_INPUT));

		// Row 2
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(45.0f, 107.f)), module, CallAFriend::GATE_LENGTH_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(59.0f, 107.f)), module, CallAFriend::PHRASE_START_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(73.0f, 107.f)), module, CallAFriend::BAR_START_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(87.0f, 107.f)), module, CallAFriend::GROUP_START_OUTPUT));

		// Row 3
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(45.0f, 118.f)), module, CallAFriend::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(59.0f, 118.f)), module, CallAFriend::ACCENT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(73.0f, 118.f)), module, CallAFriend::UNIPOLAR_CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(87.0f, 118.f)), module, CallAFriend::BIPOLAR_CV_OUTPUT));
	}
};

Model* modelCallAFriend = createModel<CallAFriend, CallAFriendWidget>("CallAFriend");
