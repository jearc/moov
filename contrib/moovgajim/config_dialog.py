from gi.repository import GObject
from gi.repository import Gtk

from gajim.gtk.settings import SettingsDialog
from gajim.gtk.settings import ColorSetting
from gajim.gtk.settings import FileChooserSetting
from gajim.gtk.const import Setting
from gajim.gtk.const import SettingKind
from gajim.gtk.const import SettingType

from gajim.plugins.plugins_i18n import _


class MoovConfigDialog(SettingsDialog):
    def __init__(self, plugin, parent):
        self.plugin = plugin
        settings = [
            Setting(
                'MoovDirFileChooserSetting',
                _('Video directory'),
                SettingType.VALUE,
                self.plugin.config['VIDEO_DIR'],
                callback=self._on_setting,
                data='VIDEO_DIR',
                desc=_('Directory for local video search')
            ),
            Setting('UserFGColorSetting',
                    _('User message foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['USER_FG_COLOR'],
                    callback=self._on_setting,
                    data='USER_FG_COLOR',
                    desc=_('Foreground color for your messages'),
            ),
            Setting('UserBGColorSetting',
                    _('User message background color'),
                    SettingType.VALUE,
                    self.plugin.config['USER_BG_COLOR'],
                    callback=self._on_setting,
                    data='USER_BG_COLOR',
                    desc=_('Background color for your messages'),
            ),
            Setting('PartnerFGColorSetting',
                    _('Partner message foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['PARTNER_FG_COLOR'],
                    callback=self._on_setting,
                    data='PARTNER_FG_COLOR',
                    desc=_('Foreground color for your partner\'s messages'),
            ),
            Setting('PartnerBGColorSetting',
                    _('Partner message background color'),
                    SettingType.VALUE,
                    self.plugin.config['PARTNER_BG_COLOR'],
                    callback=self._on_setting,
                    data='PARTNER_BG_COLOR',
                    desc=_('Background color for your partner\'s messages'),
            ),
        ]

        extensions = [
            ('UserFGColorSetting', AlphaColorSetting),
            ('UserBGColorSetting', AlphaColorSetting),
            ('PartnerFGColorSetting', AlphaColorSetting),
            ('PartnerBGColorSetting', AlphaColorSetting),
            ('MoovDirFileChooserSetting', MoovFileChooserSetting)
        ]

        SettingsDialog.__init__(self, parent,
                                _('Moov Configuration'),
                                Gtk.DialogFlags.MODAL, settings, None,
                                extend=extensions)

    def _on_setting(self, value, data):
        if isinstance(value, str):
            value.strip()
        self.plugin.config[data] = value
        self.plugin.update()


class AlphaColorSetting(ColorSetting):

    def __init__(self, *args, **kwargs):
        ColorSetting.__init__(self, *args, **kwargs)
        self.color_button.set_use_alpha(True)


class MoovFileChooserSetting(FileChooserSetting):

    def __init__(self, *args, **kwargs):
        FileChooserSetting.__init__(self, filefilter=None, *args, **kwargs)
        file_picker_button = self.setting_box.get_children()[0]
        file_picker_button.set_action(Gtk.FileChooserAction.SELECT_FOLDER)
