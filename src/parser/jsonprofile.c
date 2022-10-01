/*
 * SC Controller - JSON Profile
 * 
 * This is default (and only writable) implementation of Profile, stored in
 * ".scprofile" file which is actually just json with nice icon.
 */
#define LOG_TAG "Profile"
#include "scc/utils/logging.h"
#include "scc/utils/assert.h"
#include "scc/utils/aojls.h"
#include "scc/utils/rc.h"
#include "scc/profile.h"
#include "scc/parser.h"
#include "scc/action.h"
#include <stdlib.h>
#include <errno.h>

static const char* PROFILE_TYPE_JSON = "json";
#define LAST_BUTTON_INDEX 24
#define LAST_AXIS_INDEX PST_GYRO
#define LAST_TRIGGER_INDEX 1
#define MAX_AXIS_KEY_LEN 15
#define MAX_TYPE_KEY_LEN 40

typedef struct {
	Profile		profile;
	Action*		buttons[LAST_BUTTON_INDEX + 1];
	Action*		triggers[LAST_TRIGGER_INDEX + 1];
	Action*		axes[LAST_AXIS_INDEX + 1];
} JSONProfile;

static char* button_names[] = {
	NULL, "RPADTOUCH", "LPADTOUCH", "RPADPRESS", "LPADPRESS", "RGRIP", "LGRIP",
	"START", "C", "BACK", "A", "X", "B", "Y", "LB", "RB", "LT", "RT",
	"CPADTOUCH", "CPADPRESS", "STICKPRESS", "RSTICKPRESS", "DOTS",
	"RGRIP2", "LGRIP2"
};

static_assert(COUNT_OF(button_names) == LAST_BUTTON_INDEX + 1);

static char* axis_names[] = {
	// Order here has to match PadStickTrigger enum
	NULL, "pad_left", "pad_right", NULL, NULL, "cpad", "dpad", "stick", "rstick", "gyro"
};


static void json_profile_dealloc(void* obj) {
	JSONProfile* p = container_of(obj, JSONProfile, profile);
	for (int i=0; i<=LAST_BUTTON_INDEX; i++)
		RC_REL(p->buttons[i]);
	for (int i=0; i<=LAST_TRIGGER_INDEX; i++)
		RC_REL(p->triggers[i]);
	for (int i=0; i<=LAST_AXIS_INDEX; i++)
		RC_REL(p->axes[i]);
	free(p);
}

/**
 * Converts value from SCButton enum to number from 0 to 21.
 * This conversion is internal for profile and used only to get index to
 * buttons array.
 */
static int scbutton_to_index(SCButton b) {
	switch (b) {
		case B_RPADTOUCH:
			return 1;
		case B_LPADTOUCH:
			return 2;
		case B_RPADPRESS:
			return 3;
		case B_LPADPRESS:
			return 4;
		case B_RGRIP:
			return 5;
		case B_LGRIP:
			return 6;
		case B_START:
			return 7;
		case B_C:
			return 8;
		case B_BACK:
			return 9;
		case B_A:
			return 10;
		case B_X:
			return 11;
		case B_B:
			return 12;
		case B_Y:
			return 13;
		case B_LB:
			return 14;
		case B_RB:
			return 15;
		case B_LT:
			return 16;
		case B_RT:
			return 17;
		case B_CPADTOUCH:
			return 18;
		case B_CPADPRESS:
			return 19;
		case B_STICKPRESS:
			return 20;
		case B_RSTICKPRESS:
			return 21;
		case B_DOTS	:
			return 22;
		case B_RGRIP2:
			return 23;
		case B_LGRIP2:
			return 24;
		case _SCButton_padding:
			return 0;
	}
	return 0;
}

/**
 * As above but L and RGrip are swapped
 */
static int scbutton_to_index_fixed(SCButton b) {
	switch (b) {
		case B_RPADTOUCH:
			return 1;
		case B_LPADTOUCH:
			return 2;
		case B_RPADPRESS:
			return 3;
		case B_LPADPRESS:
			return 4;
		case B_LGRIP:
			return 5;
		case B_RGRIP:
			return 6;
		case B_START:
			return 7;
		case B_C:
			return 8;
		case B_BACK:
			return 9;
		case B_A:
			return 10;
		case B_X:
			return 11;
		case B_B:
			return 12;
		case B_Y:
			return 13;
		case B_LB:
			return 14;
		case B_RB:
			return 15;
		case B_LT:
			return 16;
		case B_RT:
			return 17;
		case B_CPADTOUCH:
			return 18;
		case B_CPADPRESS:
			return 19;
		case B_STICKPRESS:
			return 20;
		case B_RSTICKPRESS:
			return 21;
		case B_DOTS	:
			return 22;
		case B_RGRIP2:
			return 23;
		case B_LGRIP2:
			return 24;
		case _SCButton_padding:
			return 0;
	}
	return 0;
}

static Action* get_button(Profile* _p, SCButton b) {
	JSONProfile* p = container_of(_p, JSONProfile, profile);
	Action* a = p->buttons[scbutton_to_index(b)];
	if(_p->sc_keyboard){
		if(b == B_LGRIP)
			a = p->buttons[scbutton_to_index_fixed(b)];
		else if(b == B_RGRIP)
			a = p->buttons[scbutton_to_index_fixed(b)];
	}
	RC_ADD(a);
	return a;
}

static Action* get_pad(Profile* _p, PadStickTrigger t) {
	JSONProfile* p = container_of(_p, JSONProfile, profile);
	Action* a = NoAction;
	if (t <= LAST_AXIS_INDEX)
		a = p->axes[t];
	RC_ADD(a);
	return a;
}

static Action* get_trigger(Profile* _p, PadStickTrigger t) {
	JSONProfile* p = container_of(_p, JSONProfile, profile);
	Action* a = NoAction;
	if (t == PST_LTRIGGER)
		a = p->triggers[0];
	else if (t == PST_RTRIGGER)
		a = p->triggers[1];
	RC_ADD(a);
	return a;
}

static Action* get_stick(Profile* _p, PadStickTrigger t) {
	JSONProfile* p = container_of(_p, JSONProfile, profile);
	Action* a = p->axes[t];
	RC_ADD(a);
	return a;
}

static Action* get_gyro(Profile* _p) {
	return get_stick(_p, PST_GYRO);
}

static void compress(Profile* _p) {
	JSONProfile* p = container_of(_p, JSONProfile, profile);
	for (int i=0; i<=LAST_BUTTON_INDEX; i++)
		scc_action_compress(&p->buttons[i]);
	for (int i=0; i<=LAST_TRIGGER_INDEX; i++)
		scc_action_compress(&p->triggers[i]);
	for (int i=0; i<=LAST_AXIS_INDEX; i++)
		scc_action_compress(&p->axes[i]);
}

/** Creates new, empty profile with everything set to NoAction */
static JSONProfile* new_profile() {
	JSONProfile* p = malloc(sizeof(JSONProfile));
	if (p == NULL) return NULL;
	
	ASSERT(NoAction != NULL);
	for (int i=0; i<=LAST_BUTTON_INDEX; i++)
		p->buttons[i] = NoAction;
	for (int i=0; i<=LAST_TRIGGER_INDEX; i++)
		p->triggers[i] = NoAction;
	for (int i=0; i<=LAST_AXIS_INDEX; i++)
		p->axes[i] = NoAction;
	
	RC_INIT(&p->profile, &json_profile_dealloc);
	p->profile.type = PROFILE_TYPE_JSON;
	p->profile.compress = &compress;
	p->profile.get_button = &get_button;
	p->profile.get_trigger = &get_trigger;
	p->profile.get_pad = &get_pad;
	p->profile.get_stick = &get_stick;
	p->profile.get_gyro = &get_gyro;
	
	return p;
}


// Used in menu_data.c as well
long json_reader_fn(char* buffer, size_t len, void* reader_data) {
	return fread(buffer, 1, len, (FILE*)reader_data);
}

/**
 * Returns Action decoded from json node or NoAction if action
 * cannot be parsed
 */
static Action* decode_json_action(json_object* o) {
	if (o == NULL) return NoAction;
	
	char* actionstr = json_object_get_string(o, "action");
	if (actionstr == NULL) return NoAction;
	ActionOE aoe = scc_parse_action(actionstr);
	if (IS_ACTION_ERROR(aoe)) {
		WARN("Failed to decode: '%s': %s", actionstr, ACTION_ERROR(aoe)->message);
		RC_REL(ACTION_ERROR(aoe));
		return NoAction;
	}
	
	return ACTION(aoe);
}


Profile* scc_profile_from_json(const char* filename, int* error, bool apply_overrides) {
	// Open file
	FILE* fp = fopen(filename, "r");
	if (fp == NULL) {
		LERROR("Failed to open '%s': %s", filename, strerror(errno));
		if (error != NULL) *error = 1;
		return NULL;
	}
	
	// Parse JSON
	aojls_ctx_t* ctx;
	aojls_deserialization_prefs prefs = {
		.reader = &json_reader_fn,
		.reader_data = (void*)fp
	};
	ctx = aojls_deserialize(NULL, 0, &prefs);
	fclose(fp);
	if ((ctx == NULL) || (prefs.error != NULL)) {
		LERROR("Failed to decode '%s': %s", filename, prefs.error);
		if (error != NULL) *error = 2;
		json_free_context(ctx);
		return NULL;
	}
	
	// Create profile
	JSONProfile* p = new_profile();
	if (p == NULL) {
		LERROR("OOM");
		if (error != NULL) *error = 0;
		json_free_context(ctx);
		return NULL;
	}
	
	// Grab data from JSON
	json_object* root = json_as_object(json_context_get_result(ctx));
	if (root == NULL) {
		LERROR("Failed to decode '%s': root is not an object", filename);
		if (error != NULL) *error = 3;
		goto scc_profile_from_json_invalid_profile;
	}
	json_object* buttons = json_object_get_object(root, "buttons");
	if (buttons == NULL) {
		LERROR("Failed to decode '%s': root/buttons not found or not an object", filename);
		if (error != NULL) *error = 4;
		goto scc_profile_from_json_invalid_profile;
	}

	// TODO: generalize to *_overrides
	json_object* btn_overrides = json_object_get_object(root, "deck_btn_overrides");
	bool overrides_applied = false;

	// TODO: Convert "[LR]PAD" to "[LR]PADPRESS" here
	for (int i=0; i<=LAST_BUTTON_INDEX; i++) {
		if (button_names[i] == NULL) continue;
		// This replaces b->buttons[i] value without dereferencing original
		// value. That's generaly bad idea, but I know for sure that replaced
		// action is non-ref-counted NoAction.

		if(apply_overrides && btn_overrides != NULL 
			&& json_object_get_object(btn_overrides, button_names[i]) != NULL){
			overrides_applied = true;
			p->buttons[i] = decode_json_action(json_object_get_object(btn_overrides, button_names[i]));
		} else {
			p->buttons[i] = decode_json_action(json_object_get_object(buttons, button_names[i]));
		}
		if (scc_action_is_none(p->buttons[i])) {
			if ((i == scbutton_to_index(B_LPADPRESS)) || (i == scbutton_to_index(B_RPADPRESS))) {
				// Backwards compatibility thing - old name of [LR]PADPRESS
				// in buttons array was just [LR]PAD.
				char tmp[] = "LPAD";
				tmp[0] = button_names[i][0];
				p->buttons[i] = decode_json_action(json_object_get_object(buttons, tmp));
			}
		}
	}
	
	char axis_buf[MAX_AXIS_KEY_LEN + MAX_TYPE_KEY_LEN] = "";
	for (int i=0; i<=LAST_AXIS_INDEX; i++) {
		if (axis_names[i] == NULL) continue;
		
		// This replaces b->axes[i] value in same way as is done with buttons
		p->axes[i]= decode_json_action(json_object_get_object(root, axis_names[i]));
		if(apply_overrides){
			memset(axis_buf,0,MAX_AXIS_KEY_LEN+MAX_TYPE_KEY_LEN);
			json_object * axis_override = json_object_get_object(root, strcat(strcat(axis_buf, "deck_override_"), axis_names[i]));
			if(axis_override != NULL){
				if(json_object_numkeys(axis_override) == 0){
					WARN("Empty axis %s action applied.", axis_names[i]);
				}
				overrides_applied = true;
				p->axes[i] = decode_json_action(axis_override);
			}
		}
	}
	
	// And again, same thing with gyro & triggers
	p->triggers[0] = decode_json_action(json_object_get_object(root, "trigger_left"));
	if(apply_overrides){
		memset(axis_buf,0,MAX_AXIS_KEY_LEN+MAX_TYPE_KEY_LEN);
		json_object * trigger_override = json_object_get_object(root, strcat(strcat(axis_buf, "deck_override_"), "trigger_left"));
		if(trigger_override != NULL){
			if(json_object_numkeys(trigger_override) == 0){
				WARN("Empty trigger action applied.");
			}
			overrides_applied = true;
			p->triggers[0] = decode_json_action(trigger_override);
		}
	}

	p->triggers[1] = decode_json_action(json_object_get_object(root, "trigger_right"));
	if(apply_overrides){	
		memset(axis_buf,0,MAX_AXIS_KEY_LEN+MAX_TYPE_KEY_LEN);
		json_object * trigger_override = json_object_get_object(root, strcat(strcat(axis_buf, "deck_override_"), "trigger_right"));
		if(trigger_override != NULL){
			if(json_object_numkeys(trigger_override) == 0){
				WARN("Empty trigger action applied.");
			}
			overrides_applied = true;
			p->triggers[1] = decode_json_action(trigger_override);
		}
	}

	if(apply_overrides && !overrides_applied)
		INFO("No deck overrides applied.");
	
	json_free_context(ctx);
	return &p->profile;

scc_profile_from_json_invalid_profile:
	json_free_context(ctx);
	RC_REL(&p->profile);
	return NULL;
}


Profile* scc_make_empty_profile() {
	// There is no good reason why scc_make_empty_profile is defined in parser
	// nor why it reuses JSONProfile as base. On other hand, there is no reason
	// to move it away neither.
	JSONProfile* p = new_profile();
	if (p == NULL) return NULL;
	return &p->profile;
}
