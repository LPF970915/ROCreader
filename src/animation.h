#pragma once

#include <cmath>

namespace animation {

enum class Ease {
  Linear,
  OutCubic,
  InOutCubic,
};

float ApplyEase(Ease ease, float t);

class TweenFloat {
public:
  explicit TweenFloat(float initial = 0.0f);

  void Snap(float value);
  void AnimateTo(float target, float duration_sec, Ease ease = Ease::OutCubic);
  void Update(float dt);

  float Value() const { return value_; }
  float Target() const { return target_; }
  bool IsAnimating(float epsilon = 0.0001f) const;

private:
  float value_ = 0.0f;
  float start_ = 0.0f;
  float target_ = 0.0f;
  float duration_ = 0.0f;
  float elapsed_ = 0.0f;
  Ease ease_ = Ease::OutCubic;
  bool animating_ = false;
};

struct SpringParams {
  float frequency_hz = 10.0f;
  float damping_ratio = 0.82f;
  float epsilon = 0.0008f;
  float velocity_epsilon = 0.002f;
};

class SpringFloat {
public:
  explicit SpringFloat(float initial = 0.0f);

  void Snap(float value);
  void SetTarget(float target) { target_ = target; }
  void Update(float dt, const SpringParams &params = SpringParams{});

  float Value() const { return value_; }
  float Target() const { return target_; }
  bool IsAnimating(const SpringParams &params = SpringParams{}) const;

private:
  float value_ = 0.0f;
  float target_ = 0.0f;
  float velocity_ = 0.0f;
};

} // namespace animation
