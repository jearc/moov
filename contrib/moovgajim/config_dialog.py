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
            Setting('UIBGColorSetting',
                    _('UI background color'),
                    SettingType.VALUE,
                    self.plugin.config['UI_BG_COLOR'],
                    callback=self._on_setting,
                    data='UI_BG_COLOR',
                    desc=_('Background color for Moov\'s UI'),
            ),
            Setting('UITextColorSetting',
                    _('UI text color'),
                    SettingType.VALUE,
                    self.plugin.config['UI_TEXT_COLOR'],
                    callback=self._on_setting,
                    data='UI_TEXT_COLOR',
                    desc=_('Text color for Moov\'s UI'),
            ),
            Setting('ButtonColorSetting',
                    _('Button color'),
                    SettingType.VALUE,
                    self.plugin.config['BUTTON_COLOR'],
                    callback=self._on_setting,
                    data='BUTTON_COLOR',
                    desc=_('Color of buttons in Moov\'s UI'),
            ),
            Setting('HoveredButtonColorSetting',
                    _('Hovered button color'),
                    SettingType.VALUE,
                    self.plugin.config['BUTTON_HOVERED_COLOR'],
                    callback=self._on_setting,
                    data='BUTTON_HOVERED_COLOR',
                    desc=_('Color of hovered buttons in Moov\'s UI'),
            ),
            Setting('PressedButtonColorSetting',
                    _('Pressed button color'),
                    SettingType.VALUE,
                    self.plugin.config['BUTTON_PRESSED_COLOR'],
                    callback=self._on_setting,
                    data='BUTTON_PRESSED_COLOR',
                    desc=_('Color of pressed buttons in Moov\'s UI'),
            ),
            Setting('ButtonLabelColorSetting',
                    _('Button label color'),
                    SettingType.VALUE,
                    self.plugin.config['BUTTON_LABEL_COLOR'],
                    callback=self._on_setting,
                    data='BUTTON_LABEL_COLOR',
                    desc=_('Color of pressed buttons in Moov\'s UI'),
            ),
            Setting('SeekBarBGColorSetting',
                    _('Seek bar background color'),
                    SettingType.VALUE,
                    self.plugin.config['SEEK_BAR_BG_COLOR'],
                    callback=self._on_setting,
                    data='SEEK_BAR_BG_COLOR',
                    desc=_('Seek bar background color for Moov\'s UI'),
            ),
            Setting('InactiveSeekBarFGColorSetting',
                    _('Inactive seek bar foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['SEEK_BAR_FG_INACTIVE_COLOR'],
                    callback=self._on_setting,
                    data='SEEK_BAR_FG_INACTIVE_COLOR',
                    desc=_('Inactive seek bar foreground color for Moov\'s UI'),
            ),
            Setting('ActiveSeekBarFGColorSetting',
                    _('Active seek bar foreground color'),
                    SettingType.VALUE,
                    self.plugin.config['SEEK_BAR_FG_ACTIVE_COLOR'],
                    callback=self._on_setting,
                    data='SEEK_BAR_FG_ACTIVE_COLOR',
                    desc=_('Active seek bar foreground color for Moov\'s UI'),
            ),
            Setting('SeekBarNotchColorSetting',
                    _('Seek bar notch color'),
                    SettingType.VALUE,
                    self.plugin.config['SEEK_BAR_NOTCH_COLOR'],
                    callback=self._on_setting,
                    data='SEEK_BAR_NOTCH_COLOR',
                    desc=_('Seek bar notch color for Moov\'s UI'),
            ),
            Setting('SeekBarTextColorSetting',
                    _('Seek bar text color'),
                    SettingType.VALUE,
                    self.plugin.config['SEEK_BAR_TEXT_COLOR'],
                    callback=self._on_setting,
                    data='SEEK_BAR_TEXT_COLOR',
                    desc=_('Seek bar text color for Moov\'s UI'),
            ),
        ]

        extensions = [
            ('MoovDirFileChooserSetting', MoovFileChooserSetting),
            ('UserFGColorSetting', AlphaColorSetting),
            ('UserBGColorSetting', AlphaColorSetting),
            ('PartnerFGColorSetting', AlphaColorSetting),
            ('PartnerBGColorSetting', AlphaColorSetting),
            ('UIBGColorSetting', AlphaColorSetting),
            ('UITextColorSetting', AlphaColorSetting),
            ('ButtonColorSetting', AlphaColorSetting),
            ('HoveredButtonColorSetting', AlphaColorSetting),
            ('PressedButtonColorSetting', AlphaColorSetting),
            ('ButtonLabelColorSetting', AlphaColorSetting),
            ('SeekBarBGColorSetting', AlphaColorSetting),
            ('InactiveSeekBarFGColorSetting', AlphaColorSetting),
            ('ActiveSeekBarFGColorSetting', AlphaColorSetting),
            ('SeekBarNotchColorSetting', AlphaColorSetting),
            ('SeekBarTextColorSetting', AlphaColorSetting),
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


class MoovFileChooserSetting(FileChooserSetting):

    def __init__(self, *args, **kwargs):
        FileChooserSetting.__init__(self, filefilter=None, *args, **kwargs)
        file_picker_button = self.setting_box.get_children()[0]
        file_picker_button.set_action(Gtk.FileChooserAction.SELECT_FOLDER)
