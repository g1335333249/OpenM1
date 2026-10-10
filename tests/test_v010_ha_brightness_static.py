from pathlib import Path

ha=Path('openm1/homeassistant.c').read_text()
mqtt=Path('openm1/mqtt_manager.c').read_text()
display=Path('openm1/m1_display.c').read_text()
page=Path('openm1/recovery_page.html').read_text()
parser=Path('openm1/ha_brightness.c').read_text()
assert 'static const ha_entity_t entities[4]' in ha
for sensor in ('temperature','humidity','pm25','formaldehyde'):
    assert f'{{"{sensor}"' in ha
assert '/number/%s/brightness/config' in ha
for field in ('屏幕亮度','%s_brightness','%s/brightness/set','value_json.brightness',
              '\\"min\\":0','\\"max\\":4','\\"step\\":1','\\"mode\\":\\"slider',
              'availability_topic','payload_available','payload_not_available','OpenM1 v0.6.17'):
    assert field in ha, field
assert 'if (remove) payload[0]=0' in ha
assert ha.count('MQTTPublish(client,topic,&message)')==2
assert 'MQTTSubscribe(&client,brightness_topic,QOS0,brightness_message_callback)' in mqtt
assert mqtt.index('MQTTConnect(&client,&options)')<mqtt.index('MQTTSubscribe(&client,brightness_topic')<mqtt.index('publish(&client,availability,"online",1)')
callback=mqtt.split('static void brightness_message_callback')[1].split('static openm1_mqtt_read_state_t')[0]
for forbidden in ('MQTTPublish','MQTTSubscribe','m1_display_set_brightness','openm1_log','config_store_save'):
    assert forbidden not in callback, forbidden
assert 'data->message->payloadlen' in callback
assert 'length!=1' in parser
assert 'm1_display_set_brightness(level)' in mqtt
assert 'rc=publish_state(&client,&current)' in mqtt
assert 'ha_brightness_take(&brightness_command,recovery_ota_busy(),&level)' in mqtt
assert 'm1_display_get_status(&display)' in mqtt
assert 'ha_brightness_format_state(payload,sizeof(payload)' in mqtt
assert '\\"brightness\\":%u' in parser
assert 'brightness_control_subscribed' in mqtt and 'subscribe_fail_count' in mqtt
assert 'MQTT_STAGE_SUBSCRIBE' in mqtt
assert '"entity_count\\":5' in mqtt or '\\"entity_count\\":5' in mqtt
assert 'h.entity_count||5' in page
assert 'recovery_ota_busy()' in display
assert '#define OPENM1_CONFIG_VERSION 3u' in Path('openm1/config_store.h').read_text()
assert 'WIFI_BOOT_AUTO_CONNECT_GRACE_MS 60000u' in Path('openm1/wifi_station_logic.h').read_text()
assert '0.6.17' in Path('openm1/main.c').read_text()
print('V010_HA_BRIGHTNESS_STATIC_PASS')
