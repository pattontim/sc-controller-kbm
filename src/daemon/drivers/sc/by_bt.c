/**
 * Steam Controller Controller Steam Controller Driver
 *
 * Used to communicate with single Steam Controller
 * connected via bluetooth.
 */
#define LOG_TAG "sc_by_bt"
#include "scc/utils/logging.h"
#include "scc/utils/container_of.h"
#include "scc/utils/assert.h"
#include "scc/input_device.h"
#include "scc/input_test.h"
#include "scc/driver.h"
#include "scc/mapper.h"
#include "scc/tools.h"
#include "sc.h"
#include <stddef.h>
#include <stdio.h>

#define ENDPOINT			3
#define CONTROLIDX			-1
#define CHUNK_LENGTH		64
#define VENDOR_ID			0x28de
#define PRODUCT_ID			0x1106	// D0G (original Steam Controller)
#define TRITON_BLE_PID		0x1303	// Triton (Steam Controller 2025)
#define PACKET_SIZE 		20
#define LONG_PACKET 		0x80
#define BT_BUTTONS_BITS 	23

// Triton Report IDs (over BLE)
#define ID_TRITON_CONTROLLER_STATE_BLE		0x45
#define ID_TRITON_BATTERY_STATUS			0x43
#define ID_TRITON_WIRELESS_STATUS			0x79
#define ID_TRITON_WIRELESS_STATUS_X			0x46

static controller_available_cb controller_available = NULL;

enum BtInPacketType {
	BUTTON   = 0x0010,
	TRIGGERS = 0x0020,
	STICK    = 0x0080,
	LPAD     = 0x0100,
	RPAD     = 0x0200,
	GYRO     = 0x1800,
	PING     = 0x5000,
};

// Triton structures (for BLE)
typedef struct {
	uint32_t timestamp;
	int16_t sAccelX;
	int16_t sAccelY;
	int16_t sAccelZ;
	int16_t sGyroX;
	int16_t sGyroY;
	int16_t sGyroZ;
} TritonIMU_t;

typedef struct {
	uint8_t seq_num;
	uint32_t buttons;
	int16_t sTriggerLeft;
	int16_t sTriggerRight;
	int16_t sLeftStickX;
	int16_t sLeftStickY;
	int16_t sRightStickX;
	int16_t sRightStickY;
	int16_t sLeftPadX;
	int16_t sLeftPadY;
	uint16_t ucPressureLeft;
	int16_t sRightPadX;
	int16_t sRightPadY;
	uint16_t ucPressureRight;
	TritonIMU_t imu;
} TritonReport_t;

typedef struct {
	uint8_t ucChargeState;
	uint8_t ucBatteryLevel;
	uint16_t sBatteryVoltage;
	uint16_t sSystemVoltage;
	uint16_t sInputVoltage;
	uint16_t sCurrent;
	uint16_t sInputCurrent;
	uint16_t sTemperature;
} TritonBatteryStatus_t;

typedef struct {
	uint8_t state;
} TritonWirelessStatus_t;

static inline void debug_packet(char* buffer, size_t size) {
	size_t i;
	for(i=0; i<size; i++)
		printf("%02x", buffer[i] & 0xff);
	DDEBUG("\n");
}

/**
 * Maps Triton button bitmap to SC button bitmap
 */
static uint32_t triton_buttons_to_sc_buttons(uint32_t triton_buttons) {
	uint32_t sc_buttons = 0;
	
	// Action buttons
	if (triton_buttons & 0x00000001) sc_buttons |= SCB_A;
	if (triton_buttons & 0x00000002) sc_buttons |= SCB_B;
	if (triton_buttons & 0x00000004) sc_buttons |= SCB_X;
	if (triton_buttons & 0x00000008) sc_buttons |= SCB_Y;
	
	// Bumpers
	if (triton_buttons & 0x00080000) sc_buttons |= SCB_LB;
	if (triton_buttons & 0x00000200) sc_buttons |= SCB_RB;
	
	// Triggers (buttons)
	// Note: Triton uses stick press for L3/R3
	if (triton_buttons & 0x00008000) sc_buttons |= SCB_LT;
	if (triton_buttons & 0x00000020) sc_buttons |= SCB_RT;
	
	// Menu buttons
	if (triton_buttons & 0x00004000) sc_buttons |= SCB_BACK;
	if (triton_buttons & 0x00000040) sc_buttons |= SCB_START;
	if (triton_buttons & 0x00010000) sc_buttons |= SCB_C;
	
	// Paddles and special buttons (Triton-specific)
	if (triton_buttons & 0x00000010) sc_buttons |= SCB_QAM;
	if (triton_buttons & 0x00000080) sc_buttons |= SCB_R4;
	if (triton_buttons & 0x00020000) sc_buttons |= SCB_L4;
	if (triton_buttons & 0x00000100) sc_buttons |= SCB_R5;
	if (triton_buttons & 0x00040000) sc_buttons |= SCB_L5;
	
	// Stick and pad touches/clicks
	if (triton_buttons & 0x00002000) sc_buttons |= SCB_LPAD;
	if (triton_buttons & 0x00000800) sc_buttons |= SCB_RPAD;
	if (triton_buttons & 0x00001000) sc_buttons |= SCB_LGRIP;
	if (triton_buttons & 0x00000400) sc_buttons |= SCB_RGRIP;
	
	return sc_buttons;
}

/**
 * Handles Triton controller state report (0x45)
 * Complete state packet in single BLE notification
 */
static int triton_handle_input(SCController* sc, const uint8_t* data, size_t len) {
	if (len < sizeof(TritonReport_t) + 1) {
		DDEBUG("Triton report too short: %zu bytes", len);
		return 0;
	}
	
	TritonReport_t* report = (TritonReport_t*)&data[1];
	
	// Map buttons
	sc->input.buttons = triton_buttons_to_sc_buttons(report->buttons);
	
	// Triggers (0-32767 -> 0-255)
	sc->input.ltrig = (report->sTriggerLeft >> 7) & 0xFF;
	sc->input.rtrig = (report->sTriggerRight >> 7) & 0xFF;
	
	// Sticks
	sc->input.stick_x = report->sLeftStickX;
	sc->input.stick_y = report->sLeftStickY;
	
	// Pads with pressure sensitivity
	sc->input.lpad_x = report->sLeftPadX;
	sc->input.lpad_y = report->sLeftPadY;
	sc->input.rpad_x = report->sRightPadX;
	sc->input.rpad_y = report->sRightPadY;
	
	// IMU - convert raw to similar format as D0G
	// Gyro: raw to rad/s conversion
	sc->input.gyro.gpitch = report->imu.sGyroX;
	sc->input.gyro.groll = report->imu.sGyroY;
	sc->input.gyro.gyaw = report->imu.sGyroZ;
	
	// Set quaternion to identity (Triton doesn't send quaternion)
	sc->input.gyro.q0 = 16384;  // 1.0 in Q15 fixed point
	sc->input.gyro.q1 = 0;
	sc->input.gyro.q2 = 0;
	sc->input.gyro.q3 = 0;
	
	return 1;
}

char tmp_buffer[256] = "";
char ptr_buffer[256] = "";	

int bt_handle_input(SCController *sc, uint8_t* i){
	if(sc->mapper != NULL){			
			if (sc->long_packet) {
				// Previous packet had long flag set and this is its 2nd part
				memcpy(ptr_buffer + PACKET_SIZE, tmp_buffer + 1, PACKET_SIZE - 1);
				sc->long_packet = 0;
			} else {
				// This is 1st part of long packet
				memcpy(ptr_buffer, (void *)i, PACKET_SIZE);
				sc->long_packet = *((uint8_t*)(ptr_buffer + 1)) == LONG_PACKET;
				if (sc->long_packet) {
					return 0;
				}
				// debug_packet(ptr->buffer, PACKET_SIZE);
			}
			// debug_packet(ptr_buffer, PACKET_SIZE);
						
			int rv = 0;
			int bit;
			uint16_t type = *((uint16_t*)(ptr_buffer + 2));
			char* data = &ptr_buffer[4];
			if ((type & PING) == PING) {
				// PING packet does nothing
				return 0;
			}
			if ((type & BUTTON) == BUTTON) {
				rv = 1; 
				uint32_t bt_buttons = *((uint32_t*)data);
				uint32_t sc_buttons = 0;
				//TODO cover remaining bits
				for (bit=0; bit<BT_BUTTONS_BITS; bit++) {
					if ((bt_buttons & 1) != 0)
						sc_buttons |= BT_BUTTONS[bit];
					bt_buttons >>= 1;
				}
				sc->input.buttons = (SCButton)sc_buttons;
				data += 3;
			}
			if ((type & TRIGGERS) == TRIGGERS) {
				rv = 1;
				sc->input.ltrig = *(((uint8_t*)data) + 0);
				sc->input.rtrig = *(((uint8_t*)data) + 1);
				data += 2;
			}
			if ((type & STICK) == STICK) {
				rv = 1; 
				sc->input.stick_x = *(((int16_t*)data) + 0);
				sc->input.stick_y = *(((int16_t*)data) + 1);
				data += 4;
			}
			if ((type & LPAD) == LPAD) { 
				rv = 1; 
				sc->input.lpad_x = *(((int16_t*)data) + 0);
				sc->input.lpad_y = *(((int16_t*)data) + 1);
				data += 4;
			}
			if ((type & RPAD) == RPAD) {
				rv = 1;
				sc->input.rpad_x = *(((int16_t*)data) + 0);
				sc->input.rpad_y = *(((int16_t*)data) + 1);
				data += 4;
			}
			if ((type & GYRO) == GYRO) {
				rv = 1; 
				sc->input.gyro.gpitch = *(((int16_t*)data) + 0);
				sc->input.gyro.groll = *(((int16_t*)data) + 1);
				sc->input.gyro.gyaw = *(((int16_t*)data) + 2);
				sc->input.gyro.q0 = *(((int16_t*)data) + 3);
				sc->input.gyro.q1 = *(((int16_t*)data) + 4);
				sc->input.gyro.q2 = *(((int16_t*)data) + 5);
				sc->input.gyro.q3 = *(((int16_t*)data) + 6);
				data += 14;
			}
			sc->input.buttons &= ~0b00000000000011110000000000000000;
			//input eval same bitmap?
			// FIXME: gyro inputs are disabled for now
			//sc->mapper->input(sc->mapper, &sc->input);
			return rv;
		}
}

void input_interrupt_cb(Daemon* d, InputDevice* dev, uint8_t endpoint, const uint8_t* data, void* userdata) {
	SCController* sc = (SCController*)userdata;
	if (data == NULL) {
		// Means controller disconnected (or failed in any other way)
		DDEBUG("%s disconnected", sc->desc);
		// USBHelper* usb = d->get_usb_helper();
		// usb->close(sc->usb_hndl);
		// TODO: Calling close at this point may hang. Closing should be
		//       scheduled for later time instead, ideally in sccd_usb_dev_close.
		disconnected(sc);
		// TODO: Deallocate sc
		return;
	}
	
	int status = 0;
	
	// Update Triton lizard mode watchdog (needs periodic refresh every 3 seconds)
	if (sc->model == SC_MODEL_TRITON) {
		// This will update lizard mode if needed
		// TODO: Implement triton_check_lizard_mode_watchdog() in common.c
		// triton_check_lizard_mode_watchdog(sc);
	}
	
	// Handle Triton and D0G differently based on controller model
	if (sc->model == SC_MODEL_TRITON) {
		// Triton sends complete state in a single BLE notification
		// First byte is the report ID
		if (data[0] == ID_TRITON_CONTROLLER_STATE_BLE) {
			status = triton_handle_input(sc, data, PACKET_SIZE * 2);
		} else if (data[0] == ID_TRITON_BATTERY_STATUS) {
			// Battery status report - could be handled separately
			DDEBUG("Triton battery status report received");
			// TODO: Handle battery status
		} else if (data[0] == ID_TRITON_WIRELESS_STATUS || data[0] == ID_TRITON_WIRELESS_STATUS_X) {
			// Wireless status report
			DDEBUG("Triton wireless status report received");
			// TODO: Handle wireless status
		} else {
			DDEBUG("Unknown Triton report ID: 0x%02X", data[0]);
		}
	} else {
		// D0G (original Steam Controller) - uses chunked packets
		status = bt_handle_input(sc, (uint8_t*)data);
	}
	
	if(status == 1){
		//TODO input rotation support
		/*if (
			//self._input_rotation_l and 
			(self._state.type & 0x0100) != 0) {
			lx, ly = self._state.lpad_x, self._state.lpad_y
			//s, c = sin(self._input_rotation_l), cos(self._input_rotation_l)
			self._state.lpad_x = int(lx * c - ly * s)
			self._state.lpad_y = int(lx * s + ly * c)
		}
		if (
			//self._input_rotation_r and 
			(self._state.type & 0x0200) != 0) {
			rx, ry = self._state.rpad_x, self._state.rpad_y
			s, c = sin(self._input_rotation_r), cos(self._input_rotation_r)
			self._state.rpad_x = int(rx * c - ry * s)
			self._state.rpad_y = int(rx * s + ry * c)
		}*/
		
		sc->mapper->input(sc->mapper, &sc->input);

		//it's unlikely this is necessary
		//flush()
	} else if(status > 1) {
		DDEBUG("Read Failed");

		//TODO maybe should retry
		//self.close()
		//self.driver.retry(self.syspath)
	}	
}

static void turnoff(Controller* c) {
	// try to restart the process on controller turnoff
	DDEBUG("TURNING OFF CONTROLLER %s", c->get_description(c));
	/*
	SCController* sc = container_of(c, SCController, controller);
	uint8_t data[65] = { PT_BT_PREFIX, PT_LIZARD_BUTTONS, 0x01 };
	uint8_t* response;
	if ((response = sc->dev->hid_request(sc->dev, sc->idx, data, -65)) == NULL) {
		LERROR("Failed to activate lizard mode 1");
	}
	debug_packet((char *)response, 128);
	data[1] = PT_LIZARD_MOUSE;
	if ((response = sc->dev->hid_request(sc->dev, sc->idx, data, -65)) == NULL) {
		LERROR("Failed to activate lizard mode 1");
	}
	debug_packet((char *)response, 128);*/
	/*
	//uint8_t data[128] = { PT_BT_PREFIX, PT_OFF, 0x04, 0x6f, 0x66, 0x66, 0x21 };
	sc->dev->close(sc->dev);
	uint8_t* response;
	uint8_t* data; 
	data = calloc(128, sizeof(uint8_t));
		data[0] = PT_BT_PREFIX;
		data[1] = PT_OFF;
		data[2] = 0x04;
	    data[3] = 0x6f;
		data[4] = 0x66;
		data[5] = 0x66; 
		data[6] = 0x21;
	//if ((response = sc->dev->hid_request(sc->dev, sc->idx, data, -(65))) == NULL)
	//	LERROR("Failed to turn off controller");
	//debug_packet((char *)data, 128);*/
}

static bool hotplug_cb(Daemon* daemon, const InputDeviceData* idata) {
	if (controller_available != NULL) {
		controller_available("sc_by_bt", 9, idata);
		return true;
	}
	SCController* sc = NULL;
	
	char* product_str = idata->get_prop(idata, "product_id");  
    uint16_t product_id = 0;  
      
    if (product_str != NULL) {  
        product_id = (uint16_t)strtol(product_str, NULL, 16);  
        free(product_str);  
    }  	
	SCControllerModel controller_model = SC_MODEL_ORIGINAL;	// Default to original SC
	
	DDEBUG("%s (product ID: 0x%04x)", idata->path, product_id);
	
	// Determine controller model based on product ID
	if (product_id == TRITON_BLE_PID) {
		controller_model = SC_MODEL_TRITON;
		DEBUG("Detected Triton (Steam Controller 2025)");
	} else if (product_id == PRODUCT_ID) {
		controller_model = SC_MODEL_ORIGINAL;
		DEBUG("Detected D0G (Original Steam Controller)");
	} else {
		// Unknown product ID, still try to handle it as original SC
		WARN("Unknown BLE Steam Controller product ID: 0x%04x, assuming D0G", product_id);
		controller_model = SC_MODEL_ORIGINAL;
	}
	
	InputDevice* 	dev = idata->open(idata);
	if (dev == NULL) {
		LERROR("Failed to open '%s'", idata->path);
		return true;		// and nothing happens
	}
	// All BLE controllers use SC_BT connection type, differentiated by model
	if ((sc = create_usb_controller(daemon, dev, SC_BT, CONTROLIDX)) == NULL) {
		LERROR("Failed to allocate memory");
		goto hotplug_cb_fail;
	}
	sc->model = controller_model;
	
	if (dev->sys == USB) {
		if (dev->claim_interfaces_by(dev, 3, 0, 0) <= 0) {
			LERROR("Failed to claim interfaces");
			goto hotplug_cb_fail;
		}
	}
	//TODO fix serial grabbing
	/* For Bluetooth devices, serial reading may fail due to HID communication issues.
	   Try to read serial */
	if (!read_serial(sc)) {
		LERROR("Failed to read serial number for Bluetooth controller");
		goto hotplug_cb_failed_to_configure;
		// WARN("Failed to read serial number for Bluetooth controller, using default");
		/* Generate a synthetic serial based on vendor:product */
		// bad idea
		//snprintf(sc->serial, sizeof(sc->serial), "BT%04x%04x", VENDOR_ID, PRODUCT_ID);
	}
	// only needed on windows, do anyways on linux
	if (!clear_mappings(sc)) {
		LERROR("Failed to clear mappings on Bluetooth controller, continuing anyway");
		goto hotplug_cb_failed_to_configure;
	}
	if (!configure(sc)) {
		LERROR("Failed to configure Bluetooth controller via feature reports");
		goto hotplug_cb_failed_to_configure;
	}
	if (!dev->interupt_read_loop(dev, ENDPOINT, PACKET_SIZE, &input_interrupt_cb, sc)) {
		LERROR("Failed to setup interrupt read loop");
		goto hotplug_cb_failed_to_configure;
	}
	DEBUG("New BLE Steam Controller (%s) with serial %s connected", 
		  sc->model == SC_MODEL_TRITON ? "Triton" : "D0G", sc->serial);
	sc->state = SS_READY;
	if (!daemon->controller_add(&sc->controller)) {
		// This shouldn't happen unless memory is running out
		DEBUG("Failed to add controller to daemon");
		goto hotplug_cb_fail;
	}
	sc->controller.turnoff = &turnoff;
	return true;
hotplug_cb_failed_to_configure:
	LERROR("Failed to configure controlller");
hotplug_cb_fail:
	DEBUG("Failed to initialize controller");
	if (sc != NULL)
		free(sc);
	dev->close(dev);
	return true;
}

static bool driver_start(Driver* drv, Daemon* daemon) {
	HotplugFilter filter_vendor  = { .type=SCCD_HOTPLUG_FILTER_VENDOR,	.vendor=VENDOR_ID };
	HotplugFilter filter_product_d0g = { .type=SCCD_HOTPLUG_FILTER_PRODUCT,	.product=PRODUCT_ID };
	HotplugFilter filter_product_triton = { .type=SCCD_HOTPLUG_FILTER_PRODUCT,	.product=TRITON_BLE_PID };
	HotplugFilter filter_idx	 = { .type=SCCD_HOTPLUG_FILTER_IDX,		.idx=CONTROLIDX };
#if defined(_WIN32)
	// Windows: Register for both D0G and Triton
	#define FILTERS_D0G &filter_vendor, &filter_product_d0g, &filter_idx
	#define FILTERS_TRITON &filter_vendor, &filter_product_triton, &filter_idx
	if (!daemon->hotplug_cb_add(HIDAPI, hotplug_cb, FILTERS_D0G, NULL)) {
		LERROR("Failed to register hotplug callback for D0G");
		return false;
	}
	if (!daemon->hotplug_cb_add(HIDAPI, hotplug_cb, FILTERS_TRITON, NULL)) {
		LERROR("Failed to register hotplug callback for Triton");
		return false;
	}
#elif defined(__BSD__)
	// BSD: Register for both D0G and Triton
	#define FILTERS_D0G &filter_vendor, &filter_product_d0g, &filter_idx
	#define FILTERS_TRITON &filter_vendor, &filter_product_triton, &filter_idx
	if (!daemon->hotplug_cb_add(UHID, hotplug_cb, FILTERS_D0G, NULL)) {
		LERROR("Failed to register hotplug callback for D0G");
		return false;
	}
	if (!daemon->hotplug_cb_add(UHID, hotplug_cb, FILTERS_TRITON, NULL)) {
		LERROR("Failed to register hotplug callback for Triton");
		return false;
	}
#else
	/* Linux: Register for both HIDAPI and BT subsystems.
	   On Linux, Bluetooth devices detected via input subsystem are classified as:
	   - HIDAPI if use-hidapi is enabled (preferred for better compatibility)
	   - BT if use-hidapi is disabled */
	#define FILTERS_VENDOR &filter_vendor
	if (daemon->get_hidapi_enabled()) {
		if (!daemon->hotplug_cb_add(HIDAPI, hotplug_cb, FILTERS_VENDOR, NULL)) {
			LERROR("Failed to register hotplug callback for HIDAPI");
			return false;
		}
	}
	/* Also always register for BT subsystem for Bluetooth devices that might not be detected via HIDAPI */
	if (!daemon->hotplug_cb_add(BT, hotplug_cb, FILTERS_VENDOR, NULL)) {
		LERROR("Failed to register hotplug callback for BT");
		return false;
	}
#endif
	return true;
}

static void driver_list_devices(Driver* drv, Daemon* daemon, const controller_available_cb ca) {
	controller_available = ca;
	driver_start(drv, daemon);
}

static Driver driver = {
	.unload = NULL,
	.start = driver_start,
	// .list_devices = driver_list_devices,
};

Driver* scc_driver_init(Daemon* daemon) {
	ASSERT(sizeof(TriggerValue) == 1);
	ASSERT(sizeof(AxisValue) == 2);
	ASSERT(sizeof(GyroValue) == 2);
	// ^^ If any of above assertions fails, input_interrupt_cb code has to be
	//    modified so it doesn't use memcpy calls, as those depends on those sizes
	
	return &driver;
}

