/*
 * FreeRTOSTimer.h
 *
 *  Created on: Mar 8, 2017
 *      Author: kolban
 */
/**
 * Original code from nkolban, licensed under Apache-2.0.
 * (See https://github.com/nkolban/esp32-snippets)
 *
 * Modified by oddlama (oddlama@oddlama.org).
 * All modifications are licensed under The MIT License,
 * a copy of which can be found in the 'LICENSE' file at
 * the project's root directory.
 */


#ifndef COMPONENTS_CPP_UTILS_FREERTOSTIMER_H_
#define COMPONENTS_CPP_UTILS_FREERTOSTIMER_H_
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
/**
 * @brief Wrapper around the %FreeRTOS timer functions.
 */
class FreeRTOSTimer {
public:
	FreeRTOSTimer(char* name, TickType_t period, UBaseType_t reload, void* data, void (*callback)(FreeRTOSTimer* pTimer));
	virtual ~FreeRTOSTimer();
	void changePeriod(TickType_t newPeriod, TickType_t blockTime = portMAX_DELAY);
	void* getData();
	const char* getName();
	TickType_t getPeriod();
	void reset(TickType_t blockTime = portMAX_DELAY);
	void start(TickType_t blockTime = portMAX_DELAY);
	void stop(TickType_t blockTime = portMAX_DELAY);

private:
	TimerHandle_t timerHandle;
	TickType_t period;
	void (*callback)(FreeRTOSTimer* pTimer);
	static void internalCallback(TimerHandle_t xTimer);

};

#endif /* COMPONENTS_CPP_UTILS_FREERTOSTIMER_H_ */
