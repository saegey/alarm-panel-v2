#include <string.h>

#include "app_state.h"

AlarmLifecycleState parseAlarmLifecycleState(const char *state) {
  if (state == nullptr || state[0] == '\0') {
    return AlarmLifecycleState::Unknown;
  }
  if (strcmp(state, "disarmed") == 0) {
    return AlarmLifecycleState::Disarmed;
  }
  if (strcmp(state, "arming") == 0) {
    return AlarmLifecycleState::Arming;
  }
  if (strcmp(state, "armed_home") == 0) {
    return AlarmLifecycleState::ArmedHome;
  }
  if (strcmp(state, "armed_away") == 0) {
    return AlarmLifecycleState::ArmedAway;
  }
  if (strcmp(state, "armed_night") == 0) {
    return AlarmLifecycleState::ArmedNight;
  }
  if (strcmp(state, "armed_vacation") == 0) {
    return AlarmLifecycleState::ArmedVacation;
  }
  if (strcmp(state, "armed_custom_bypass") == 0) {
    return AlarmLifecycleState::ArmedCustomBypass;
  }
  if (strcmp(state, "pending") == 0) {
    return AlarmLifecycleState::Pending;
  }
  if (strcmp(state, "triggered") == 0) {
    return AlarmLifecycleState::Triggered;
  }
  if (strcmp(state, "open") == 0) {
    return AlarmLifecycleState::Open;
  }
  if (strcmp(state, "closed") == 0) {
    return AlarmLifecycleState::Closed;
  }
  if (strcmp(state, "opening") == 0) {
    return AlarmLifecycleState::Opening;
  }
  if (strcmp(state, "closing") == 0) {
    return AlarmLifecycleState::Closing;
  }
  return AlarmLifecycleState::Unknown;
}

const char *friendlyAlarmState(const char *state) {
  switch (parseAlarmLifecycleState(state)) {
    case AlarmLifecycleState::Open:
      return "Open";
    case AlarmLifecycleState::Closed:
      return "Closed";
    case AlarmLifecycleState::Opening:
      return "Opening";
    case AlarmLifecycleState::Closing:
      return "Closing";
    case AlarmLifecycleState::Disarmed:
      return "Disarmed";
    case AlarmLifecycleState::Arming:
      return "Arming";
    case AlarmLifecycleState::ArmedHome:
      return "Armed Home";
    case AlarmLifecycleState::ArmedAway:
      return "Armed Away";
    case AlarmLifecycleState::ArmedNight:
      return "Armed Night";
    case AlarmLifecycleState::ArmedVacation:
      return "Armed Vacation";
    case AlarmLifecycleState::ArmedCustomBypass:
      return "Armed Custom";
    case AlarmLifecycleState::Pending:
      return "Pending";
    case AlarmLifecycleState::Triggered:
      return "Triggered";
    case AlarmLifecycleState::Unknown:
      break;
  }

  if (state != nullptr && strcmp(state, "unknown") != 0) {
    return state;
  }
  return "Waiting";
}

lv_color_t alarmStateColor(const char *state) {
  switch (parseAlarmLifecycleState(state)) {
    case AlarmLifecycleState::Closed:
    case AlarmLifecycleState::Disarmed:
      return lv_color_hex(0x7BD389);
    case AlarmLifecycleState::Opening:
    case AlarmLifecycleState::Closing:
    case AlarmLifecycleState::Arming:
    case AlarmLifecycleState::Pending:
      return lv_color_hex(0xF2C14E);
    case AlarmLifecycleState::Unknown:
      return lv_color_hex(0xA8DADC);
    default:
      return lv_color_hex(0xE76F51);
  }
}

bool isTriggeredState(AlarmLifecycleState state) {
  return state == AlarmLifecycleState::Triggered;
}

bool isDisarmedState(const char *state) {
  return parseAlarmLifecycleState(state) == AlarmLifecycleState::Disarmed;
}

AlarmAction nextActionForTargetState(AlarmTarget target, AlarmLifecycleState state) {
  if (target == AlarmTarget::GarageDoor) {
    return state == AlarmLifecycleState::Closed ? AlarmAction::Arm : AlarmAction::Disarm;
  }

  return state == AlarmLifecycleState::Disarmed ? AlarmAction::Arm : AlarmAction::Disarm;
}
