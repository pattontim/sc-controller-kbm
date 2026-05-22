/**
 * SC Controller - Dampen modifier
 *
 * Dampens mouse movement when trigger is actuated anew to prevent jitter during clicks.
 * Only filters small movement deltas, allowing normal drag operations.
*/
#include "scc/utils/logging.h"
#include "scc/utils/strbuilder.h"
#include "scc/utils/math.h"
#include "scc/utils/rc.h"
#include "scc/param_checker.h"
#include "scc/action.h"
#include "tostring.h"
#include "props.h"
#include <stdlib.h>
#include <tgmath.h>

static ParamChecker pc;
static const char* KW_DAMPEN = "dampen";

typedef struct {
	Action				action;
	Action*				child;
	ParameterList		params;
	double				threshold;
	PadStickTrigger		trigger;
	TriggerValue		old_trigger_pos;
	bool				trigger_was_pressed;
	ivec_t				old_pos;
	bool				old_pos_set;
} DampenModifier;

ACTION_MAKE_TO_STRING(DampenModifier, dampen, KW_DAMPEN, &pc);

MODIFIER_MAKE_DESCRIBE(DampenModifier, "%s (dampen)", "%s (dampen)");

static void dampen_dealloc(Action* a) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	list_free(d->params);
	RC_REL(d->child);
	free(d);
}

static Action* compress(Action* a) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	scc_action_compress(&d->child);
	return a;
}

static Action* get_child(Action* a) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	RC_ADD(d->child);
	return d->child;
}

static Parameter* get_property(Action* a, const char* name) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	MAKE_FLOAT_PROPERTY(d->threshold, "threshold");
	
	DWARN("Requested unknown property '%s' from '%s'", name, a->type);
	return NULL;
}

static void whole(Action* a, Mapper* m, AxisValue x, AxisValue y, PadStickTrigger what) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	
	// Calculate movement magnitude from old position
	if (d->old_pos_set && m->is_touched(m, what)) {
		double dx = ((double)x - d->old_pos.x);
		double dy = ((double)y - d->old_pos.y);
		double magnitude = sqrt(dx * dx + dy * dy);
		
		// Only apply dampening for small movements when trigger is newly actuated
		if (d->trigger_was_pressed && magnitude < d->threshold) {
			// Skip this movement (dampen it) but update position tracking
			DEBUG("DAMPEN: Blocking small movement (magnitude=%.2f < threshold=%.2f) trigger_was_pressed=%d",
				magnitude, d->threshold, d->trigger_was_pressed);
			d->old_pos.x = x;
			d->old_pos.y = y;
			return;
		} else {
            DEBUG("DAMPEN: Allowing movement (magnitude=%.2f, threshold=%.2f) trigger_was_pressed=%d",
                magnitude, d->threshold, d->trigger_was_pressed);
        }
		
		if (d->trigger_was_pressed) {
			DEBUG("DAMPEN: Allowing movement (magnitude=%.2f >= threshold=%.2f)", magnitude, d->threshold);
		} else {
            DEBUG("DAMPEN: No trigger press detected, allowing movement (magnitude=%.2f)", magnitude);
        }
	}
	
	// Pass through to child action
	d->child->whole(d->child, m, x, y, what);
	d->old_pos.x = x;
	d->old_pos.y = y;
	d->old_pos_set = true;
}

static void trigger(Action* a, Mapper* m, TriggerValue old_pos, TriggerValue pos, PadStickTrigger what) {
	DampenModifier* d = container_of(a, DampenModifier, action);
	
	// Only track state for the trigger we're monitoring
	if (what != d->trigger)
		return;
	
	// Detect "actuated anew" - trigger just crossed press threshold (assuming 50% is press threshold)
	bool trigger_now_pressed = (pos > TRIGGER_MAX / 2);
	bool trigger_actuated_anew = trigger_now_pressed && !d->trigger_was_pressed;
	
	if (trigger_actuated_anew) {
		DEBUG("DAMPEN: Trigger '%s' PRESSED (old_pos=%d, pos=%d) - dampening enabled", 
			d->trigger == PST_LTRIGGER ? "LT" : "RT", old_pos, pos);
	} else {
        DEBUG("DAMPEN: Trigger '%s' state changed (old_pos=%d, pos=%d) - trigger_was_pressed=%d",
            d->trigger == PST_LTRIGGER ? "LT" : "RT", old_pos, pos, d->trigger_was_pressed);
    }
	
	if (d->trigger_was_pressed && !trigger_now_pressed) {
		DEBUG("DAMPEN: Trigger '%s' RELEASED (old_pos=%d, pos=%d) - dampening disabled", 
			d->trigger == PST_LTRIGGER ? "LT" : "RT", old_pos, pos);
	} else {
        DEBUG("DAMPEN: Trigger '%s' state changed (old_pos=%d, pos=%d) - trigger_was_pressed=%d",
            d->trigger == PST_LTRIGGER ? "LT" : "RT", old_pos, pos, d->trigger_was_pressed);
    }
	
	d->trigger_was_pressed = trigger_now_pressed;
	d->old_trigger_pos = pos;
	
	// If trigger was released, reset the dampening state
	if (!trigger_now_pressed) {
		d->trigger_was_pressed = false;
		d->old_pos_set = false;
	}
}

static ActionOE dampen_constructor(const char* keyword, ParameterList params) {
    DEBUG("Constructed dampen");
	ParamError* err = scc_param_checker_check(&pc, keyword, params);
	if (err != NULL) return (ActionOE)err;
	params = scc_param_checker_fill_defaults(&pc, params);
	if (params == NULL) return (ActionOE)scc_oom_action_error();
	
	DampenModifier* d = malloc(sizeof(DampenModifier));
	if (d == NULL) return (ActionOE)scc_oom_action_error();
	scc_action_init(&d->action, KW_DAMPEN, AF_MODIFIER, &dampen_dealloc, &dampen_to_string);
	d->action.get_property = &get_property;
	d->action.describe = &describe;
	d->action.compress = &compress;
	d->action.whole = &whole;
	d->action.trigger = &trigger;
	d->action.extended.get_child = &get_child;
	
	d->threshold = scc_parameter_as_float(params->items[0]);
	const char* trigger_str = scc_parameter_as_string(params->items[1]);
	
	if (0 == strcmp(trigger_str, "LT"))
		d->trigger = PST_LTRIGGER;
	else if (0 == strcmp(trigger_str, "RT"))
		d->trigger = PST_RTRIGGER;
	else {
		free(d);
		return (ActionOE)scc_new_param_error(AEC_INVALID_VALUE,
			"'%s' cannot take '%s' as trigger parameter", keyword, trigger_str);
	}
	
	d->child = scc_parameter_as_action(params->items[2]);
	d->params = params;
	d->old_trigger_pos = 0;
	d->trigger_was_pressed = false;
	d->old_pos.x = 0;
	d->old_pos.y = 0;
	d->old_pos_set = false;
	
	RC_ADD(d->child);
	return (ActionOE)&d->action;
}

void scc_actions_init_dampen() {
	scc_param_checker_init(&pc, "fsa");
	scc_param_checker_set_defaults(&pc, 5.0, "RT");
	scc_action_register(KW_DAMPEN, &dampen_constructor);
}
