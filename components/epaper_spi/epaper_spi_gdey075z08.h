#pragma once

#include "epaper_spi_uc8179_bwr.h"

namespace esphome::epaper_spi {

/**
 * Good Display GDEY075Z08
 *
 * 7.5", 800x480, black/white/red, UC8179.
 *
 * Full refresh uses the normal UC8179 BWR implementation.
 *
 * Partial refresh uses the GDEY075Z08 black/white differential mode:
 *   - Red pixels remain static.
 *   - Black/white pixels can change.
 *   - A monochrome composite of black + red is retained as the
 *     previous frame for differential refresh.
 */
class EPaperGDEY075Z08 : public EPaperUC8179BWR {
 public:
  using EPaperUC8179BWR::EPaperUC8179BWR;

  void setup() override;

 protected:
  bool initialise(bool partial) override;
  bool transfer_data() override;

  // Previous displayed image as seen by the B/W partial waveform.
  // 800 * 480 / 8 = 48,000 bytes.
  uint8_t *old_data_{nullptr};

  bool partial_{false};

  void snapshot_composite_();
};

}  // namespace esphome::epaper_spi