#pragma once
#include <cstdint>

enum class ButtonEvent { None, Single, Double, Hold };

// Pure debouncer/gesture recognizer, sampled every 10 ms by the GPIO task.
class ButtonGestures {
public:
    void reset(bool pressed, uint64_t now) {
        raw_ = stable_ = pressed;
        armed_ = !pressed;  // A button held during boot must first be released.
        pending_ = second_ = held_ = false;
        changedAt_ = pressedAt_ = releasedAt_ = now;
    }
    ButtonEvent update(bool pressed, uint64_t now) {
        if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
        if (raw_ != stable_ && now - changedAt_ >= 30) {
            stable_ = raw_;
            if (stable_) {
                pressedAt_ = changedAt_;
                held_ = false;
                second_ = pending_ && pressedAt_ - releasedAt_ <= 300;
                if (pending_ && !second_) {
                    pending_ = false;
                    return ButtonEvent::Single;
                }
            } else {
                if (!armed_) { armed_ = true; return ButtonEvent::None; }
                if (held_) return ButtonEvent::None;
                if (second_) {
                    pending_ = second_ = false;
                    return ButtonEvent::Double;
                }
                pending_ = true;
                releasedAt_ = changedAt_;
            }
        }
        if (armed_ && stable_ && raw_ && !held_ && now - pressedAt_ > 1000) {
            held_ = true;
            pending_ = second_ = false;
            return ButtonEvent::Hold;
        }
        if (pending_ && !stable_ && !raw_ && now - releasedAt_ >= 300) {
            pending_ = false;
            return ButtonEvent::Single;
        }
        return ButtonEvent::None;
    }
private:
    bool raw_ = false, stable_ = false, armed_ = true;
    bool pending_ = false, second_ = false, held_ = false;
    uint64_t changedAt_ = 0, pressedAt_ = 0, releasedAt_ = 0;
};
