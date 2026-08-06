#include "plugin.hpp"


struct ClockGenerator : Module {
	enum ParamId {
		TEMPO_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		CLOCK_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		BLINK_LIGHT,
		LIGHTS_LEN
	};

	float phase = 0.f;

	ClockGenerator() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TEMPO_PARAM, 0.f, 1.f, 0.5f, "Tempo", " Hz", 0.f, 1.f + 9.f);
		configInput(RESET_INPUT, "Reset");
		configOutput(CLOCK_OUTPUT, "Clock");
	}

	void process(const ProcessArgs& args) override {
		// Reset phase on trigger
		if (inputs[RESET_INPUT].getVoltage() >= 2.0f) {
			phase = 0.f;
		}

		// Map 0..1 parameter to 1..10 Hz
		float pitch = params[TEMPO_PARAM].getValue();
		float freq = 1.f + pitch * 9.f;

		// Accumulate time
		phase += freq * args.sampleTime;
		if (phase >= 1.f) {
			phase -= 1.f;
		}

		// Generate square wave (50% duty cycle, 10V trigger)
		float clockVal = (phase < 0.5f) ? 10.f : 0.f;
		outputs[CLOCK_OUTPUT].setVoltage(clockVal);

		// Blink light
		lights[BLINK_LIGHT].setBrightness(phase < 0.5f ? 1.f : 0.f);
	}
};


struct ClockGeneratorWidget : ModuleWidget {
	ClockGeneratorWidget(ClockGenerator* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ClockGenerator.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 46.063)), module, ClockGenerator::TEMPO_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 77.478)), module, ClockGenerator::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 108.713)), module, ClockGenerator::CLOCK_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(15.24, 25.81)), module, ClockGenerator::BLINK_LIGHT));
	}
};


Model* modelClockGenerator = createModel<ClockGenerator, ClockGeneratorWidget>("ClockGenerator");