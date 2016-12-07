#include <Arduino.h>
#include <functional>

;

class Effect {
	public:
		String name = "Unnamed Effect";
		std::function<void(void)> run = [](){
			Serial.println("Running uninitialized effect...");
		};

	/*
	Effect(String n, std::function<void(void)> fn) {
		name = n;
		run = fn;
	}

	Effect(){};
	*/
};

Effect efx[2];

void initEffects() {
	efx[0].name = "Test Effect";
	efx[0].run = [](){
		Serial.println("Running test effect!!!");
	};
}

