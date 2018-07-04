/*
 * led_states.h
 *
 *  Created on: Jun 18, 2018
 *      Author: patlas
 */

#ifndef LED_STATES_H_
#define LED_STATES_H_

#include <linux/init.h>

enum {
	RED = 0,
	GREEN,
	BLUE,
	BLINK,
	SIZE
};


typedef enum {
	STATE_FACTORY_BOOTED = 0,
	STATE_BOOTING,
	STATE_DISCLOSURE,
	STATE_CONNECTED_PRIOVISIONING,
	STATE_PROVISIONED,
	STATE_RESET,
	STATE_SIZE
} state_t;

//typedef (void)(*fblink_ptr)(void);

typedef struct {
	u8 lred;
	u8 lgreen;
	u8 lblue;
} rgb_luminance_t;

typedef struct {
	const char *state_string;
	void (*fblink_ptr)(void);
} s_state;


typedef struct {
	bool blink;
	u32 ton_ms; 	// not obligatory if blink == True
	u32 toff_ms; 	// not obligatory if blink == True
} blink_t;


#endif /* LED_STATES_H_ */
