#include "reader_core.h"

int ReaderScrollDirForButton(int rotation, Button button) {
  if (rotation == 0) {
    if (button == Button::Down) return 1;
    if (button == Button::Up) return -1;
  } else if (rotation == 270) {
    if (button == Button::Right) return 1;
    if (button == Button::Left) return -1;
  } else if (rotation == 90) {
    if (button == Button::Left) return 1;
    if (button == Button::Right) return -1;
  } else {
    if (button == Button::Up) return 1;
    if (button == Button::Down) return -1;
  }
  return 0;
}

int ReaderTapPageActionForButton(int rotation, Button button) {
  if (rotation == 0) {
    if (button == Button::Right) return 1;
    if (button == Button::Left) return -1;
  } else if (rotation == 90) {
    if (button == Button::Down) return 1;
    if (button == Button::Up) return -1;
  } else if (rotation == 180) {
    if (button == Button::Left) return 1;
    if (button == Button::Right) return -1;
  } else {
    if (button == Button::Up) return 1;
    if (button == Button::Down) return -1;
  }
  return 0;
}

int PdfScrollDirForButton(int rotation, Button button) {
  if (rotation == 0) {
    if (button == Button::Down) return 1;
    if (button == Button::Up) return -1;
  } else if (rotation == 90) {
    if (button == Button::Left) return 1;
    if (button == Button::Right) return -1;
  } else if (rotation == 180) {
    if (button == Button::Up) return 1;
    if (button == Button::Down) return -1;
  } else {
    if (button == Button::Left) return -1;
    if (button == Button::Right) return 1;
  }
  return 0;
}

int PdfTapPageActionForButton(int rotation, Button button) {
  if (rotation == 0) {
    if (button == Button::Right) return 1;
    if (button == Button::Left) return -1;
  } else if (rotation == 90) {
    if (button == Button::Up) return -1;
    if (button == Button::Down) return 1;
  } else if (rotation == 180) {
    if (button == Button::Left) return 1;
    if (button == Button::Right) return -1;
  } else {
    if (button == Button::Up) return 1;
    if (button == Button::Down) return -1;
  }
  return 0;
}
