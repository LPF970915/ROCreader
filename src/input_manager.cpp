#include "input_manager.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>

const char *ButtonName(Button b) {
  switch (b) {
  case Button::Up: return "Up";
  case Button::Down: return "Down";
  case Button::Left: return "Left";
  case Button::Right: return "Right";
  case Button::A: return "A";
  case Button::B: return "B";
  case Button::X: return "X";
  case Button::Y: return "Y";
  case Button::Menu: return "Menu";
  case Button::L1: return "L1";
  case Button::L2: return "L2";
  case Button::R1: return "R1";
  case Button::R2: return "R2";
  case Button::Start: return "Start";
  case Button::Select: return "Select";
  case Button::VolUp: return "VolUp";
  case Button::VolDown: return "VolDown";
  default: return "Invalid";
  }
}

const char *SdlEventName(Uint32 type) {
  switch (type) {
  case SDL_KEYDOWN: return "SDL_KEYDOWN";
  case SDL_KEYUP: return "SDL_KEYUP";
  case SDL_CONTROLLERBUTTONDOWN: return "SDL_CONTROLLERBUTTONDOWN";
  case SDL_CONTROLLERBUTTONUP: return "SDL_CONTROLLERBUTTONUP";
  case SDL_CONTROLLERAXISMOTION: return "SDL_CONTROLLERAXISMOTION";
  case SDL_JOYBUTTONDOWN: return "SDL_JOYBUTTONDOWN";
  case SDL_JOYBUTTONUP: return "SDL_JOYBUTTONUP";
  case SDL_JOYHATMOTION: return "SDL_JOYHATMOTION";
  case SDL_JOYAXISMOTION: return "SDL_JOYAXISMOTION";
  default: return "SDL_EVENT_UNKNOWN";
  }
}

InputManager::InputManager(const std::string &mapping_path, bool h700_defaults) {
  pad_map_.fill(InvalidButton());
  joy_map_.fill(InvalidButton());
  LoadDefaultPadMap();
  LoadDefaultJoyMap(h700_defaults);
  LoadOverrides(mapping_path);
}

void InputManager::BeginFrame(float dt) {
  dt_ = dt;
  for (auto &s : states_) {
    s.just_pressed = false;
    s.just_released = false;
    s.repeated = false;
    s.long_pressed = false;
  }
}

void InputManager::HandleEvent(const SDL_Event &e) {
  if (e.type == SDL_KEYDOWN && !e.key.repeat) {
    const Button mapped = KeyToButton(e.key.keysym.sym);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " sym=" << SDL_GetKeyName(e.key.keysym.sym)
              << " keycode=" << static_cast<int>(e.key.keysym.sym)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: keydown sym=" << SDL_GetKeyName(e.key.keysym.sym)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, true);
  } else if (e.type == SDL_KEYUP) {
    const Button mapped = KeyToButton(e.key.keysym.sym);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " sym=" << SDL_GetKeyName(e.key.keysym.sym)
              << " keycode=" << static_cast<int>(e.key.keysym.sym)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: keyup sym=" << SDL_GetKeyName(e.key.keysym.sym)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, false);
  } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
    const Button mapped = PadToButton(e.cbutton.button);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " button=" << static_cast<int>(e.cbutton.button)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: pad down button=" << static_cast<int>(e.cbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, true);
  } else if (e.type == SDL_CONTROLLERBUTTONUP) {
    const Button mapped = PadToButton(e.cbutton.button);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " button=" << static_cast<int>(e.cbutton.button)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: pad up button=" << static_cast<int>(e.cbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, false);
  } else if (e.type == SDL_CONTROLLERAXISMOTION) {
    constexpr int kDeadzone = 16000;
    const int axis = e.caxis.axis;
    const int val = static_cast<int>(e.caxis.value);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " axis=" << axis
              << " value=" << val << "\n";
    if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_RIGHTX) {
      SetDown(Button::Left, val < -kDeadzone);
      SetDown(Button::Right, val > kDeadzone);
    } else if (axis == SDL_CONTROLLER_AXIS_LEFTY || axis == SDL_CONTROLLER_AXIS_RIGHTY) {
      SetDown(Button::Up, val < -kDeadzone);
      SetDown(Button::Down, val > kDeadzone);
    }
  } else if (e.type == SDL_JOYBUTTONDOWN) {
    const Button mapped = JoyButtonToButton(e.jbutton.button);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " button=" << static_cast<int>(e.jbutton.button)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: joy down button=" << static_cast<int>(e.jbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, true);
  } else if (e.type == SDL_JOYBUTTONUP) {
    const Button mapped = JoyButtonToButton(e.jbutton.button);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " button=" << static_cast<int>(e.jbutton.button)
              << " mapped=" << ButtonName(mapped) << "\n";
    if (mapped == Button::VolUp || mapped == Button::VolDown) {
      std::cout << "[native_h700] volume event: joy up button=" << static_cast<int>(e.jbutton.button)
                << " mapped=" << ButtonName(mapped) << "\n";
    }
    SetDown(mapped, false);
  } else if (e.type == SDL_JOYHATMOTION) {
    const uint8_t v = e.jhat.value;
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " hat=" << static_cast<int>(e.jhat.hat)
              << " value=" << static_cast<int>(v) << "\n";
    SetDown(Button::Up, (v & SDL_HAT_UP) != 0);
    SetDown(Button::Down, (v & SDL_HAT_DOWN) != 0);
    SetDown(Button::Left, (v & SDL_HAT_LEFT) != 0);
    SetDown(Button::Right, (v & SDL_HAT_RIGHT) != 0);
  } else if (e.type == SDL_JOYAXISMOTION) {
    constexpr int kDeadzone = 16000;
    const int axis = e.jaxis.axis;
    const int val = static_cast<int>(e.jaxis.value);
    std::cout << "[native_h700] raw input: type=" << SdlEventName(e.type)
              << " axis=" << axis
              << " value=" << val << "\n";
    if (axis == 0 || axis == 6) {
      SetDown(Button::Left, val < -kDeadzone);
      SetDown(Button::Right, val > kDeadzone);
    } else if (axis == 1 || axis == 7) {
      SetDown(Button::Up, val < -kDeadzone);
      SetDown(Button::Down, val > kDeadzone);
    }
  }
}

void InputManager::EndFrame() {
  for (auto &s : states_) {
    if (!s.down) {
      s.hold_time = 0.0f;
      s.repeat_timer = 0.0f;
      s.repeat_active = false;
      continue;
    }

    s.hold_time += dt_;
    if (s.hold_time >= 0.8f) s.long_pressed = true;

    if (!s.repeat_active) {
      s.repeat_active = true;
      s.repeat_timer = 0.4f;
      s.repeated = true;
    } else {
      s.repeat_timer -= dt_;
      if (s.repeat_timer <= 0.0f) {
        s.repeated = true;
        s.repeat_timer = 0.1f;
      }
    }
  }
}

bool InputManager::IsPressed(Button b) const { return Get(b).down; }
bool InputManager::IsJustPressed(Button b) const { return Get(b).just_pressed; }
bool InputManager::IsJustReleased(Button b) const { return Get(b).just_released; }
bool InputManager::IsRepeated(Button b) const { return Get(b).repeated; }
bool InputManager::IsLongPressed(Button b) const { return Get(b).long_pressed; }
float InputManager::HoldTime(Button b) const { return Get(b).hold_time; }

bool InputManager::AnyPressed() const {
  for (const auto &s : states_) {
    if (s.down) return true;
  }
  return false;
}

void InputManager::ResetAll() {
  for (auto &s : states_) {
    s = BtnState{};
  }
}

Button InputManager::InvalidButton() { return static_cast<Button>(-1); }

bool InputManager::IsValid(Button b) {
  const int i = static_cast<int>(b);
  return i >= 0 && i < kButtonCount;
}

Button InputManager::KeyToButton(SDL_Keycode k) {
  constexpr SDL_Keycode kVolumeUpFallback = static_cast<SDL_Keycode>(1073741952);
  constexpr SDL_Keycode kVolumeDownFallback = static_cast<SDL_Keycode>(1073741953);
  switch (k) {
  case SDLK_UP: return Button::Up;
  case SDLK_DOWN: return Button::Down;
  case SDLK_LEFT: return Button::Left;
  case SDLK_RIGHT: return Button::Right;
  case SDLK_a: return Button::A;
  case SDLK_b: return Button::B;
  case SDLK_x: return Button::X;
  case SDLK_y: return Button::Y;
  case SDLK_m: return Button::Menu;
  case SDLK_ESCAPE: return Button::B;
  case SDLK_RETURN: return Button::A;
  case SDLK_BACKSPACE: return Button::Select;
  case SDLK_TAB: return Button::Start;
  case SDLK_q: return Button::L1;
  case SDLK_w: return Button::R1;
  case SDLK_e: return Button::L2;
  case SDLK_r: return Button::R2;
  case SDLK_1: return Button::L1;
  case SDLK_2: return Button::L2;
  case SDLK_4: return Button::R1;
  case SDLK_3: return Button::R2;
  case SDLK_z: return Button::Start;
  case SDLK_c: return Button::Select;
#ifdef SDLK_VOLUMEUP
  case SDLK_VOLUMEUP: return Button::VolUp;
#endif
#ifdef SDLK_VOLUMEDOWN
  case SDLK_VOLUMEDOWN: return Button::VolDown;
#endif
  case kVolumeUpFallback: return Button::VolUp;
  case kVolumeDownFallback: return Button::VolDown;
#ifdef SDLK_PLUS
  case SDLK_PLUS: return Button::VolUp;
#endif
#ifdef SDLK_KP_PLUS
  case SDLK_KP_PLUS: return Button::VolUp;
#endif
#ifdef SDLK_MINUS
  case SDLK_MINUS: return Button::VolDown;
#endif
#ifdef SDLK_KP_MINUS
  case SDLK_KP_MINUS: return Button::VolDown;
#endif
  default: return static_cast<Button>(-1);
  }
}

Button InputManager::PadToButton(uint8_t b) const {
  if (b >= pad_map_.size()) return InvalidButton();
  return pad_map_[b];
}

Button InputManager::JoyButtonToButton(uint8_t b) const {
  if (b >= joy_map_.size()) return InvalidButton();
  return joy_map_[b];
}

void InputManager::LoadDefaultPadMap() {
  pad_map_[SDL_CONTROLLER_BUTTON_DPAD_UP] = Button::Up;
  pad_map_[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = Button::Down;
  pad_map_[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = Button::Left;
  pad_map_[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = Button::Right;
  pad_map_[SDL_CONTROLLER_BUTTON_A] = Button::A;
  pad_map_[SDL_CONTROLLER_BUTTON_B] = Button::B;
  pad_map_[SDL_CONTROLLER_BUTTON_X] = Button::X;
  pad_map_[SDL_CONTROLLER_BUTTON_Y] = Button::Y;
  pad_map_[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = Button::L1;
  pad_map_[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = Button::R1;
  pad_map_[SDL_CONTROLLER_BUTTON_LEFTSTICK] = Button::L2;
  pad_map_[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = Button::R2;
  pad_map_[SDL_CONTROLLER_BUTTON_BACK] = Button::Select;
  pad_map_[SDL_CONTROLLER_BUTTON_START] = Button::Start;
}

void InputManager::LoadDefaultJoyMap(bool h700_defaults) {
  joy_map_[0] = Button::A;
  joy_map_[1] = Button::B;
  joy_map_[4] = Button::L1;
  joy_map_[5] = Button::R1;
  if (h700_defaults) {
    joy_map_[2] = Button::Y;
    joy_map_[3] = Button::X;
    joy_map_[6] = Button::Select;
    joy_map_[7] = Button::Start;
    joy_map_[8] = Button::Menu;
    joy_map_[9] = Button::Menu;
    joy_map_[10] = Button::L2;
    joy_map_[11] = Button::R2;
    joy_map_[12] = Button::Select;
    joy_map_[13] = Button::Start;
    joy_map_[15] = Button::VolDown;
    joy_map_[16] = Button::VolUp;
  } else {
    joy_map_[2] = Button::X;
    joy_map_[3] = Button::Y;
    joy_map_[6] = Button::L2;
    joy_map_[7] = Button::R2;
    joy_map_[8] = Button::Select;
    joy_map_[9] = Button::Start;
    joy_map_[10] = Button::Menu;
    joy_map_[11] = Button::Menu;
    joy_map_[12] = Button::Select;
    joy_map_[13] = Button::Start;
    joy_map_[15] = Button::VolDown;
    joy_map_[16] = Button::VolUp;
  }
}

std::string InputManager::Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

bool InputManager::ParseButtonName(const std::string &raw, Button &out) {
  std::string n;
  n.reserve(raw.size());
  for (char c : raw) {
    if (c == ' ' || c == '\t' || c == '-') continue;
    n.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  if (n == "UP") out = Button::Up;
  else if (n == "DOWN") out = Button::Down;
  else if (n == "LEFT") out = Button::Left;
  else if (n == "RIGHT") out = Button::Right;
  else if (n == "A") out = Button::A;
  else if (n == "B") out = Button::B;
  else if (n == "X") out = Button::X;
  else if (n == "Y") out = Button::Y;
  else if (n == "MENU") out = Button::Menu;
  else if (n == "L1") out = Button::L1;
  else if (n == "L2") out = Button::L2;
  else if (n == "R1") out = Button::R1;
  else if (n == "R2") out = Button::R2;
  else if (n == "START") out = Button::Start;
  else if (n == "SELECT") out = Button::Select;
  else if (n == "VOLUP" || n == "VOLUMEUP") out = Button::VolUp;
  else if (n == "VOLDOWN" || n == "VOLUMEDOWN") out = Button::VolDown;
  else if (n == "NONE" || n == "DISABLED" || n == "INVALID") out = InvalidButton();
  else return false;
  return true;
}

void InputManager::LoadOverrides(const std::string &mapping_path) {
  std::ifstream in(mapping_path);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string val = Trim(line.substr(eq + 1));
    Button mapped = InvalidButton();
    if (!ParseButtonName(val, mapped)) continue;
    if (key.rfind("joy.", 0) == 0) {
      const int idx = std::atoi(key.substr(4).c_str());
      if (idx >= 0 && idx < static_cast<int>(joy_map_.size())) joy_map_[idx] = mapped;
    } else if (key.rfind("pad.", 0) == 0) {
      const int idx = std::atoi(key.substr(4).c_str());
      if (idx >= 0 && idx < static_cast<int>(pad_map_.size())) pad_map_[idx] = mapped;
    }
  }
}

void InputManager::SetDown(Button b, bool down) {
  if (!IsValid(b)) return;
  BtnState &s = states_[static_cast<int>(b)];
  if (down && !s.down) {
    s.down = true;
    s.just_pressed = true;
    s.hold_time = 0.0f;
  } else if (!down && s.down) {
    s.down = false;
    s.just_released = true;
  }
}

const BtnState &InputManager::Get(Button b) const {
  static BtnState empty;
  if (!IsValid(b)) return empty;
  return states_[static_cast<int>(b)];
}
