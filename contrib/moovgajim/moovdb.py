import subprocess
import json
import html
import os.path
import os
import re

def download_info(url):
    p = subprocess.run(['youtube-dl', '-j', url], capture_output=True, encoding='utf-8')
    j = json.loads(p.stdout)
    return {
        'url': url,
        'title': j['title'],
        'uploader': j['uploader'] if 'uploader' in j else 'Unknown',
        'uploader_url': j['uploader_url'] if 'uploader_url' in j else None,
        'duration': j['duration'] if 'duration' in j else None
    }

def video_search(directory, keywords):
    if not os.path.isdir(directory):
        return None
    video_filetypes = ["mp4", "mkv", "avi", "ogm"]
    videos = []
    for root, subdirs, files in os.walk(directory):
        for f in files:
            if not f.split('.')[-1].lower() in video_filetypes:
                continue
            match = True
            for keyword in keywords:
                if not re.search(keyword, f, re.IGNORECASE):
                    match = False
                    break
            if match:
                videos.append(os.path.join(root, f))
    return sorted(videos, key=str.lower)

def session_playlist(video_directory, session):
    if session['type'] == 'url':
        return [session['video_info']['url']]
    if session['type'] == 'search':
        return video_search(video_directory, session['search'].split())

def format_time(time):
	s = int(round(time))
	h, s = s // 3600, s % 3600
	m, s = s // 60, s % 60
	return (f'{h}:{m:02}' if h else f'{m}') + f':{s:02}'

def format_link(url, text):
    return f'<a href="{html.escape(url)}">{html.escape(text)}</a>'

def format_search_session(index, session):
    return (
        f'[{index}] "{session["search"]}" '
        f'{session["playlist_position"] + 1}/{session["playlist_count"]} '
        f'{format_time(session["time"])}'
    )

def format_session_html(index, session):
    if session['type'] == 'url':
        info = session['video_info']
        index = html.escape(f'[{index}]')
        uploader = html.escape(info['uploader'])
        if info['uploader_url'] is not None:
            uploader = format_link(info['uploader_url'], info['uploader'])
        link = format_link(info['url'], info['title'])
        time = html.escape(format_time(session["time"]))
        return f'{index} {uploader}: {link} {time}'
    else:
        return html.escape(format_search_session(index, session))

def format_session_text(index, session):
    if session['type'] == 'url':
        info = session['video_info']
        index = f'[{index}]'
        uploader = info['uploader']
        title = info['title']
        time = format_time(session['time'])
        return f'{index} {uploader}: {title} {time}'
    else:
        return format_search_session(index, session)

def format_sessions_html(sessions):
    res = ''
    for i, s in enumerate(sessions):
        res += f'{format_session_html(i, s)} '
    return res

def format_sessions_text(sessions):
    res = ''
    for i, s in enumerate(sessions):
        res += f'{format_session_text(i, s)} '
    return res

class MoovDB:

    _db = []
    _session_counter = 0

    def __init__(self, save_path):
        self._save_path = save_path
        self._load(save_path)

    def _load(self, save_path):
        if os.path.isfile(save_path):
            with open(save_path, 'r') as fp:
                data = json.load(fp)
                self._db = data['db']
                self._session_counter = data['session_counter']

    def _save(self):
        # os.makedirs(os.path.dirname(self._save_path), exist_ok=True)
        with open(self._save_path, 'w+') as fp:
            data = {
                'db': self._db,
                'session_counter': self._session_counter
            }
            json.dump(data, fp, indent=4)

    def list(self):
        return self._db

    def add_url(self, video_info, time):
        for i, s in enumerate(self._db):
            if s['type'] == 'url' and s['video_info']['url'] == video_info['url']:
                return (i, s, True)
        self._db.append({
            'id': self._session_counter,
            'type': 'url',
            'video_info': video_info,
            'playlist_position': 0,
            'playlist_count': 1,
            'time': time})
        self._session_counter += 1
        self._save()
        return (len(self._db) - 1, self.top(), False)

    def add_search(self, search, files):
        for i, s in enumerate(self._db):
            if s['type'] == 'search' and s['search'] == search:
                return (i, s, True)
        self._db.append({
            'id': self._session_counter,
            'type': 'search',
            'search': search,
            'files': files,
            'playlist_position': 0,
            'playlist_count': len(files),
            'time': 0
        })
        self._session_counter += 1
        self._save()
        return (len(self._db) - 1, self.top(), False)

    def set_top(self, index):
        self._db.append(self._db.pop(index))
        self._save()
        return self.top()

    def index_of_id(self, id):
        for i, s in enumerate(self._db):
            if s['id'] == id:
                return i
        return None

    def get_session(self, id):
        index = self.index_of_id(id)
        if index is None:
            return None
        return self._db[index]

    def update_session(self, session_id, playlist_position, time):
        index = self.index_of_id(session_id)
        if index is not None:
            self._db[index]['playlist_position'] = playlist_position
            self._db[index]['time'] = time
            self._save()

    def top(self):
        return self._db[-1] if len(self._db) != 0 else None

    def pop(self, indices):
        if len(indices) == 0:
            if len(self._db) != 0:
                del self._db[-1]
        else:
            for index in sorted(indices, reverse=True):
                if 0 <= index < len(self._db):
                    del self._db[index]
        self._save()
