#pragma once
#include <rack.hpp>
#include <functional>
#include <cmath>

using namespace rack;

struct RotaryDialWidget : OpaqueWidget {
	float currentAngle = 0.f;       // in radians
	bool dragging = false;
	bool returning = false;
	int selectedDigit = -1;
	int targetDigit = -1;
	float dragStartAngle = 0.f;
	float wheelStartAngle = 0.f;

	std::function<void(int)> onDigitDialed;

	RotaryDialWidget() {
		box.size = mm2px(Vec(43.f, 43.f));
	}

	// Helper: get angle for digit index (1=1, ..., 9=9, 0=10 pulses)
	// Digits are spaced around the circle
	float getDigitRestAngle(int digit) const {
		// digit: 1..9, 0 (where 0 means 10)
		int idx = (digit == 0) ? 10 : digit;
		// 1 is at ~ 0.5 rad, 0(10) is at ~ 2.4 rad
		return 0.35f + idx * 0.23f;
	}

	float getStopAngleForDigit(int digit) const {
		int idx = (digit == 0) ? 10 : digit;
		return idx * 0.28f;
	}

	void step() override {
		OpaqueWidget::step();
		if (returning) {
			float returnSpeed = 3.5f * APP->engine->getSampleTime() * 44100.f / 60.f; // smooth return
			if (returnSpeed < 0.03f) returnSpeed = 0.03f;
			currentAngle -= returnSpeed;
			if (currentAngle <= 0.f) {
				currentAngle = 0.f;
				returning = false;
				if (targetDigit >= 0 && onDigitDialed) {
					onDigitDialed(targetDigit);
				}
				targetDigit = -1;
			}
		}
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS && !returning) {
				Vec center = box.size.div(2);
				Vec d = e.pos.minus(center);
				float dist = d.norm();
				float radius = box.size.x * 0.38f;
				if (dist > radius * 0.45f && dist < radius * 1.35f) {
					float clickAngle = std::atan2(d.y, d.x);
					// Find closest digit hole
					int bestDigit = -1;
					float minDist = 10.f;
					for (int dig = 0; dig <= 9; dig++) {
						float ang = getDigitRestAngle(dig);
						float diff = std::abs(ang - clickAngle);
						if (diff < minDist) {
							minDist = diff;
							bestDigit = dig;
						}
					}
					if (bestDigit >= 0 && minDist < 0.35f) {
						dragging = true;
						selectedDigit = bestDigit;
						targetDigit = bestDigit;
						dragStartAngle = clickAngle;
						wheelStartAngle = currentAngle;
						e.consume(this);
						return;
					}
				}
			} else if (e.action == GLFW_RELEASE && dragging) {
				dragging = false;
				// If released or clicked, trigger return sequence from full stop angle
				if (targetDigit >= 0) {
					currentAngle = getStopAngleForDigit(targetDigit);
					returning = true;
				}
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (dragging && selectedDigit >= 0) {
			float stopAng = getStopAngleForDigit(selectedDigit);
			currentAngle += 0.05f;
			if (currentAngle > stopAng) currentAngle = stopAng;
		}
		OpaqueWidget::onDragMove(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;

		Vec center = box.size.div(2);
		float r = box.size.x * 0.42f;

		// 1. Draw base plate with numbers
		nvgSave(args.vg);
		nvgTranslate(args.vg, center.x, center.y);

		// Outer dark metallic ring
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, 0, 0, r);
		nvgFillColor(args.vg, nvgRGB(28, 32, 38));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(60, 68, 80));
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);

		// Numbers underneath
		nvgFontSize(args.vg, 13.f);
		if (APP->window->uiFont) {
			nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		}
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int dig = 0; dig <= 9; dig++) {
			float ang = getDigitRestAngle(dig);
			float nx = std::cos(ang) * (r * 0.68f);
			float ny = std::sin(ang) * (r * 0.68f);
			nvgFillColor(args.vg, nvgRGB(230, 240, 255));
			std::string s = std::to_string(dig);
			nvgText(args.vg, nx, ny, s.c_str(), nullptr);
		}

		// Finger Stop bracket at ~ 2.8 rad
		float stopAng = 2.85f;
		float sx = std::cos(stopAng) * (r * 0.72f);
		float sy = std::sin(stopAng) * (r * 0.72f);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, sx, sy, 4.5f);
		nvgFillColor(args.vg, nvgRGB(180, 190, 205));
		nvgFill(args.vg);

		// 2. Rotating finger wheel
		nvgRotate(args.vg, currentAngle);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, 0, 0, r * 0.92f);
		nvgFillColor(args.vg, nvgRGBA(15, 18, 22, 140));
		nvgFill(args.vg);

		// Draw holes in wheel
		for (int dig = 0; dig <= 9; dig++) {
			float ang = getDigitRestAngle(dig);
			float hx = std::cos(ang) * (r * 0.68f);
			float hy = std::sin(ang) * (r * 0.68f);
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, hx, hy, r * 0.16f);
			nvgStrokeColor(args.vg, nvgRGB(110, 125, 145));
			nvgStrokeWidth(args.vg, 1.2f);
			nvgStroke(args.vg);
		}

		// Center cap
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, 0, 0, r * 0.32f);
		nvgFillColor(args.vg, nvgRGB(20, 24, 30));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(80, 95, 115));
		nvgStrokeWidth(args.vg, 1.0f);
		nvgStroke(args.vg);

		nvgRestore(args.vg);
	}
};
