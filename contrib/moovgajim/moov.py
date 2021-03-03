import subprocess
import struct
import threading
import queue
import time
import json


class Moov:

	def __init__(self):
		self._status_request_counter = 0
		self._proc = subprocess.Popen(
		    ['moov'],
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			universal_newlines=True
		)
		self._message_queue = queue.Queue()
		self._control_queue = queue.Queue()
		self._replies = dict()
		self._replies_lock = threading.Lock()
		self._reader_thread = threading.Thread(target=self._reader)
		self._reader_thread.start()

	def _write(self, v):
		self._proc.stdin.write(json.dumps(v))
		self._proc.stdin.write('\n')
		self._proc.stdin.flush()

	def _reader(self):
		for line in iter(self._proc.stdout.readline, ""):
			msg = json.loads(line)
			if msg['type'] == 'control':
				self._control_queue.put(msg)
			if msg['type'] == 'status':
				with self._replies_lock:
					self._replies[msg['request_id']] = msg
			if msg['type'] == 'user_input':
				self._message_queue.put(msg['text'])
		self._proc.stdout.close()

	def _request_status(self):
		request_id = self._status_request_counter
		self._status_request_counter += 1
		self._write({'type': 'request_status', 'request_id': request_id})
		return request_id

	def _await_reply(self, request_id):
		while True:
			with self._replies_lock:
				if request_id in self._replies:
					break
			time.sleep(0.01)
		reply = None
		with self._replies_lock:
			reply = self._replies[request_id]
			del self._replies[request_id]
		return reply

	def alive(self):
		return self._proc and self._proc.poll() is None

	def put_message(self, message, fg_color, bg_color):
		self._write({
			'type': 'message',
			'bg_color': bg_color,
			'fg_color': fg_color,
			'message': message
		})

	def index(self, position):
		self._write({'type': 'set_playlist_position', 'position': position})

	def previous(self):
		pl_pos = self.get_status()['playlist_position']
		if pl_pos - 1 >= 0:
			self.index(pl_pos - 1)

	def next(self):
		status = self.get_status()
		pl_pos = status['playlist_position']
		pl_count = status['playlist_count']
		if pl_pos + 1 < pl_count:
			self.index(pl_pos + 1)

	def append(self, path):
		self._write({'type': 'add_file', 'file_path': path})

	def clear_playlist(self):
		self._write({'type': 'playlist_clear'})

	def set_canonical(self, playlist_position, paused, time):
		self._write({
			'type': 'set_canonical',
			'playlist_position': playlist_position,
			'paused': paused,
			'time': time
		})

	def set_paused(self, paused):
		self._write({'type': 'pause', 'paused': paused})

	def toggle_paused(self):
		paused = self.get_status()['paused']
		self.set_paused(not paused)

	def seek(self, time):
		self._write({'type': 'seek', 'time': time})

	def relative_seek(self, time_delta):
		time = self.get_status()['time']
		self.seek(time + time_delta)

	def get_status(self):
		request_id = self._request_status()
		return self._await_reply(request_id)

	def get_user_inputs(self):
		inputs = list(self._message_queue.queue)
		self._message_queue.queue.clear()
		return inputs

	def get_user_control_commands(self):
		controls = list(self._control_queue.queue)
		self._control_queue.queue.clear()
		return controls

	def set_property(self, prop, value):
		self._write({
			'type': 'set_property',
			'property': prop,
			'value': value
		})

	def close(self):
		if self.alive():
			self._write({'type': 'close'})
			self._reader_thread.join()
