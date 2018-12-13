/*
 * SC-Controller - Mode shift
 *
 * Assings multiple actions to same input and choses between them
 * based on condition.
 */
#include "scc/utils/logging.h"
#include "scc/utils/strbuilder.h"
#include "scc/utils/iterable.h"
#include "scc/utils/math.h"
#include "scc/utils/list.h"
#include "scc/utils/rc.h"
#include "scc/param_checker.h"
#include "scc/action.h"
#include "scc/tools.h"
#include "tostring.h"
#include <stdlib.h>
#include <stdio.h>

static const char* KW_MODE = "mode";
#define MIN_TRIGGER		2 /* When trigger is bellow this position, list of held_triggers is cleared */
#define MIN_STICK		2 /* When abs(stick) < MIN_STICK, stick is considered released and held_sticks is cleared */

typedef enum {
	MCT_BUTTON,
	MCT_RANGE,
	MCT_DEFAULT,
} ModeConditionType;

typedef struct Mode {
	ModeConditionType	c_type;
	union {
		SCButton		c_button;
		Parameter*		c_range;
	};
	Action*				action;
	bool				is_active;
} Mode;

typedef LIST_TYPE(Mode) Modes;


typedef struct {
	Action				action;
	ParameterList		params;
	Modes				modes;
	SCButton			held_buttons;
	uint8_t				held_sticks;		// bit set from PadStickTrigger
	uint8_t				held_triggers;		// bit set from PadStickTrigger
} ModeModifier;


ACTION_MAKE_TO_STRING(ModeModifier, mode, KW_MODE, NULL);

/** Deallocates ModeModifier */
static void mode_dealloc(Action* a) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	list_free(mm->params);
	list_free(mm->modes);
	free(mm);
}

/** Deallocates instance of struct Mode */
static void struct_mode_free(void* a) {
	Mode* m = (Mode*)a;
	if (m->c_type == MCT_RANGE)
		RC_REL(m->c_range);
	RC_REL(m->action);
	free(m);
}

static Action* compress(Action* a) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	FOREACH_IN(Mode*, mode, mm->modes)
		scc_action_compress(&mode->action);
	return a;
}

static Mode* choose(ModeModifier* mm, Mapper* m) {
	Mode* deflt = NULL;
	FOREACH_IN(Mode*, mode, mm->modes) {
		switch (mode->c_type) {
		case MCT_BUTTON:
			if (m->is_pressed(m, mode->c_button))
				return mode;
			break;
		case MCT_RANGE:
			break;
		case MCT_DEFAULT:
			deflt = mode;
			break;
		}
	}
	
	return deflt;
}


#define DEACTIVATE_ALL(mm, condition, input, ...) do {					\
	FOREACH_IN(Mode*, md, mm->modes) {									\
		if ((condition) && (md->is_active)) {							\
			md->action->input(md->action, __VA_ARGS__);					\
			md->is_active = false;										\
		}																\
	}																	\
} while (0)

static void change(Action* a, Mapper* m, double dx, double dy, PadStickTrigger what) {
	// ModeModifier* mm = container_of(a, ModeModifier, action);
}

static void button_press(Action* a, Mapper* m) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	Mode* mode = choose(mm, m);
	if (mode) {
		mode->action->button_press(mode->action, m);
		mode->is_active = true;
	}
}

static void button_release(Action* a, Mapper* m) {
	// Releases all active children, not just button that matches currently pressed modifier
	ModeModifier* mm = container_of(a, ModeModifier, action);
	DEACTIVATE_ALL(mm, true, button_release, m);
}

static void axis(Action* a, Mapper* m, AxisValue value, PadStickTrigger what) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	Mode* mode = choose(mm, m);
	if (mode)
		mode->action->axis(mode->action, m, value, what);
}

static void whole(Action* a, Mapper* m, AxisValue x, AxisValue y, PadStickTrigger what) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	if (what == PST_STICK) {
		if ((abs(x) < MIN_STICK) && (abs(y) < MIN_STICK)) {
			DEACTIVATE_ALL(mm, true, whole, m, 0, 0, what);
		} else {
			Mode* mode = choose(mm, m);
			if (mode == NULL) {
				DEACTIVATE_ALL(mm, true, whole, m, 0, 0, what);
			} else {
				DEACTIVATE_ALL(mm, md != mode, whole, m, 0, 0, what);
				mode->action->whole(mode->action, m, x, y, what);
				mode->is_active = true;
			}
		}
		// mapper.force_event.add(FE_STICK)
	} else {
		Mode* mode = choose(mm, m);
		if ((mode == NULL) || (!mode->is_active)) {
			// mapper.set_button(what, False)
			DEACTIVATE_ALL(mm, true, whole, m, 0, 0, what);
			if (mode != NULL) {
				mode->action->whole(mode->action, m, x, y, what);
				mode->is_active = true;
			}
			// mapper.set_button(what, True)
		} else if (mode != NULL) {
			mode->action->whole(mode->action, m, x, y, what);
			mode->is_active = true;
		}
	}
}

static void gyro(Action* a, Mapper* m, GyroValue pitch, GyroValue yaw, GyroValue roll,
					GyroValue q1, GyroValue q2, GyroValue q3, GyroValue q4) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	Mode* mode = choose(mm, m);
	if ((mode == NULL) || (!mode->is_active)) {
		// Switching to no or different action
		DEACTIVATE_ALL(mm, true, gyro, m, 0, 0, 0, q1, q2, q3, q4);
	}
	if (mode) {
		mode->action->gyro(mode->action, m, pitch, yaw, roll, q1, q2, q3, q4);
		mode->is_active = true;
	}
}

static void trigger(Action* a, Mapper* m, TriggerValue old_pos, TriggerValue pos, PadStickTrigger what) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	if (pos < MIN_TRIGGER) {
		DEACTIVATE_ALL(mm, true, trigger, m, old_pos, pos, what);
	} else {
		Mode* mode = choose(mm, m);
		if (mode) {
			mode->action->trigger(mode->action, m, old_pos, pos, what);
			mode->is_active = true;
		}
	}
}


static void set_haptic(Action* a, HapticData hdata) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	FOREACH_IN(Mode*, mode, mm->modes) {
		if (mode->action->extended.set_haptic != NULL)
			mode->action->extended.set_haptic(mode->action, hdata);
	}
}

static void set_sensitivity(Action* a, float x, float y, float z) {
	ModeModifier* mm = container_of(a, ModeModifier, action);
	FOREACH_IN(Mode*, mode, mm->modes) {
		if (mode->action->extended.set_sensitivity != NULL)
			mode->action->extended.set_sensitivity(mode->action, x, y, z);
	}
}


static ActionOE mode_constructor(const char* keyword, ParameterList params) {
	ListIterator it = iter_get(params);
	Modes modes = list_new(Mode, 2);
	params = scc_copy_param_list(params);
	ModeModifier* mm = malloc(sizeof(ModeModifier));
	if ((modes == NULL) || (it == NULL) || (mm == NULL) || (params == NULL)) {
		free(mm);
		iter_free(it);
		list_free(modes);
		list_free(params);
		return (ActionOE)scc_oom_action_error();
	}
	
	scc_action_init(&mm->action, KW_MODE, AF_ACTION, &mode_dealloc, &mode_to_string);
	list_set_dealloc_cb(modes, &struct_mode_free);
	mm->modes = modes;
	mm->params = params;
	
	mm->action.compress = &compress;
	mm->action.button_press = &button_press;
	mm->action.button_release = &button_release;
	mm->action.axis = &axis;
	mm->action.whole = &whole;
	mm->action.gyro = &gyro;
	mm->action.trigger = &trigger;
	mm->action.extended.set_sensitivity = &set_sensitivity;
	mm->action.extended.set_haptic = &set_haptic;
	mm->action.extended.change = &change;
	
	Mode* mode = NULL;
	FOREACH(Parameter*, p, it) {
		if (mode == NULL) {
			// Reading button or condition
			if (scc_parameter_type(p) & PT_STRING) {
				SCButton b = scc_string_to_button(scc_parameter_as_string(p));
				if (b != 0) {
					// setup condition here
					mode = malloc(sizeof(Mode));
					if (mode == NULL) goto mode_constructor_fail;
					mode->c_type = MCT_BUTTON;
					mode->c_button = b;
					mode->is_active = false;
					mode->action = NULL;
				}
			} else if (scc_parameter_type(p) & PT_RANGE) {
				mode = malloc(sizeof(Mode));
				if (mode == NULL) goto mode_constructor_fail;
				mode->c_type = MCT_RANGE;
				mode->c_range = p;
				mode->is_active = false;
				mode->action = NULL;
				RC_ADD(mode->c_range);
			} else if (scc_parameter_type(p) & PT_ACTION) {
				if (!iter_has_next(it)) {
					// last parameter is an action
					mode = malloc(sizeof(Mode));
					if (mode == NULL) goto mode_constructor_fail;
					mode->c_type = MCT_DEFAULT;
					mode->is_active = false;
					mode->action = scc_parameter_as_action(p);
					if (!list_add(modes, mode)) {
						free(mode);
						goto mode_constructor_fail;
					}
					RC_ADD(mode->action);
					mode = NULL;
					break;
				}
			}
			
			if (mode == NULL) {
				// Failed to decode condition
				ParamError* e;
				RC_REL(&mm->action);
				char* _p = scc_parameter_to_string(p);
				if (_p == NULL) {
					e = scc_oom_action_error();
				} else {
					e = scc_new_param_error(AEC_INVALID_PARAMETER_TYPE,
						"%s cannot take %s as button/condition", KW_MODE, _p);
					free(_p);
				}
				return (ActionOE)e;
			}
		} else {
			// Reading action
			if (!(scc_parameter_type(p) & PT_ACTION)) {
				ParamError* e;
				char* _p = scc_parameter_to_string(p);
				if (_p == NULL) {
					e = scc_oom_action_error();
				} else {
					// TODO: Simplify formatting of theese
					e = scc_new_param_error(AEC_INVALID_PARAMETER_TYPE,
						"%s cannot take %s as action parameter", KW_MODE, _p);
					free(_p);
				}
				return (ActionOE)e;
			}
			mode->action = scc_parameter_as_action(p);
			if (!list_add(modes, mode)) {
				free(mode);
				goto mode_constructor_fail;
			}
			RC_ADD(mode->action);
			mode = NULL;
		}
	}
	iter_free(it);
	if (mode != NULL) {
		free(mode);
		return (ActionOE)scc_new_param_error(AEC_INVALID_NUMBER_OF_PARAMETERS, "Expected action after last parameter");
	}
	
	return (ActionOE)&mm->action;
	
mode_constructor_fail:
	RC_REL(&mm->action);
	return (ActionOE)scc_oom_action_error();
}

void scc_actions_init_mode() {
	scc_action_register(KW_MODE, &mode_constructor);
}
