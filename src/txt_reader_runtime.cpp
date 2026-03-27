#include "txt_reader_runtime.h"

#include <algorithm>
#include <array>

void HandleTxtReaderInput(TxtReaderInputDeps &deps) {
  if (deps.ui.mode != ReaderMode::Txt || !deps.ui.txt_reader.open) return;

  std::array<Button, 2> vdirs = {Button::Up, Button::Down};
  for (Button b : vdirs) {
    int bi = static_cast<int>(b);
    const int long_dir = (b == Button::Down) ? 1 : -1;
    if (deps.input.IsPressed(b)) {
      const float hold = deps.input.HoldTime(b);
      const float delay = 0.30f;
      const float speed_min = 120.0f;
      const float speed_max = 620.0f;
      const float speed_accel = 860.0f;
      if (hold >= delay) {
        deps.ui.long_fired[bi] = true;
        deps.ui.hold_speed[bi] = (deps.ui.hold_speed[bi] <= 0.0f)
                                     ? speed_min
                                     : std::min(speed_max, deps.ui.hold_speed[bi] + speed_accel * deps.dt);
        const int step_px = std::max(1, static_cast<int>(deps.ui.hold_speed[bi] * deps.dt));
        deps.text_scroll_by(long_dir * step_px);
      } else {
        deps.ui.hold_speed[bi] = 0.0f;
      }
    } else {
      deps.ui.hold_speed[bi] = 0.0f;
    }
  }

  for (Button b : vdirs) {
    int bi = static_cast<int>(b);
    if (!deps.input.IsJustReleased(b)) continue;
    deps.ui.hold_speed[bi] = 0.0f;
    if (deps.ui.long_fired[bi]) {
      deps.ui.long_fired[bi] = false;
      continue;
    }
    const int tap_dir = (b == Button::Down) ? 1 : -1;
    deps.text_scroll_by(tap_dir * deps.tap_step_px);
  }

  if (deps.input.IsJustPressed(Button::Right)) {
    deps.text_page_by(1);
  } else if (deps.input.IsJustPressed(Button::Left)) {
    deps.text_page_by(-1);
  }

  deps.ui.progress.page = (deps.ui.txt_reader.line_h > 0) ? (deps.ui.txt_reader.scroll_px / deps.ui.txt_reader.line_h) : 0;
  deps.ui.progress.scroll_y = deps.ui.txt_reader.scroll_px;
}

void DrawTxtReaderRuntime(TxtReaderRenderDeps &deps) {
  if (deps.ui.mode != ReaderMode::Txt || !deps.ui.txt_reader.open) return;

  deps.clamp_text_scroll();
  const SDL_Rect clip{
      deps.ui.txt_reader.viewport_x,
      deps.ui.txt_reader.viewport_y,
      deps.ui.txt_reader.viewport_w,
      deps.ui.txt_reader.viewport_h,
  };
  deps.set_clip_rect(clip);
  const int text_x = deps.ui.txt_reader.viewport_x + 2;
  const int start_line = std::max(0, deps.ui.txt_reader.scroll_px / std::max(1, deps.ui.txt_reader.line_h));
  int y = deps.ui.txt_reader.viewport_y - (deps.ui.txt_reader.scroll_px % std::max(1, deps.ui.txt_reader.line_h));
  for (int i = start_line; i < static_cast<int>(deps.ui.txt_reader.lines.size()); ++i) {
    if (y > deps.ui.txt_reader.viewport_y + deps.ui.txt_reader.viewport_h) break;
    deps.draw_text_line(deps.ui.txt_reader.lines[i], text_x, y);
    y += deps.ui.txt_reader.line_h;
  }
  deps.clear_clip_rect();
}
