#pragma once
#include <rack.hpp>
#include <string>

using namespace rack;

struct SegmentDisplayWidget : TransparentWidget {
	std::string* displayStr = nullptr;

	SegmentDisplayWidget() {
		box.size = mm2px(Vec(28.f, 18.f));
	}

	void drawSegment(const DrawArgs& args, Vec pos, int seg, NVGcolor color) {
		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, color);
		float w = 22.f;
		float h = 36.f;
		float t = 3.6f; // segment thickness
		switch (seg) {
			case 0: // top (a)
				nvgRect(args.vg, pos.x + t, pos.y, w - 2 * t, t);
				break;
			case 1: // top-right (b)
				nvgRect(args.vg, pos.x + w - t, pos.y + t, t, h * 0.5f - t);
				break;
			case 2: // bottom-right (c)
				nvgRect(args.vg, pos.x + w - t, pos.y + h * 0.5f, t, h * 0.5f - t);
				break;
			case 3: // bottom (d)
				nvgRect(args.vg, pos.x + t, pos.y + h - t, w - 2 * t, t);
				break;
			case 4: // bottom-left (e)
				nvgRect(args.vg, pos.x, pos.y + h * 0.5f, t, h * 0.5f - t);
				break;
			case 5: // top-left (f)
				nvgRect(args.vg, pos.x, pos.y + t, t, h * 0.5f - t);
				break;
			case 6: // middle (g)
				nvgRect(args.vg, pos.x + t, pos.y + h * 0.5f - t * 0.5f, w - 2 * t, t);
				break;
		}
		nvgFill(args.vg);
	}

	uint8_t getSegmentMask(char c) {
		switch (c) {
			case '0': return 0b00111111; // a b c d e f
			case '1': return 0b00000110; // b c
			case '2': return 0b01011011; // a b d e g
			case '3': return 0b01001111; // a b c d g
			case '4': return 0b01100110; // b c f g
			case '5': return 0b01101101; // a c d f g
			case '6': return 0b01111101; // a c d e f g
			case '7': return 0b00000111; // a b c
			case '8': return 0b01111111; // a b c d e f g
			case '9': return 0b01101111; // a b c d f g
			case 'r': return 0b01010000; // e g
			case 'S': return 0b01101101; // a c d f g
			case '-': return 0b01000000; // g
			default:  return 0b00000000;
		}
	}

	void drawChar(const DrawArgs& args, Vec pos, char c) {
		uint8_t mask = getSegmentMask(c);
		NVGcolor offColor = nvgRGBA(45, 12, 12, 65);
		NVGcolor onColor = nvgRGBA(255, 45, 10, 245);

		for (int s = 0; s < 7; s++) {
			bool active = (mask & (1 << s)) != 0;
			drawSegment(args, pos, s, active ? onColor : offColor);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;

		std::string s = "00";
		if (displayStr && displayStr->size() >= 2) {
			s = *displayStr;
		} else if (displayStr && displayStr->size() == 1) {
			s = "0" + *displayStr;
		}

		// Centered inside the 82.68 px wide x 53.15 px high slot
		float startX = 15.3f;
		float startY = 8.5f;
		drawChar(args, Vec(startX, startY), s[0]);
		drawChar(args, Vec(startX + 30.0f, startY), s[1]);
	}
};
