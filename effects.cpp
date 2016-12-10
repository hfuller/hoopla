#include "effects.h"

int Effects::add(String name, std::function<void(void)> run) {
	if ( count >= sizeof(efx) ) {
		Serial.println("Couldn't add new effect, the pattern is full!");
		Serial.print("sizeof(efx): "); Serial.print(sizeof(efx));
		Serial.print(" count: "); Serial.println(count);
		return -1;
	}
	efx[count].name = name;
	efx[count].run = run;
	return(count++);
}

Effect Effects::get(int idx) {
	return efx[idx];
}

int Effects::getCount() {
	return count;
}

Effects::Effects() {
	count = 0;
	Serial.println("[e.cpp] effects object instantiated");

	Serial.print("[e.cpp] add test effect result: ");
	Serial.println(add("Test Effect", [](){
		Serial.println("Running test effect!!!");
	}));
}

