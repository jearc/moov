#!/usr/bin/env python3

import re
from moov import Moov
import time
from functools import reduce


def parse_time(string):
	ns = re.findall(r'-?\d+', string)
	return reduce(lambda t, n: 60*t + int(n), ns[:3], 0)


def format_time(time):
	s = int(round(time))
	h, s = s // 3600, s % 3600
	m, s = s // 60, s % 60
	return (f'{h}:{m:02}' if h else f'{m}') + f':{s:02}'


def format_status(status):
	s = f'{status["pl_pos"]+1}/{status["pl_count"]} '
	s += 'paused' if status['paused'] else 'playing'
	s += f'␣{format_time(status["time"])}'
	return s


files = [
    'https://www.twitch.tv/videos/709133837',
    'https://www.twitch.tv/videos/709163445'
]
start_time = 5940
start_pos = 1

moov = Moov()

for f in files:
	moov.append(f)
moov.index(start_pos)
moov.seek(start_time)

while moov.alive():
	messages = moov.get_user_inputs()
	for msg in messages:
		moov.put_message(msg, 0xfff0cf89, 0xbb000000)

		if msg == "status":
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "pp":
			moov.toggle_paused()
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "play":
			moov.set_paused(False)
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "pause":
			moov.set_paused(True)
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg[0:5] == "seek ":
			moov.seek(parse_time(msg[5:]))
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg[0:6] == "seek+ ":
			moov.relative_seek(parse_time(msg[5:]))
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg[0:6] == "seek- ":
			moov.relative_seek(-parse_time(msg[5:]))
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg[0:5] == "index":
			prog = re.compile(r'(-?\d+)')
			position = int(prog.findall(msg[6:])[0]) - 1
			moov.index(position)
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "next":
			moov.next()
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "prev":
			moov.previous()
			status = moov.get_status()
			moov.put_message(format_status(status), 0xff00ffff, 0xbb000000)

		if msg == "close":
			moov.close()

	time.sleep(0.01)
