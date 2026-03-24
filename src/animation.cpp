#include "animation.h"

#include <algorithm>

namespace animation {
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

float ApplyEase(Ease ease, float t) {
  const float x = std::clamp(t, 0.0f, 1.0f);
  switch (ease) {
  case Ease::Linear:
    return x;
  case Ease::OutCubic: {
    const float one_minus = 1.0f - x;
    return 1.0f - one_minus * one_minus * one_minus;
  }
  case Ease::InOutCubic:
    if (x < 0.5f) return 4.0f * x * x * x;
    return 1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) / 2.0f;
  }
  return x;
}

TweenFloat::TweenFloat(float initial) { Snap(initial); }

void TweenFloat::Snap(float value) {
  value_ = value;
  start_ = value;
  target_ = value;
  duration_ = 0.0f;
  elapsed_ = 0.0f;
  animating_ = false;
}

void TweenFloat::AnimateTo(float target, float duration_sec, Ease ease) {
  if (duration_sec <= 0.0f) {
    Snap(target);
    return;
  }
  start_ = value_;
  target_ = target;
  duration_ = duration_sec;
  elapsed_ = 0.0f;
  ease_ = ease;
  animating_ = true;
}

void TweenFloat::Update(float dt) {
  if (!animating_) return;
  if (dt <= 0.0f) return;
  elapsed_ = std::min(duration_, elapsed_ + dt);
  const float t = duration_ > 0.0f ? (elapsed_ / duration_) : 1.0f;
  value_ = start_ + (target_ - start_) * ApplyEase(ease_, t);
  if (elapsed_ >= duration_) {
    value_ = target_;
    animating_ = false;
  }
}

bool TweenFloat::IsAnimating(float epsilon) const {
  if (animating_) return true;
  return std::abs(target_ - value_) > epsilon;
}

SpringFloat::SpringFloat(float initial) { Snap(initial); }

void SpringFloat::Snap(float value) {
  value_ = value;
  target_ = value;
  velocity_ = 0.0f;
}

void SpringFloat::Update(float dt, const SpringParams &params) {
  if (dt <= 0.0f) return;

  // Implicit Euler integration of a damped spring; stable on variable frame time.
  const float omega = 2.0f * kPi * std::max(0.1f, params.frequency_hz);
  const float zeta = std::clamp(params.damping_ratio, 0.05f, 4.0f);
  const float f = 1.0f + 2.0f * dt * zeta * omega;
  const float oo = omega * omega;
  const float hoo = dt * oo;
  const float hhoo = dt * hoo;
  const float det_inv = 1.0f / (f + hhoo);

  const float det_x = f * value_ + dt * velocity_ + hhoo * target_;
  const float det_v = velocity_ + hoo * (target_ - value_);

  value_ = det_x * det_inv;
  velocity_ = det_v * det_inv;
}

bool SpringFloat::IsAnimating(const SpringParams &params) const {
  return std::abs(target_ - value_) > params.epsilon || std::abs(velocity_) > params.velocity_epsilon;
}

} // namespace animation
