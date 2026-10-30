#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the bounded scan list using OrangeFox's existing themed widgets."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
rows = '\n'.join(f'''      <listitem name="%dodge_wifi_row_{i}%">
        <condition var1="dodge_wifi_count" op="&gt;" var2="{i}"/>
        <action function="wifi">select:{i}</action>
      </listitem>''' for i in range(32))
xml = f'''<?xml version="1.0"?>
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<recovery>
  <pages>
    <page name="dodge_wifi">
      <template name="base"/>
      <text style="text_ab_title"><placement x="%col1_x_indent%" y="%ab_bc_y%"/><text>Wi-Fi</text></text>
      <text style="text_body1_dark"><placement x="%col1_x_caption%" y="%row1_1_y%"/><text>%dodge_wifi_summary%</text></text>
      <text style="text_body2_dark"><placement x="%col1_x_caption%" y="%row1_2_y%"/><text>%dodge_wifi_ip%</text></text>
      <button style="btn_raised_hl">
        <condition var1="dodge_wifi_enabled" var2="0"/>
        <placement x="%center_x%" y="%row2_1_y%" placement="4"/>
        <text>{{@wifi_enable=Enable Wi-Fi}}</text>
        <action function="wifiprepare"/>
        <action function="wifi">enable</action>
      </button>
      <listbox style="group_list_flat">
        <condition var1="dodge_wifi_enabled" var2="1"/>
        <placement x="0" y="%row2_1_y%" w="%screen_w%" h="%dodge_wifi_list_h%"/>
        <iconsize w="48" h="120"/>
        <data name="dodge_wifi_generation" requireReload="1"/>
        <listitem name="{{@wifi_scan=Refresh networks}}"><action function="wifi">scan</action></listitem>
        <listitem name="{{@wifi_disconnect=Disconnect}}"><action function="wifi">disconnect</action></listitem>
        <listitem name="{{@wifi_disable=Turn Wi-Fi off}}"><action function="wifi">disable</action></listitem>
{rows}
      </listbox>
      <template name="gestures"/>
      <action><touch key="back"/><action function="page">settings</action></action>
      <action><touch key="home"/><action function="page">main</action></action>
    </page>
    <page name="dodge_wifi_connect">
      <template name="base_key"/>
      <text style="text_ab_title"><placement x="%col1_x_indent%" y="%ab_bc_y%"/><text>Wi-Fi</text></text>
      <text style="text_body1_dark"><placement x="%col1_x_caption%" y="%row1_1_y%"/><text>%dodge_wifi_selected%</text></text>
      <text style="text_body2_dark"><placement x="%col1_x_caption%" y="%row1_2_y%"/><text>%dodge_wifi_security%</text></text>
      <text style="text_body1_dark">
        <condition var1="dodge_wifi_need_password" var2="1"/>
        <placement x="%col1_x_caption%" y="%row2_1_y%"/><text>{{@wifi_password=Password}}</text>
      </text>
      <input>
        <condition var1="dodge_wifi_need_password" var2="1"/>
        <condition var1="dodge_wifi_show_password" var2="0"/>
        <placement x="%col1_x_caption%" y="%row2_2_y%" w="%input_w%" h="%input_height%"/>
        <font resource="body1" color="%text%"/>
        <data name="dodge_wifi_password" mask="*"/>
        <restrict minlen="0" maxlen="64"/>
        <action function="wifi">connect</action>
      </input>
      <input>
        <condition var1="dodge_wifi_need_password" var2="1"/>
        <condition var1="dodge_wifi_show_password" var2="1"/>
        <placement x="%col1_x_caption%" y="%row2_2_y%" w="%input_w%" h="%input_height%"/>
        <font resource="body1" color="%text%"/>
        <data name="dodge_wifi_password"/>
        <restrict minlen="0" maxlen="64"/>
        <action function="wifi">connect</action>
      </input>
      <checkbox style="checkbox">
        <condition var1="dodge_wifi_need_password" var2="1"/>
        <placement x="%col1_x_caption%" y="%row3_1_y%"/>
        <text>{{@wifi_show_password=Show password}}</text>
        <data variable="dodge_wifi_show_password"/>
      </checkbox>
      <text style="text_body2_dark"><placement x="%col1_x_caption%" y="%row3_2_y%"/><text>%dodge_wifi_input_error%</text></text>
      <button style="btn_raised_hl">
        <condition var1="dodge_wifi_supported" var2="1"/>
        <condition var1="dodge_wifi_valid_password" var2="1"/>
        <placement x="%center_x%" y="%row4_1_y%" placement="4"/>
        <text>{{@wifi_connect=Connect}}</text><action function="wifi">connect</action>
      </button>
      <button style="btn_raised">
        <placement x="%center_x%" y="%row5_1_y%" placement="4"/>
        <text>{{@cancel_btn=Cancel}}</text><action function="wifi">cancel</action>
      </button>
      <template name="gestures_key"/>
      <action><touch key="back"/><action function="wifi">cancel</action></action>
      <action><touch key="home"/><action function="wifi">cancel</action><action function="page">main</action></action>
    </page>
    <page name="dodge_wifi_unsupported">
      <template name="base"/>
      <text style="text_ab_title"><placement x="%col1_x_indent%" y="%ab_bc_y%"/><text>Wi-Fi</text></text>
      <text style="text_body1_dark"><placement x="%col1_x_caption%" y="%row1_1_y%"/><text>%dodge_wifi_selected%</text></text>
      <text style="text_body2_dark"><placement x="%col1_x_caption%" y="%row2_1_y%"/><text>{{@wifi_unsupported=This security type is not supported}}</text></text>
      <button style="btn_raised_hl"><placement x="%center_x%" y="%row3_1_y%" placement="4"/><text>{{@back_btn=Back}}</text><action function="wifi">cancel</action></button>
      <template name="gestures"/>
      <action><touch key="back"/><action function="wifi">cancel</action></action>
      <action><touch key="home"/><action function="page">main</action></action>
    </page>
  </pages>
</recovery>
'''
(root / 'wifi' / 'wifi.xml').write_text(xml)
