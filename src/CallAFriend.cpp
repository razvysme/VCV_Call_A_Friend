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

struct CallAFriend : Module {
	enum ParamId {
		SYMMETRY_PARAM,
		NOTE_DENSITY_PARAM,
		GATE_LENGTH_PARAM,
		TRIPLET_STRAIGHT_PARAM,
		PITCH_ATTEN_PARAM,
		CLEAR_PARAM,
		RECALL_PARAM,
		SAVE_PARAM,
		REST_PARAM,
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
		LIGHTS_LEN
	};

	// Sequence State
	std::vector<RhythmicGroup> groups;
	std::vector<RhythmicGroup> saveSlots[10];

	struct PlayheadState {
		int currentGroupIdx = 0;
		int currentStepInGroup = 0;
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
			totalStepCounter = 0;
			barClockCounter = 0;
			gateTimeRemaining = 0.f;
			currentGateVoltage = 0.f;
			currentCV = 0.f;
			isAccent = false;
			isNoteActive = false;
		}
	};

	PlayheadState straightLane;
	PlayheadState tripletLane;
	float tripletSampleCounter = 0.f;
	int tripletStepIndex = 0;

	// Mode State
	enum InputMode {
		MODE_NORMAL,
		MODE_RECALL,
		MODE_SAVE,
		MODE_REST
	} mode = MODE_NORMAL;

	std::string displayStr = "00";

	// Clock Input Schmitt Triggers & Button Triggers
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::SchmittTrigger recallTrigger;
	dsp::SchmittTrigger saveTrigger;
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
		configParam(SYMMETRY_PARAM, 0.f, 100.f, 50.f, "Symmetry", "%");
		configParam(NOTE_DENSITY_PARAM, 0.f, 100.f, 100.f, "Note Density", "%");
		configParam(GATE_LENGTH_PARAM, 5.f, 95.f, 50.f, "Global Gate Length", "%");
		configParam(TRIPLET_STRAIGHT_PARAM, 0.f, 100.f, 100.f, "Triplet / Straight", "%");
		configParam(PITCH_ATTEN_PARAM, 0.f, 1.f, 1.f, "Pitch Attenuator", "%", 0.f, 100.f);

		configParam(CLEAR_PARAM, 0.f, 1.f, 0.f, "Clear");
		configParam(RECALL_PARAM, 0.f, 1.f, 0.f, "Recall");
		configParam(SAVE_PARAM, 0.f, 1.f, 0.f, "Save");
		configParam(REST_PARAM, 0.f, 1.f, 0.f, "Rest");

		for (int dig = 0; dig <= 9; dig++) {
			configParam(DIGIT_0_PARAM + dig, 0.f, 1.f, 0.f, "Digit " + std::to_string(dig));
		}

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");

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
			displayStr = "r-";
		} else if (mode == MODE_SAVE) {
			displayStr = "S-";
		} else if (mode == MODE_REST) {
			displayStr = "--";
		} else {
			int total = getTotalSteps();
			char buf[8];
			snprintf(buf, sizeof(buf), "%02d", total % 100);
			displayStr = buf;
		}
	}

	void onDialDigit(int digit) {
		int dialed = (digit == 0) ? 10 : digit;

		if (mode == MODE_RECALL) {
			int slot = (digit == 10) ? 0 : digit;
			if (slot >= 0 && slot < 10 && !saveSlots[slot].empty()) {
				groups = saveSlots[slot];
			}
			mode = MODE_NORMAL;
		} else if (mode == MODE_SAVE) {
			int slot = (digit == 10) ? 0 : digit;
			if (slot >= 0 && slot < 10) {
				saveSlots[slot] = groups;
			}
			mode = MODE_NORMAL;
		} else if (mode == MODE_REST) {
			RhythmicGroup g;
			g.length = dialed;
			g.isRest = true;
			g.symmetry = params[SYMMETRY_PARAM].getValue();
			generateGroupPattern(g);
			groups.push_back(g);
			mode = MODE_NORMAL;
		} else {
			RhythmicGroup g;
			g.length = dialed;
			g.isRest = false;
			g.symmetry = params[SYMMETRY_PARAM].getValue();
			generateGroupPattern(g);
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

	void advanceLaneStep(PlayheadState& lane, float sampleTime, float periodSec) {
		if (groups.empty()) return;

		lane.currentStepInGroup++;
		if (lane.currentStepInGroup >= groups[lane.currentGroupIdx].length) {
			lane.currentStepInGroup = 0;
			lane.currentGroupIdx = (lane.currentGroupIdx + 1) % groups.size();
		}

		lane.totalStepCounter++;
		lane.barClockCounter++;

		// 1. Phrase Start Out: fired at Group 0, Step 0
		if (lane.currentGroupIdx == 0 && lane.currentStepInGroup == 0) {
			lane.phrasePulse.trigger(0.010f);
		}

		// 2. Bar Start Out: fired every 4 clock pulses
		if (lane.barClockCounter % 4 == 1 || lane.barClockCounter == 1) {
			lane.barPulse.trigger(0.010f);
		}

		// 3. Group Start Out: fired on Step 0 of any active RhythmicGroup
		if (lane.currentStepInGroup == 0) {
			lane.groupPulse.trigger(0.010f);
		}

		// 4. Accent & Drift Out: Symmetry/Accent Hierarchy Engine & Turing Machine Mutation
		lane.isAccent = false;
		RhythmicGroup& curGroup = groups[lane.currentGroupIdx];
		float sym = params[SYMMETRY_PARAM].getValue();
		int randVal = std::rand() % 100;
		if (randVal < (int)sym) {
			lane.isAccent = getTemplateAccents(curGroup.length, lane.currentStepInGroup);
		} else {
			lane.isAccent = (turingRegister & 1) != 0;
			// Advance Turing Machine
			bool newBit = ((turingRegister & 1) ^ ((turingRegister >> 1) & 1));
			turingRegister = (turingRegister >> 1) | (newBit ? 0x8000 : 0);

			// Organic Drifting: mutate pitch CV slightly when symmetry < 100%
			if (lane.currentStepInGroup < (int)curGroup.stepCVs.size()) {
				int driftSemitones = ((turingRegister & 0x7) - 3); // -3..+4 semitones drift
				float newCV = curGroup.stepCVs[lane.currentStepInGroup] + driftSemitones / 12.0f;
				if (newCV < 0.f) newCV += 1.0f;
				if (newCV > 2.0f) newCV -= 1.0f;
				curGroup.stepCVs[lane.currentStepInGroup] = newCV;
			}
		}

		// 5. Note Density threshold check & Velocity Gate
		lane.isNoteActive = false;
		float stepVel = 0.f;
		if (!curGroup.isRest && lane.currentStepInGroup < (int)curGroup.stepVelocities.size()) {
			stepVel = curGroup.stepVelocities[lane.currentStepInGroup];
			float density = params[NOTE_DENSITY_PARAM].getValue();
			if (density > 0.f) {
				if (density >= 100.f) {
					lane.isNoteActive = true;
				} else {
					float threshold = 5.0f * (1.0f - density / 100.f);
					lane.isNoteActive = (stepVel >= threshold);
				}
			}
		}

		if (lane.isNoteActive) {
			float duty = params[GATE_LENGTH_PARAM].getValue() / 100.f;
			lane.gateTimeRemaining = periodSec * duty;
			float mult = lane.isAccent ? 1.1f : 1.0f;
			lane.currentGateVoltage = stepVel * mult;
			if (lane.currentGateVoltage > 10.f) lane.currentGateVoltage = 10.f;

			if (lane.currentStepInGroup < (int)curGroup.stepCVs.size()) {
				lane.currentCV = curGroup.stepCVs[lane.currentStepInGroup];
			}
		} else {
			lane.gateTimeRemaining = 0.f;
			lane.currentGateVoltage = 0.f;
		}

		if (lane.isAccent && lane.isNoteActive) {
			lane.accentPulse.trigger(0.010f);
		}
	}

	void process(const ProcessArgs& args) override {
		timesincesync++;

		// Button Handlers
		if (clearTrigger.process(params[CLEAR_PARAM].getValue())) {
			if (mode != MODE_NORMAL) {
				// Acts as universal Escape/Cancel when display is flashing command prompts (SAVE, RECALL, REST)
				mode = MODE_NORMAL;
				updateDisplay();
			} else {
				// Instantly flushes array, zeros variables, resets display to 00
				groups.clear();
				straightLane.reset();
				tripletLane.reset();
				tripletSampleCounter = 0.f;
				tripletStepIndex = 0;
				mode = MODE_NORMAL;
				updateDisplay();
			}
		}
		if (recallTrigger.process(params[RECALL_PARAM].getValue())) {
			mode = (mode == MODE_RECALL) ? MODE_NORMAL : MODE_RECALL;
			updateDisplay();
		}
		if (saveTrigger.process(params[SAVE_PARAM].getValue())) {
			mode = (mode == MODE_SAVE) ? MODE_NORMAL : MODE_SAVE;
			updateDisplay();
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

		// Check Reset Input
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
			straightLane.reset();
			tripletLane.reset();
			tripletSampleCounter = 0.f;
			tripletStepIndex = 0;
			straightLane.phrasePulse.trigger(0.010f);
			straightLane.groupPulse.trigger(0.010f);
			tripletLane.phrasePulse.trigger(0.010f);
			tripletLane.groupPulse.trigger(0.010f);
		}

		// Check Clock Input
		bool clockRisen = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
		if (clockRisen) {
			updateWobblerClockSync();

			// Clock Shutter Mechanism: force absolute phase reset on audio wave loop
			phaseAccumulator = 0;

			float straightPeriodSec = (extsync && SyncedPeriodTime > 0) ? ((float)SyncedPeriodTime * args.sampleTime) : 0.25f;
			advanceLaneStep(straightLane, args.sampleTime, straightPeriodSec);

			// Align & advance triplet lane every 2 straight clock steps
			if (straightLane.totalStepCounter % 2 == 0) {
				float tripletPeriodSec = straightPeriodSec * (2.0f / 3.0f);
				advanceLaneStep(tripletLane, args.sampleTime, tripletPeriodSec);
				tripletSampleCounter = 0.f;
				tripletStepIndex = 0;
			}
		}

		// Timeout check for extsync
		int timeoutSamples = (int)(args.sampleRate * 2.0f);
		if (timesincesync > timeoutSamples || syncindex == 0) {
			extsync = false;
		}

		// Advance sub-steps for Triplet lane
		float straightPeriodSamples = (extsync && SyncedPeriodTime > 0) ? (float)SyncedPeriodTime : (args.sampleRate * 0.25f);
		float tripletStepSamples = straightPeriodSamples * (2.0f / 3.0f);
		float tripletPeriodSec = tripletStepSamples * args.sampleTime;

		tripletSampleCounter += 1.0f;
		if (tripletStepIndex == 0 && tripletSampleCounter >= tripletStepSamples) {
			advanceLaneStep(tripletLane, args.sampleTime, tripletPeriodSec);
			tripletStepIndex = 1;
		} else if (tripletStepIndex == 1 && tripletSampleCounter >= 2.0f * tripletStepSamples) {
			advanceLaneStep(tripletLane, args.sampleTime, tripletPeriodSec);
			tripletStepIndex = 2;
		}

		// Update gate durations
		if (straightLane.gateTimeRemaining > 0.f) {
			straightLane.gateTimeRemaining -= args.sampleTime;
		} else {
			straightLane.currentGateVoltage = 0.f;
		}

		if (tripletLane.gateTimeRemaining > 0.f) {
			tripletLane.gateTimeRemaining -= args.sampleTime;
		} else {
			tripletLane.currentGateVoltage = 0.f;
		}

		float straightCV = straightLane.currentCV;
		float tripletCV = tripletLane.currentCV;

		// Determine if we are in Generator Mode (< 50ms period / > 20 Hz clock)
		bool generatorMode = (extsync && SyncedPeriodTime < (args.sampleRate * 0.05f));

		float atten = params[PITCH_ATTEN_PARAM].getValue();
		float mix = params[TRIPLET_STRAIGHT_PARAM].getValue() / 100.f; // 0 = triplet, 1 = straight

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
			// SEQUENCER MODE: Poly-metering crossfade between straight and triplet lanes
			float unipolarStraight = straightCV * 5.f * atten;
			float bipolarStraight = (straightCV - 1.f) * 5.f * atten;

			float unipolarTriplet = tripletCV * 5.f * atten;
			float bipolarTriplet = (tripletCV - 1.f) * 5.f * atten;

			float mixedUnipolar = unipolarTriplet * (1.f - mix) + unipolarStraight * mix;
			float mixedBipolar = bipolarTriplet * (1.f - mix) + bipolarStraight * mix;
			float mixedGate = tripletLane.currentGateVoltage * (1.f - mix) + straightLane.currentGateVoltage * mix;

			outputs[UNIPOLAR_CV_OUTPUT].setVoltage(mixedUnipolar);
			outputs[BIPOLAR_CV_OUTPUT].setVoltage(mixedBipolar);
			outputs[GATE_OUTPUT].setVoltage(mixedGate);
		}

		// Process Trigger Pulses (10V active for 10ms) crossfaded across lanes
		float phraseOut = (tripletLane.phrasePulse.process(args.sampleTime) ? 10.f : 0.f) * (1.f - mix)
						+ (straightLane.phrasePulse.process(args.sampleTime) ? 10.f : 0.f) * mix;
		float barOut = (tripletLane.barPulse.process(args.sampleTime) ? 10.f : 0.f) * (1.f - mix)
					 + (straightLane.barPulse.process(args.sampleTime) ? 10.f : 0.f) * mix;
		float groupOut = (tripletLane.groupPulse.process(args.sampleTime) ? 10.f : 0.f) * (1.f - mix)
					   + (straightLane.groupPulse.process(args.sampleTime) ? 10.f : 0.f) * mix;
		float accentOut = (tripletLane.accentPulse.process(args.sampleTime) ? 10.f : 0.f) * (1.f - mix)
						+ (straightLane.accentPulse.process(args.sampleTime) ? 10.f : 0.f) * mix;

		outputs[PHRASE_START_OUTPUT].setVoltage(phraseOut);
		outputs[BAR_START_OUTPUT].setVoltage(barOut);
		outputs[GROUP_START_OUTPUT].setVoltage(groupOut);
		outputs[ACCENT_OUTPUT].setVoltage(accentOut);
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

		// 1. Top Buttons (x = 18, 38, 58, 78 mm; y = 16 mm)
		addParam(createParamCentered<VCVButton>(mm2px(Vec(18.f, 16.f)), module, CallAFriend::CLEAR_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(38.f, 16.f)), module, CallAFriend::RECALL_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(58.f, 16.f)), module, CallAFriend::SAVE_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(78.f, 16.f)), module, CallAFriend::REST_PARAM));

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

		// 3. Knobs below dial (y = 78 mm; x = 15.0, 32.9, 50.8, 68.7, 86.6 mm)
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.0f, 78.f)), module, CallAFriend::SYMMETRY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(32.9f, 78.f)), module, CallAFriend::NOTE_DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.8f, 78.f)), module, CallAFriend::GATE_LENGTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(68.7f, 78.f)), module, CallAFriend::TRIPLET_STRAIGHT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(86.6f, 78.f)), module, CallAFriend::PITCH_ATTEN_PARAM));

		// 4. 2-Digit 7-Segment Display (x = 8.5 mm, y = 93 mm)
		SegmentDisplayWidget* disp = createWidget<SegmentDisplayWidget>(mm2px(Vec(8.5f, 93.f)));
		if (module) {
			disp->displayStr = &module->displayStr;
		}
		addChild(disp);

		// 5. Bottom Right 3x3 I/O Jack Grid (Row x = 49.5, 68.0, 86.5 mm; Row y = 96.0, 107.0, 118.0 mm)
		// Row 1
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(49.5f, 96.f)), module, CallAFriend::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(68.0f, 96.f)), module, CallAFriend::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(86.5f, 96.f)), module, CallAFriend::PHRASE_START_OUTPUT));

		// Row 2
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.5f, 107.f)), module, CallAFriend::BAR_START_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.0f, 107.f)), module, CallAFriend::GROUP_START_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(86.5f, 107.f)), module, CallAFriend::ACCENT_OUTPUT));

		// Row 3
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.5f, 118.f)), module, CallAFriend::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.0f, 118.f)), module, CallAFriend::UNIPOLAR_CV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(86.5f, 118.f)), module, CallAFriend::BIPOLAR_CV_OUTPUT));
	}
};

Model* modelCallAFriend = createModel<CallAFriend, CallAFriendWidget>("CallAFriend");
