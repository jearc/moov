from threading import Thread
import re
from functools import reduce, partial
from pathlib import Path
import time
from gajim.common import app
from gajim.common import ged
from gajim.plugins import GajimPlugin
from gajim.plugins.plugins_i18n import _
from gajim.common.structs import OutgoingMessage
from gajim.common import configpaths
from gi.repository import GLib
from gi.repository import Gdk
from nbxmpp.modules.misc import build_xhtml_body
import moovgajim.moov as moov
import moovgajim.moovdb as moovdb
from moovgajim.config_dialog import MoovConfigDialog
from moovgajim.config_dialog import color_properties
import os

lor_pattern = re.compile(r'^\s*"([^"]+)"\s+(\d+)\s+((\d+:)?(\d+:)?\d+)\s*$')
set_pattern = re.compile(r'^\s*(\d+)\s+(paused|playing)\s+((\d+:)?(\d+:)?\d+)\s*$')

def parse_time(string):
	ns = re.findall(r'-?\d+', string)
	return reduce(lambda t, n: 60*t + int(n), ns[:3], 0)


def format_time(time):
	s = int(round(time))
	h, s = s // 3600, s % 3600
	m, s = s // 60, s % 60
	return (f'{h}:{m:02}' if h else f'{m}') + f':{s:02}'


def format_status(status):
	playlist = f'{status["playlist_position"]+1}/{status["playlist_count"]}'
	paused = 'paused' if status['paused'] else 'playing'
	time = format_time(status["time"])
	delay = round(status['delay'])
	delay_str = f'{"-" if delay > 0 else "+"}{abs(delay)}'
	return f'{playlist} {paused} {time} {delay_str}'


def convert_color(rgba_str):
	r = Gdk.RGBA()
	r.parse(rgba_str)
	return '#%02x%02x%02x%02x' % (
		round(r.red * 255),
		round(r.green * 255),
		round(r.blue * 255),
		round(r.alpha * 255)
	)


class Conversation:

	def __init__(self, account, contact, conn):
		self._account = account
		self._contact = contact
		self._conn = conn

	def send(self, text, xhtml=None):
		if xhtml is not None:
			xhtml = build_xhtml_body(xhtml)
		def f(text):
			message = OutgoingMessage(self._account, self._contact, text, 'chat', xhtml=xhtml)
			self._conn.send_message(message)
		GLib.idle_add(f, text)


class MoovPlugin(GajimPlugin):

	moov_thread = None
	moov = None
	conv = None
	session_id = None
	db = None

	def init(self):
		self.description = _('Adds Moov support to Gajim')
		self.config_dialog = partial(MoovConfigDialog, self)
		self.config_default_values = {
			'VIDEO_DIR': (
				None,
				'Directory for local video search'),
			'USER_FG_COLOR': (
				'rgba(255, 255, 191, 100)',
				'Foreground color for your messages'),
			'USER_BG_COLOR': (
				'rgba(0, 0, 0, 80)',
				'Background color for your messages'),
			'PARTNER_FG_COLOR': (
				'rgba(191, 255, 255, 100)',
				'Foreground color for your partner\'s messages'),
			'PARTNER_BG_COLOR': (
				'rgba(0, 0, 0, 80)',
				'Background color for your partner\'s messages'),
		}
		for p in color_properties:
			self.config_default_values[p] = (color_properties[p][0], color_properties[p][2])

		self.events_handlers = {
			'decrypted-message-received': (ged.PREGUI, self._on_message_received),
			'message-sent': (ged.PREGUI, self._on_message_sent),
		}
		db_path = Path(configpaths.get('PLUGINS_DATA')) / 'moov' / 'db.json'
		# self.db = moovdb.MoovDB(db_path)

	def update(self, data):
		if self.moov is not None and self.moov.alive():
			if data in color_properties:
				self.moov.set_property(data, convert_color(self.config[data]))


	def _on_message_received(self, event):
		if not event.msgtxt:
			return

		contact = app.contacts.get_contact(event.account, event.jid)
		conv = Conversation(event.account, contact, event.conn)
		self.relay_message(event.msgtxt, False)
		self.handle_command(conv, event.msgtxt)

	def _on_message_sent(self, event):
		if not event.message:
			return
		if not event.control:
			return

		conv = Conversation(
			event.control.account,
			event.control.contact,
			event.control.connection
		)

		self.relay_message(event.message, True)
		self.handle_command(conv, event.message)

	def send_message(self, conv, text, xhtml=None, command=False):
		conv.send(text, xhtml=xhtml)
		self.relay_message(text)
		if command:
			self.handle_command(conv, text)

	def handle_command(self, conv, message):
		tokens = message.split()

		alive = self.moov is not None and self.moov.alive()
		db_enabled = self.db is not None

		if tokens[0] == '.add' and db_enabled:
			try:
				url = tokens[1]
				time = 0 if len(tokens) < 3 else parse_time(tokens[2])
			except:
				conv.send('error: invalid args')

			def cb(info):
				(index, session, dupe) = self.db.add_url(info, time)
				prefix = 'already have ' if dupe else 'added '
				text = prefix + moovdb.format_session_text(index, session)
				xhtml = prefix + moovdb.format_session_html(index, session)
				conv.send(text, xhtml=xhtml)
				self.relay_message(text)

			download_thread = Thread(target=self.download_info, args=[url, cb, conv])
			download_thread.start()
		elif tokens[0] == '.o':
			try:
				url = tokens[1]
				time = 0 if len(tokens) < 3 else parse_time(tokens[2])
			except:
				self.send_message(conv, 'error: invalid args')

			def cb(info):
				if self.db is not None:
					(index, session, dupe) = self.db.add_url(info, time)
					self.db.set_top(index)
				self.conv = conv
				self.open_moov()
				self.moov.append(url)
				self.moov.seek(time)
				self.send_message(conv, format_status(self.moov.get_status()))

			download_thread = Thread(target=self.download_info, args=[url, cb, conv])
			download_thread.start()
		elif tokens[0] == '.ladd' and db_enabled:
			search = message[6:]

			results = moovdb.video_search(self.config['VIDEO_DIR'], search)
			if results is None:
				self.send_message(conv, 'error: no video directory set')
				return
			if len(results) == 0:
				self.send_message(conv, 'no matches found')
				return

			(index, session, dupe) = self.db.add_search(search, len(results))
			self.db.set_top(index)
			text = "added " + moovdb.format_session_text(index, session)
			xhtml = "added " + moovdb.format_session_html(index, session)
			self.send_message(conv, text, xhtml=xhtml)
		elif tokens[0] == '.lor':
			match = lor_pattern.match(message[5:])
			if match is None:
				self.send_message(conv, 'error: invalid args')
				return
			search = match.group(1)
			playlist_position = int(match.group(2)) - 1
			time = parse_time(match.group(3))
			results = moovdb.video_search(self.config['VIDEO_DIR'], search)
			if results is None:
				self.send_message(conv, 'error: no video directory set')
				return
			if len(results) == 0:
				conv.send('no matches found')
				return
			self.conv = conv
			self.open_moov()
			for video_file in results:
				self.moov.append(video_file)
			self.moov.index(playlist_position)
			self.moov.seek(time)
			self.send_message(conv, format_status(self.moov.get_status()))
		elif tokens[0] == '.list' and db_enabled:
			session_list = self.db.list()
			if len(session_list) != 0:
				text = moovdb.format_sessions_text(self.db.list())
				xhtml = moovdb.format_sessions_html(self.db.list())
				self.send_message(conv, text, xhtml=xhtml)
			else:
				self.send_message(conv, 'no sessions')
		elif tokens[0] == '.pop' and db_enabled:
			indices = tokens[1:]
			for i in range(len(indices)):
				indices[i] = int(indices[i])
			if len(indices) == 0 and alive:
				indices.append(self.db.index_of_id(self.session_id))
				self.kill_moov()
			self.db.pop(indices)
			if len(self.db.list()) != 0:
				text = moovdb.format_sessions_text(self.db.list())
				xhtml = moovdb.format_sessions_html(self.db.list())
				self.send_message(conv, text, xhtml=xhtml)
			else:
				self.send_message(conv, 'database empty')
		elif tokens[0] == '.pop' and not db_enabled:
			indices = tokens[1:]
			if len(indices) == 0 and alive:
				self.kill_moov()
		elif tokens[0] == '.resume' and db_enabled:
			if len(tokens) >= 2:
				try:
					self.db.set_top(int(tokens[1]))
				except:
					return

			session = self.db.top()
			if session is None:
				return

			self.conv = conv
			self.open_moov()
			self.session_id = session['id']
			if session['type'] == 'url':
				self.moov.append(session['video_info']['url'])
				self.moov.seek(session['time'])
				self.send_message(conv, f'.o {session["video_info"]["url"]} {format_time(session["time"])}')
				self.send_message(conv, format_status(self.moov.get_status()))
			elif session['type'] == 'search':
				results = moovdb.video_search(self.config['VIDEO_DIR'], session['search'])
				if len(results) == 0 or len(results) != session['playlist_count']:
					conv.send('error: videos deleted')
					return
				self.conv = conv
				for video_file in results:
					self.moov.append(video_file)
				self.moov.index(session['playlist_position'])
				self.moov.seek(session['time'])
				self.send_message(conv, f'.lor "{session["search"]}" {session["playlist_position"]+1} {format_time(session["time"])}')
				self.send_message(conv, format_status(self.moov.get_status()))
		elif tokens[0] == '.status' and alive:
			self.send_message(conv, format_status(self.moov.get_status()))
		elif tokens[0] == '.status' and not alive:
			self.send_message(conv, 'nothing playing')
		elif tokens[0] == 'pp' and alive:
			self.moov.toggle_paused()
			self.send_message(conv, format_status(self.moov.get_status()))
			self.update_db()
		elif tokens[0] == '.seek' and alive:
			self.moov.seek(parse_time(message[6:]))
			self.send_message(conv, format_status(self.moov.get_status()))
			self.update_db()
		elif tokens[0] == '.seek+' and alive:
			self.moov.relative_seek(parse_time(message[7:]))
			self.send_message(conv, format_status(self.moov.get_status()))
			self.update_db()
		elif tokens[0] == '.seek-' and alive:
			self.moov.relative_seek(-parse_time(message[7:]))
			self.send_message(conv, format_status(self.moov.get_status()))
			self.update_db()
		elif tokens[0] == '.set' and alive:
			match = set_pattern.match(message[5:])
			if match is not None:
				playlist_position = int(match.group(1)) - 1
				paused = match.group(2) == 'paused'
				time = parse_time(match.group(3))
				self.moov.set_canonical(playlist_position, paused, time)
				self.send_message(conv, format_status(self.moov.get_status()))
				self.update_db()
			else:
				conv.send('error: invalid args')
		elif tokens[0] == '.close' and alive:
			self.update_db()
			self.kill_moov()
		elif tokens[0] == '.re' and db_enabled and alive:
			session = self.db.get_session(self.session_id)
			moov_status = self.moov.get_status()
			time_str = format_time(moov_status['time'])
			if session['type'] == 'url':
				self.send_message(conv, f'.o {self.video_url} {time_str}')
			elif session['type'] == 'search':
				playlist_position = moov_status['playlist_position'] + 1
				search = session['search']
				self.send_message(conv, f'.lor "{search}" {playlist_position} {time_str}')


	def download_info(self, url, callback, conv):
		try:
			info = moovdb.download_info(url)
			GLib.idle_add(callback, info)
		except:
			GLib.idle_add(self.send_message, conv, 'error: could not get video information')

	def handle_control(self, control_command):
		p = control_command['playlist_position'] + 1
		t = format_time(control_command['time'])
		pp = 'paused' if control_command['paused'] else 'playing'
		message = f'.set {p} {pp} {t}'
		self.send_message(self.conv, message)

	def open_moov(self):
		self.kill_moov()
		self.moov = moov.Moov()
		self.moov_thread = Thread(target=self.moov_thread_f)
		self.moov_thread.start()
		for p in color_properties:
			self.moov.set_property(p, convert_color(self.config[p]))

	def update_db(self):
		if self.db is not None and self.session_id is not None:
			moov_status = self.moov.get_status()
			playlist_position = moov_status['playlist_position']
			time = moov_status['time']
			self.db.update_session(self.session_id, playlist_position, time)

	def moov_thread_f(self):
		last_update = time.time()
		while self.moov is not None and self.moov.alive():
			now = time.time()
			if now - last_update > 5:
				GLib.idle_add(self.update_db)
				last_update = now
			for user_input in self.moov.get_user_inputs():
				GLib.idle_add(self.send_message, self.conv, user_input)
			for control_command in self.moov.get_user_control_commands():
				GLib.idle_add(self.handle_control, control_command)
			time.sleep(0.01)
		if self.moov is not None:
			self.kill_moov()

	def relay_message(self, message, own=True):
		if self.moov and self.moov.alive():
			fg = convert_color(self.config['USER_FG_COLOR' if own else 'PARTNER_FG_COLOR'])
			bg = convert_color(self.config['USER_BG_COLOR' if own else 'PARTNER_BG_COLOR'])
			self.moov.put_message(message, fg, bg)

	def kill_moov(self):
		self.session_id = None
		if not self.moov:
			return
		self.moov.close()
		self.moov = None
		self.moov_thread = None
