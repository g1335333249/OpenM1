from pathlib import Path

manager = Path('openm1/mqtt_manager.c').read_text()
reader = Path('openm1/mqtt_bounded_read.c').read_text()
header = Path('openm1/mqtt_bounded_read.h').read_text()
script = Path('scripts/build_recovery.sh').read_text()

assert 'SO_RCVTIMEO' not in manager
assert 'SO_SNDTIMEO' not in manager
assert 'select(socket+1,&readfds' in manager
assert 'network->mqttread=openm1_mqtt_read' in manager
assert 'options.keepAliveInterval=30' in manager
assert 'MQTTYield(&client,200)' in manager
assert '#define OPENM1_MQTT_INBOUND_MAX 1024u' in header
assert 'OPENM1_MQTT_INBOUND_MAX-state->remaining' in reader
assert 'return got; /* No bytes is an ordinary MQTT idle read. */' in reader
for stage in ('MQTT_STAGE_MQTT_CONNECT', 'MQTT_STAGE_AVAILABILITY_PUBLISH',
              'MQTT_STAGE_HA_DISCOVERY', 'MQTT_STAGE_STATE_PUBLISH'):
    assert f'record_failure({stage}' in manager
for name in ('mqtt_select_bounded_read', 'mqtt_socket_receive_timeout_ms'):
    assert name in Path('tools/generate_manifest.py').read_text()
assert 'test_mqtt_bounded_read.c' in script
assert 'test_mqtt_diagnostics.c' in script
print('[PASS] v0.6.7 MQTT bounded read and failure diagnostics static checks')
