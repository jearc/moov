from gi.repository import GObject
from gi.repository import Gtk

from gajim.gtk.settings import SettingsDialog
from gajim.gtk.settings import ColorSetting
from gajim.gtk.settings import FileChooserSetting
from gajim.gtk.const import Setting
from gajim.gtk.const import SettingKind
from gajim.gtk.const import SettingType

from gajim.plugins.plugins_i18n import _

color_properties = {
	'ui_bg_color': (
		'rgba(0, 0, 0, 53)',
		_('UI background color'),
		_('Background color for Moov\'s UI')
	),
	'ui_text_color': (
		'rgba(255, 255, 255, 100)',
		_('UI text color'),
		_('Text color for Moov\'s UI')
	),
	'button_color': (
		'rgba(255, 0, 0, 100)',
		_('Button color'),
		_('Color of buttons in Moov\'s UI')
	),
	'button_hovered_color': (
		'rgba(0, 255, 0, 100)',
		_('Hovered button color'),
		_('Color of hovered buttons in Moov\'s UI')
	),
	'button_pressed_color': (
		'rgba(0, 0, 255, 100)',
		_('Pressed button color'),
		_('Color of pressed buttons in Moov\'s UI')
	),
	'button_label_color': (
		'rgba(255, 255, 255, 100)',
		_('Button label color'),
		_('Color of button labels in Moov\'s UI')
	),
	'seek_bar_bg_color': (
		'rgba(136, 136, 136, 53)',
		_('Seek bar background color'),
		_('Seek bar background color for Moov\'s UI')
	),
	'seek_bar_fg_inactive_color': (
		'rgba(255, 170, 0, 53)',
		_('Inactive seek bar foreground color'),
		_('Inactive seek bar foreground color for Moov\'s UI')
	),
	'seek_bar_fg_active_color': (
		'rgba(255, 170, 0, 100)',
		_('Active seek bar foreground color'),
		_('Active seek bar foreground color for Moov\'s UI')
	),
	'seek_bar_notch_color': (
		'rgba(0, 0, 0, 100)',
		_('Seek bar notch color'),
		_('Seek bar notch color for Moov\'s UI')
	),
	'seek_bar_text_color': (
		'rgba(255, 255, 255, 100)',
		_('Seek bar text color'),
		_('Seek bar text color for Moov\'s UI')
	),
}

class MoovConfigDialog(SettingsDialog):
    def __init__(self, plugin, parent):
        self.plugin = plugin

        qualities = [
            ('best', _('Best')),
            ('1080p', '1080p'),
            ('720p', '720p'),
            ('480p', '480p'),
            ('240p', '240p'),
            ('144p', '144p'),
        ]

        settings = [
            Setting(
                'DirectoryChooserSetting',
                _('Video directory'),
                SettingType.VALUE,
                self.plugin.config['VIDEO_DIR'],
                callback=self._on_setting,
                data='VIDEO_DIR',
                desc=_('Directory for local video search')
            ),
            Setting(
                SettingKind.COMBO,
                _('Preferred maximum stream quality'),
                SettingType.VALUE,
                self.plugin.config['preferred_maximum_stream_quality'],
                callback=self._on_setting,
                data='preferred_maximum_stream_quality',
                desc=_('Preferred maximum quality for internet videos'),
                props={'combo_items': qualities},
            ),
            Setting('AlphaColorSetting',
                    _('User message foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['USER_FG_COLOR'],
                    callback=self._on_setting,
                    data='USER_FG_COLOR',
                    desc=_('Foreground color for your messages'),
            ),
            Setting('AlphaColorSetting',
                    _('User message background color'),
                    SettingType.VALUE,
                    self.plugin.config['USER_BG_COLOR'],
                    callback=self._on_setting,
                    data='USER_BG_COLOR',
                    desc=_('Background color for your messages'),
            ),
            Setting('AlphaColorSetting',
                    _('Partner message foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['PARTNER_FG_COLOR'],
                    callback=self._on_setting,
                    data='PARTNER_FG_COLOR',
                    desc=_('Foreground color for your partner\'s messages'),
            ),
            Setting('AlphaColorSetting',
                    _('Partner message background color'),
                    SettingType.VALUE,
                    self.plugin.config['PARTNER_BG_COLOR'],
                    callback=self._on_setting,
                    data='PARTNER_BG_COLOR',
                    desc=_('Background color for your partner\'s messages'),
            ),
        ]

        for p in color_properties:
            settings.append(Setting(
                'AlphaColorSetting',
                color_properties[p][1],
                SettingType.VALUE,
                self.plugin.config[p],
                callback=self._on_setting,
                data=p,
                desc=color_properties[p][2]
            ))

        extensions = [
            ('DirectoryChooserSetting', DirectoryChooserSetting),
            ('AlphaColorSetting', AlphaColorSetting),
        ]

        SettingsDialog.__init__(self, parent,
                                _('Moov Configuration'),
                                Gtk.DialogFlags.MODAL, settings, None,
                                extend=extensions)

    def _on_setting(self, value, data):
        if isinstance(value, str):
            value.strip()
        self.plugin.config[data] = value
        self.plugin.update(data)


class AlphaColorSetting(ColorSetting):

    def __init__(self, *args, **kwargs):
        ColorSetting.__init__(self, *args, **kwargs)
        self.color_button.set_use_alpha(True)


class DirectoryChooserSetting(FileChooserSetting):

    def __init__(self, *args, **kwargs):
        FileChooserSetting.__init__(self, filefilter=None, *args, **kwargs)
        file_picker_button = self.setting_box.get_children()[0]
        file_picker_button.set_action(Gtk.FileChooserAction.SELECT_FOLDER)
