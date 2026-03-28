#pragma once

#include <SDL.h>

#include <array>
#include <string>

constexpr int kButtonCount = 17;

enum class Button {
  Up,
  Down,
  Left,
  Right,
  A,
  B,
  X,
  Y,
  Menu,
  L1,
  L2,
  R1,
  R2,
  Start,
  Select,
  VolUp,
  VolDown,
};

const char *ButtonName(Button b);
const char *SdlEventName(Uint32 type);

struct BtnState {
  bool down = false;
  bool just_pressed = false;
  bool just_released = false;
  bool repeated = false;
  bool long_pressed = false;
  float hold_time = 0.0f;
  float repeat_timer = 0.0f;
  bool repeat_active = false;
};

class InputManager {
public:
  InputManager(const std::string &mapping_path, bool h700_defaults);

  void BeginFrame(float dt);
  void HandleEvent(const SDL_Event &e);
  void EndFrame();

  bool IsPressed(Button b) const;
  bool IsJustPressed(Button b) const;
  bool IsJustReleased(Button b) const;
  bool IsRepeated(Button b) const;
  bool IsLongPressed(Button b) const;
  float HoldTime(Button b) const;
  bool AnyPressed() const;
  void ResetAll();

private:
  static Button InvalidButton();
  static bool IsValid(Button b);
  static Button KeyToButton(SDL_Keycode k);
  Button PadToButton(uint8_t b) const;
  Button JoyButtonToButton(uint8_t b) const;
  void LoadDefaultPadMap();
  void LoadDefaultJoyMap(bool h700_defaults);
  static std::string Trim(std::string s);
  static bool ParseButtonName(const std::string &raw, Button &out);
  void LoadOverrides(const std::string &mapping_path);
  void SetDown(Button b, bool down);
  const BtnState &Get(Button b) const;

  std::array<BtnState, kButtonCount> states_{};
  std::array<Button, 32> pad_map_{};
  std::array<Button, 32> joy_map_{};
  float dt_ = 1.0f / 60.0f;
};
