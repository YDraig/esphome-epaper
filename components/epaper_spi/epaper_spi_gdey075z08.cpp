#include "epaper_spi_gdey075z08.h"

#include <algorithm>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::epaper_spi {

static const char *const TAG = "epaper_spi.gdey075z08";

void EPaperGDEY075Z08::setup() {
  // EPaperUC8179BWR does not override setup(), so initialise the normal
  // two-plane framebuffer and SPI/pins through EPaperBase.
  EPaperBase::setup();

  if (this->is_failed())
    return;

  // We only need the extra old-frame buffer when partial refresh is enabled.
  if (this->is_using_partial_update_()) {
    const size_t frame = this->buffer_length_ / 2u;

    RAMAllocator<uint8_t> allocator;
    this->old_data_ = allocator.allocate(frame);

    if (this->old_data_ == nullptr) {
      this->mark_failed(LOG_STR("Failed to allocate GDEY075Z08 partial refresh buffer"));
      return;
    }

    ESP_LOGI(TAG, "Allocated %u byte partial refresh buffer", static_cast<unsigned>(frame));
  }
}

void EPaperGDEY075Z08::snapshot_composite_() {
  if (this->old_data_ == nullptr)
    return;

  const size_t frame = this->buffer_length_ / 2u;

  for (size_t i = 0; i < frame; i++) {
    const uint8_t black = this->buffer_[i];
    const uint8_t red = this->buffer_[frame + i];

    // The GDEY075Z08 partial B/W waveform sees both existing black and
    // existing red pigment as "ink".
    //
    // ESPHome framebuffer:
    //   black bit = 1 means black
    //   red bit   = 1 means red
    //
    // Partial representation:
    //   1 = white
    //   0 = ink
    this->old_data_[i] = static_cast<uint8_t>(~(black | red));
  }
}

bool EPaperGDEY075Z08::initialise(bool partial) {
  this->partial_ = partial;

  if (!partial) {
    // Normal B/W/red full refresh. Keep the generic implementation intact.
    return EPaperUC8179BWR::initialise(false);
  }

  // Send the model's normal GDEY075Z08 BWR init sequence first.
  //
  // Do not call EPaperUC8179BWR::initialise(true), because that selects
  // the generic BWR fast-refresh waveform (0xE5 = 0x5A). We want the
  // Good Display differential partial waveform instead.
  if (!EPaperBase::initialise(true))
    return false;

  ESP_LOGD(TAG, "Initialising B/W differential partial refresh");

  // Good Display GDEY075Z08 partial refresh mode.
  //
  // PANEL SETTING:
  // Switch the UC8179 from KWR (B/W/R) mode into KW B/W mode for
  // differential partial refresh.
  this->cmd_data(0x00, {0x1F});

  // Use fixed-temperature waveform selection.
  this->cmd_data(0xE0, {0x02});

  // GDEY075Z08 partial-refresh waveform.
  this->cmd_data(0xE5, {0x6E});

  // Differential B/W polarity and VCOM/data interval.
  this->cmd_data(0x50, {0xA9, 0x07});

  return true;
}

bool HOT EPaperGDEY075Z08::transfer_data() {
  /*
   * FULL B/W/R REFRESH
   *
   * Let the generic UC8179 BWR driver do exactly what it did before.
   * Once transfer is complete, save a monochrome representation of the
   * frame so the next partial update has a valid OLD frame.
   */
  if (!this->partial_) {
    const bool complete = EPaperUC8179BWR::transfer_data();

    if (complete)
      this->snapshot_composite_();

    return complete;
  }

  /*
   * GDEY075Z08 B/W DIFFERENTIAL PARTIAL REFRESH
   *
   * Red cannot be changed here. The current red plane is folded into the
   * monochrome frame so existing red pigment is preserved as ink.
   */

  const uint32_t start_time = millis();
  const size_t frame = this->buffer_length_ / 2u;

  uint8_t bytes_to_send[MAX_TRANSFER_SIZE];

  /*
   * Enter partial mode and select the full display as the partial window.
   *
   * We're doing a full-screen differential update for now. Later we can
   * optimise this further to restrict the window to the clock digits.
   */
  if (this->current_data_index_ == 0) {
    const uint16_t xe = this->width_ - 1;
    const uint16_t ye = this->height_ - 1;

    this->command(0x91);  // PARTIAL IN

    this->cmd_data(0x90, {
                             0x00,
                             0x00,  // X start

                             static_cast<uint8_t>(xe >> 8),
                             static_cast<uint8_t>(xe & 0xFF),  // X end

                             0x00,
                             0x00,  // Y start

                             static_cast<uint8_t>(ye >> 8),
                             static_cast<uint8_t>(ye & 0xFF),  // Y end

                             0x01,
                         });
  }

  /*
   * Phase 1 — OLD FRAME
   *
   * Send the previously displayed B/W composite to DTM1 (0x10).
   */
  if (this->current_data_index_ < frame) {
    if (this->current_data_index_ == 0)
      this->command(0x10);

    this->start_data_();

    while (this->current_data_index_ < frame) {
      const size_t n = std::min(MAX_TRANSFER_SIZE, frame - this->current_data_index_);

      std::copy_n(&this->old_data_[this->current_data_index_], n, bytes_to_send);

      this->write_array(bytes_to_send, n);
      this->current_data_index_ += n;

      if (millis() - start_time > MAX_TRANSFER_TIME) {
        this->disable();
        return false;
      }
    }

    this->disable();
  }

  /*
   * Phase 2 — NEW FRAME
   *
   * Fold ESPHome's two BWR planes into the one B/W representation expected
   * by the GDEY075Z08 differential waveform:
   *
   *      composite = ~(black | red)
   *
   * White pixel       -> 1
   * Black pixel       -> 0
   * Existing red pixel-> 0
   *
   * Red should therefore remain unchanged during partial-refresh cycles.
   */
  if (this->current_data_index_ < 2 * frame) {
    if (this->current_data_index_ == frame)
      this->command(0x13);

    this->start_data_();

    while (this->current_data_index_ < 2 * frame) {
      const size_t offset = this->current_data_index_ - frame;

      const size_t n = std::min(MAX_TRANSFER_SIZE, (2 * frame) - this->current_data_index_);

      for (size_t i = 0; i < n; i++) {
        const uint8_t black = this->buffer_[offset + i];

        const uint8_t red = this->buffer_[frame + offset + i];

        const uint8_t composite = static_cast<uint8_t>(~(black | red));

        bytes_to_send[i] = composite;

        // This becomes OLD data for the next differential update.
        if (this->old_data_ != nullptr)
          this->old_data_[offset + i] = composite;
      }

      this->write_array(bytes_to_send, n);
      this->current_data_index_ += n;

      if (millis() - start_time > MAX_TRANSFER_TIME) {
        this->disable();
        return false;
      }
    }

    this->disable();
  }

  /*
   * Good Display's full-screen partial routine refreshes while partial
   * mode is active. Do not issue PARTIAL OUT (0x92) here.
   *
   * The normal EPaperBase state machine will next:
   *
   *   POWER_ON
   *   REFRESH_SCREEN -> 0x12
   *   POWER_OFF
   *   DEEP_SLEEP
   *
   * The panel gets reset before the following update anyway.
   */
  this->current_data_index_ = 0;

  return true;
}

}  // namespace esphome::epaper_spi