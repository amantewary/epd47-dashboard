#!/usr/bin/env python3
"""Create EPD47 MQTT automations and print YAML for Home Assistant."""

import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRETS_FILE = os.path.join(SCRIPT_DIR, "examples", "ha_calendar", "secrets.h")
CONFIG_FILE = os.path.join(SCRIPT_DIR, "examples", "ha_calendar", "config.h")


def extract_define(filename, name):
    with open(filename) as f:
        for line in f:
            line = line.strip()
            m = re.match(r'#define\s+' + re.escape(name) + r'\s+(?:"([^"]*)"|(\S+))', line)
            if m:
                return m.group(1) if m.group(1) is not None else m.group(2)
    return None


def extract_entity_ids(filename, prefix, count_name):
    count = int(extract_define(filename, count_name) or 0)
    entities = []
    with open(filename) as f:
        content = f.read()
    for i in range(1, count + 1):
        m = re.search(r'#define\s+' + re.escape(prefix) + str(i) + r'\s+"([^"]+)"', content)
        if m:
            entities.append(m.group(1))
    return entities


def build_weather_yaml(weather_entity, prefix):
    return f"""alias: "EPD47 - Weather"
description: "Publish weather state to MQTT"
trigger:
  - platform: state
    entity_id: {weather_entity}
  - platform: homeassistant
    event: start
action:
  - service: mqtt.publish
    data:
      topic: {prefix}/weather
      payload: >-
        {{{{ {{ 'state': states('{weather_entity}'), 'temperature': state_attr('{weather_entity}', 'temperature') }} | tojson }}}}
      retain: true
      qos: 1
mode: single"""


def build_quotes_yaml(quote_entity, prefix):
    return f"""alias: "EPD47 - Quotes"
description: "Publish quote of the day to MQTT"
trigger:
  - platform: time
    at: "00:05:00"
  - platform: homeassistant
    event: start
action:
  - service: mqtt.publish
    data:
      topic: {prefix}/quotes
      payload: >-
        {{% set q = state_attr('{quote_entity}', 'quotes') or state_attr('{quote_entity}', 'entries') or [] %}}
        {{{{ q | tojson }}}}
      retain: true
      qos: 1
mode: single"""


def build_todos_yaml(todo_entities, prefix):
    if not todo_entities:
        return ""
    lines = [f'alias: "EPD47 - Todos"',
             'description: "Publish merged todo items to MQTT"',
             'trigger:',
             '  - platform: time_pattern',
             '    minutes: "/30"',
             '  - platform: homeassistant',
             '    event: start',
             'action:']
    for i, ent in enumerate(todo_entities):
        var = f"_t{i+1}"
        lines.append(f"  - service: todo.get_items")
        lines.append(f"    target:")
        lines.append(f"      entity_id: {ent}")
        lines.append(f"    data:")
        lines.append(f"      status: needs_action")
        lines.append(f"    response_variable: {var}")
    # Build merge expression
    parts = [f"{v}['{e}']['items']" for v, e in zip(
        [f"_t{i+1}" for i in range(len(todo_entities))], todo_entities)]
    merge_expr = " + ".join(parts)
    lines.append(f"  - service: mqtt.publish")
    lines.append(f"    data:")
    lines.append(f"      topic: {prefix}/todos")
    lines.append(f"      payload: >-")
    lines.append(f"        {{{{ ({merge_expr}) | tojson }}}}")
    lines.append(f"      retain: true")
    lines.append(f"      qos: 1")
    lines.append(f'mode: single')
    return "\n".join(lines)


def build_calendar_yaml(cal_entities, prefix):
    if not cal_entities:
        return ""
    lines = [f'alias: "EPD47 - Calendar"',
             'description: "Publish merged calendar events to MQTT"',
             'trigger:',
             '  - platform: time_pattern',
             '    minutes: "/30"',
             '  - platform: homeassistant',
             '    event: start',
             'action:']
    for i, ent in enumerate(cal_entities):
        var = f"_c{i+1}"
        lines.append(f"  - service: calendar.get_events")
        lines.append(f"    target:")
        lines.append(f"      entity_id: {ent}")
        lines.append(f"    data:")
        lines.append(f"      start_date_time: >-")
        lines.append(f"        {{{{ now().isoformat() }}}}")
        lines.append(f"      end_date_time: >-")
        lines.append(f"        {{{{ (now() + timedelta(days=7)).isoformat() }}}}")
        lines.append(f"    response_variable: {var}")
    parts = [f"{v}['{e}']['events']" for v, e in zip(
        [f"_c{i+1}" for i in range(len(cal_entities))], cal_entities)]
    merge_expr = " + ".join(parts)
    lines.append(f"  - service: mqtt.publish")
    lines.append(f"    data:")
    lines.append(f"      topic: {prefix}/calendar")
    lines.append(f"      payload: >-")
    lines.append(f"        {{{{ ({merge_expr}) | tojson }}}}")
    lines.append(f"      retain: true")
    lines.append(f"      qos: 1")
    lines.append(f'mode: single')
    return "\n".join(lines)


def main():
    print_yaml_only = "--yaml" in sys.argv[1:]

    weather_entity = extract_define(CONFIG_FILE, "ENTITY_WEATHER") or "weather.toronto_forecast"
    quote_entity = extract_define(CONFIG_FILE, "ENTITY_QUOTE") or "sensor.quote_of_the_day"
    prefix = extract_define(CONFIG_FILE, "MQTT_TOPIC_PREFIX") or "epd47"
    todo_entities = extract_entity_ids(CONFIG_FILE, "ENTITY_TODO_", "ENTITY_TODOS_COUNT")
    cal_entities = extract_entity_ids(CONFIG_FILE, "ENTITY_CALENDAR_", "ENTITY_CALENDARS_COUNT")

    docs = [
        build_weather_yaml(weather_entity, prefix),
        build_quotes_yaml(quote_entity, prefix),
    ]
    if todo_entities:
        docs.append(build_todos_yaml(todo_entities, prefix))
    if cal_entities:
        docs.append(build_calendar_yaml(cal_entities, prefix))

    if print_yaml_only:
        print("\n---\n".join(docs))
        return

    print("=" * 60)
    print("EPD47 Dashboard — MQTT Automation Generator")
    print("=" * 60)
    print(f"  Weather:  {weather_entity}")
    print(f"  Quotes:   {quote_entity}")
    print(f"  Todos:    {len(todo_entities)} entities")
    print(f"  Calendar: {len(cal_entities)} entities")
    print(f"  Prefix:   {prefix}")
    print()

    print("=" * 60)
    print("INSTRUCTIONS")
    print("=" * 60)
    print()
    print("For each automation below:")
    print("  1. HA → Settings → Automations → + Create Automation")
    print("  2. Click the three dots (⋮) → Edit in YAML")
    print("  3. Paste the YAML → Save")
    print()

    for index, doc in enumerate(docs, start=1):
        labels = [
            "Weather",
            "Quotes",
            "Todos",
            "Calendar",
        ]
        label = labels[index - 1]
        print("-" * 60)
        print(f"AUTOMATION {index}/{len(docs)}: EPD47 - {label}")
        print("-" * 60)
        print(doc)
        print()

    print("=" * 60)
    print("After creating all automations:")
    print("  Press reset on the device or wait 30 min")
    print("  Check device logs for: MQTT: done – received 4/4 topics")
    print("=" * 60)


if __name__ == "__main__":
    main()
