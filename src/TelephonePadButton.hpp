#pragma once
#include <rack.hpp>
#include <string>

using namespace rack;

struct TelephonePadButton : ParamWidget {
	std::string label = "1";
	bool pressed = false;

	TelephonePadButton() {
		box.size = mm2px(Vec(11.f, 8.8f));
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS) {
				pressed = true;
				if (module) {
					module->params[paramId].setValue(1.f);
				}
				e.consume(this);
				return;
			} else if (e.action == GLFW_RELEASE) {
				pressed = false;
				if (module) {
					module->params[paramId].setValue(0.f);
				}
				e.consume(this);
				return;
			}
		}
		ParamWidget::onButton(e);
	}

	void onEnter(const EnterEvent& e) override {
		ParamWidget::onEnter(e);
	}

	void onLeave(const LeaveEvent& e) override {
		pressed = false;
		if (module) {
			module->params[paramId].setValue(0.f);
		}
		ParamWidget::onLeave(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;

		bool active = pressed || (module && module->params[paramId].getValue() > 0.5f);

		// Outer shadow / border
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.5f);
		nvgFillColor(args.vg, nvgRGB(18, 21, 26));
		nvgFill(args.vg);

		// Key face
		nvgBeginPath(args.vg);
		float offset = active ? 1.0f : 0.0f;
		nvgRoundedRect(args.vg, 1.5f + offset * 0.5f, 1.5f + offset * 0.5f, box.size.x - 3.f, box.size.y - 3.f, 2.0f);
		if (active) {
			nvgFillColor(args.vg, nvgRGB(0, 180, 240));
		} else {
			nvgFillColor(args.vg, nvgRGB(45, 52, 64));
		}
		nvgFill(args.vg);

		nvgStrokeColor(args.vg, active ? nvgRGB(180, 240, 255) : nvgRGB(80, 92, 110));
		nvgStrokeWidth(args.vg, 1.2f);
		nvgStroke(args.vg);

		// Digit text on button face
		nvgFontSize(args.vg, 16.f);
		if (APP->window->uiFont) {
			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		}
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, active ? nvgRGB(15, 20, 25) : nvgRGB(235, 242, 255));
		nvgText(args.vg, box.size.x * 0.5f + offset, box.size.y * 0.5f + offset, label.c_str(), nullptr);
	}
};
