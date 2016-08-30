#!/usr/bin/env python2
# Copyright (c) 2015 Stany MARCEL <stanypub@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

from threading import Timer
import struct, time, logging

from scc.lib import usb1
from scc.constants import VENDOR_ID, PRODUCT_ID, HPERIOD, LPERIOD, DURATION
from scc.constants import ENDPOINT, CONTROLIDX, FORMATS, ControllerInput
from scc.constants import SCStatus, SCButtons, HapticPos, SCPacketType
from scc.constants import SCConfigType

log = logging.getLogger("SCController")

class SCController(object):

	def __init__(self, callback):
		"""
		Constructor
		
		callback:
			function called on usb message 
			takes (SCController, current_time, ControllerInput) as arguments
		"""
		self._handle = None
		self._cb = callback
		self._cscallback = None		# Controller State Callback
		self._cmsg = []
		self._claimed = []
		self._ctx = usb1.USBContext()
		self._controller_connected = False
		self._idle_timeout = 600
		self._enable_gyros = False
		self._led_level = 80
		
		for i in range(len(PRODUCT_ID)):
			pid = PRODUCT_ID[i]
			endpoint = ENDPOINT[i]
			ccidx = CONTROLIDX[i]

			self._handle = self._ctx.openByVendorIDAndProductID(
				VENDOR_ID, pid,
				skip_on_error=True,
			)
			if self._handle is not None:
				break

		if self._handle is None:
			raise ValueError('Controller Device not found')

		self._ccidx = ccidx
		dev = self._handle.getDevice()
		cfg = dev[0]

		for inter in cfg:
			for setting in inter:
				number = setting.getNumber()
				if self._handle.kernelDriverActive(number):
					self._handle.detachKernelDriver(number)
				if (setting.getClass() == 3 and
					setting.getSubClass() == 0 and
					setting.getProtocol() == 0):
					self._handle.claimInterface(number)
					self._claimed.append(number)
		
		self._transfer_list = []
		transfer = self._handle.getTransfer()
		transfer.setInterrupt(
			usb1.ENDPOINT_IN | endpoint,
			64,
			callback=self._processReceivedData,
		)
		transfer.submit()
		self._transfer_list.append(transfer)

		self._period = LPERIOD

		if pid == 0x1102:
			self._timer = Timer(LPERIOD, self._callbackTimer)
			self._timer.start()
		else:
			self._timer = None

		self._tup = None
		self._lastusb = time.time()
	
	
	def reset(self):
		self.unclaim()
		self._handle.resetDevice()
	
	
	def unclaim(self):
		for number in self._claimed:
			self._handle.releaseInterface(number)
		self._claimed = []
	
	
	def setStatusCallback(self, callback):
		"""
		Sets callback that is called when controller status is changed.
		callback:
			takes (SCController, turned_on) as arguments.
		"""
		self._cscallback = callback
	
	
	def __del__(self):
		if self._handle:
			self._handle.close()

	def _sendControl(self, data, timeout=0):

		zeros = b'\x00' * (64 - len(data))
		
		self._handle.controlWrite(request_type=0x21,
								  request=0x09,
								  value=0x0300,
								  index=self._ccidx,
								  data=data + zeros,
								  timeout=timeout)

	def addFeedback(self, position, amplitude=128, period=0, count=1):
		"""
		Add haptic feedback to be send on next usb tick

		@param int position	 haptic to use 1 for left 0 for right
		@param int amplitude	signal amplitude from 0 to 65535
		@param int period	   signal period from 0 to 65535
		@param int count		number of period to play
		"""
		self._cmsg.insert(0, struct.pack('<BBBHHH',
				SCPacketType.FEEDBACK, 0x07, position,
				amplitude, period, count))
	
	def _processReceivedData(self, transfer):
		"""Private USB async Rx function"""
		
		if (transfer.getStatus() != usb1.TRANSFER_COMPLETED or
			transfer.getActualLength() != 64):
			return
		
		data = transfer.getBuffer()
		tup = ControllerInput._make(struct.unpack('<' + ''.join(FORMATS), data))
		if tup.status == SCStatus.HOTPLUG:
			transfer.submit()
			state, = struct.unpack('<xxxxB59x', data)
			self._controller_connected = (state == 2)
			if self._cscallback:
				self._cscallback(self, self._controller_connected)
				self.configure()
		elif tup.status == SCStatus.INPUT:
			self._tup = tup
			self._callback()
			transfer.submit()
			if not self._controller_connected:
				self._set_send_controller_connected()
		elif not self._controller_connected and tup.status == SCStatus.IDLE:
			transfer.submit()
			self._set_send_controller_connected()
		else:
			transfer.submit()
	
	
	def _set_send_controller_connected(self):
		"""
		Sets self._controller_connected to true and calls callback, if set.
		Exists simply because above metod needs it on two places.
		"""
		self._controller_connected = True
		if self._cscallback:
			self._cscallback(self, self._controller_connected)
			self.configure()
	
	
	def _callback(self):
		self._lastusb = time.time()
		
		self._cb(self, self._lastusb, self._tup)
		self._period = HPERIOD
	
	
	def _callbackTimer(self):
		t = time.time()
		d = t - self._lastusb
		self._timer.cancel()
		
		if d > DURATION:
			self._period = LPERIOD
		
		self._timer = Timer(self._period, self._callbackTimer)
		self._timer.start()
		
		if self._tup is None:
			return
		
		if d < HPERIOD:
			return
		
		self._cb(self, t, self._tup)
	
	
	def configure(self, idle_timeout=None, enable_gyros=None, led_level=None):
		"""
		Sets and, if possible, sends configuration to controller.
		Only value that is provided is changed.
		'idle_timeout' is in seconds.
		'led_level' is precent (0-100)
		"""
		# ------
		"""
		packet format:
		 - uint8_t type - SCPacketType.CONFIGURE
		 - uint8_t size - 0x03 for led configuration, 0x15 for timeout & gyros
		 - uint8_t config_type - one of SCConfigType
		 - 61b data
		
		Format for data when configuring controller:
		 - uint16	timeout
		 - 13b		unknown1 - (0x18, 0x00, 0x00, 0x31, 0x02, 0x00, 0x08, 0x07, 0x00, 0x07, 0x07, 0x00, 0x30)
		 - uint8	enable gyro sensor - 0x14 enables, 0x00 disables
		 - 2b		unknown2 - (0x00, 0x2e)
		 - 43b		unused
		 
		Format for data when configuring led:
		 - uint8	led
		 - 60b		unused
		"""
		
		if idle_timeout is not None : self._idle_timeout = idle_timeout
		if enable_gyros is not None : self._enable_gyros = enable_gyros
		if led_level is not None: self._led_level = led_level
		
		if not self._controller_connected:
			# Can't configure what's not there
			return
		
		unknown1 = b'\x18\x00\x001\x02\x00\x08\x07\x00\x07\x07\x000'
		unknown2 = b'\x00\x2e'
		timeout1 = self._idle_timeout & 0x00FF
		timeout2 = (self._idle_timeout & 0xFF00) >> 8
		led_lvl  = min(100, self._led_level) & 0xFF
		
		# Timeout & Gyros
		self._cmsg.insert(0, struct.pack('>BBBBB13sB2s43x',
			SCPacketType.CONFIGURE,
			0x15, # size
			SCConfigType.TIMEOUT_N_GYROS,
			timeout1, timeout2,
			unknown1,
			0x14 if self._enable_gyros else 0,
			unknown2))
		
		# LED
		self._cmsg.insert(0, struct.pack('>BBBB59x',
			SCPacketType.CONFIGURE,
			0x03,
			SCConfigType.LED,
			led_lvl))
	
	
	def getGyroEnabled(self):
		""" Returns True if gyroscope input is currently enabled """
		return self._enable_gyros
	
	
	def turnoff(self):
		log.debug("Turning off the controller...")
		
		# Mercilessly stolen from scraw library
		self._cmsg.insert(0, struct.pack('<BBBBBB',
				SCPacketType.OFF, 0x04, 0x6f, 0x66, 0x66, 0x21))
	
	
	def run(self):
		"""Fucntion to run in order to process usb events"""
		if self._handle:
			try:
				while any(x.isSubmitted() for x in self._transfer_list):
					self._ctx.handleEvents()
					while len(self._cmsg) > 0:
						cmsg = self._cmsg.pop()
						self._sendControl(cmsg)
				try:
					# Normally, code reaches here only when usb dongle gets stuck
					# in some weird "yes, Im here but don't talk to me" state.
					# Reseting fixes it.
					self.reset()
					log.info("Performed dongle reset")
				except Exception:
					# Unless code reaches here because dongle was removed.
					pass
			except usb1.USBErrorInterrupted, e:
				log.error(e)
				pass
			finally:
				self.unclaim()


	def handleEvents(self):
		"""
		Fucntion to run in order to process usb events.
		Shouldn't be combined with run().
		"""
		if self._handle and self._ctx:
			while len(self._cmsg) > 0:
				cmsg = self._cmsg.pop()
				self._sendControl(cmsg)
			self._ctx.handleEvents()


class HapticData(object):
	""" Simple container to hold haptic feedback settings """
	
	def __init__(self, position, amplitude=512, frequency=4, period=1024, count=1):
		"""
		'frequency' is used only when emulating touchpad and describes how many
		pixels should mouse travell between two feedback ticks.
		"""
		data = tuple([ int(x) for x in (position, amplitude, period, count) ])
		if data[0] not in (HapticPos.LEFT, HapticPos.RIGHT, HapticPos.BOTH):
			raise ValueError("Invalid position")
		for i in (1,2,3):
			if data[i] > 0x8000 or data[i] < 0:
				raise ValueError("Value out of range")
		# frequency is multiplied by 1000 just so I don't have big numbers everywhere;
		# it's float until here, so user still can make pad squeak if he wish
		frequency = int(max(1.0, frequency * 1000.0))
		
		self.data = data				# send to controller
		self.frequency = frequency		# used internally
	
	
	def with_position(self, position):
		""" Creates copy of HapticData with position value changed """
		trash, amplitude, period, count = self.data
		return HapticData(position, amplitude, self.frequency, period, count)
	
	
	def get_position(self):
		return HapticPos(self.data[0])
	
	def get_amplitude(self):
		return self.data[1]
	
	def get_frequency(self):
		return float(self.frequency) / 1000.0
	
	def get_period(self):
		return self.data[2]
	
	def get_count(self):
		return self.data[3]
	
	def __mul__(self, by):
		"""
		Allows multiplying HapticData by scalar to get same values
		with increased amplitude.
		"""
		position, amplitude, period, count = self.data
		amplitude = min(amplitude * by, 0x8000)
		return HapticData(position, amplitude, self.frequency, period, count)
