#pragma once

#include "epaper_spi_uc8179_bwr.h"

namespace esphome::epaper_spi {

/**
 * Good Display GDEY075Z08
 *
 * 7.5", 800x480, black/white/red, UC8179.
 *
 * Red takes priority over black within a rendered frame, regardless of draw
 * order. White explicitly clears either color; clear before drawing a new
 * frame. Keep red content static between full refreshes.
 * Full refresh uses the normal UC8179 BWR transfer implementation.
 *
 * Partial refresh uses the GDEY075Z08 black/white differential mode:
 *   - Red pixels remain static.
 *   - Black/white pixels can change.
 *   - A monochrome composite of black + red is retained as the
 *     previous frame to seed both RAM planes after reset.
 *   - Inside the partial window, only new data is written via 0x13.
 */
class EPaperGDEY075Z08 : public EPaperUC8179BWR {
 public:
  using EPaperUC8179BWR::EPaperUC8179BWR;

  void setup() override;
  void fill(Color color) override;

 protected:
  void draw_pixel_at(int x, int y, Color color) override;
  bool initialise(bool partial) override;
  bool transfer_data() override;

  // Previous displayed image as seen by the B/W partial waveform.
  // 800 * 480 / 8 = 48,000 bytes.
  uint8_t *old_data_{nullptr};

  bool partial_{false};

  void snapshot_composite_();
};

}  // namespace esphome::epaper_spi
