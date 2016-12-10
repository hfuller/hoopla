#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include <functional>

class Effect {
	public:
		String name;
		std::function<void(void)> run;
};

class Effects {
	Effect efx[25];
	int count;
	public:
		Effects();
		int add(String name, std::function<void(void)> run);
		Effect get(int idx);
		int getCount();
};

#endif (ifndef EFFECTS_H)

