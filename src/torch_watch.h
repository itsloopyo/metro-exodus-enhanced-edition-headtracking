#pragma once

#include <cstdint>

namespace metroex {

// Names the instruction that overwrites the torch basis, once, into the log.
//
// WHY THIS EXISTS. The mod writes the beam's right/up/forward from the camera
// hook and the engine puts them straight back: a 60-degree yawed basis forced in
// from outside and rewritten every 50ms survived 0 of 80 samples. The chain and
// the offsets are right - a sentinel in the forward is replaced by the mod's own
// value within a frame - so what is missing is WHERE in the frame the engine
// writes it, and that is a question only the writing instruction can answer.
//
// Static searching does not answer it. Displacements 0x30/0x40/0x50/0x60 are the
// commonest struct offsets in the binary; filtering to functions that write all
// four, excluding stack frames, still leaves dozens.
//
// HOW. A hardware data breakpoint - DR0 plus the write bits in DR7 - on the
// forward field, and a vectored exception handler that reports the faulting RIP
// as module+RVA and then disarms. Hardware breakpoints are per thread, so every
// thread in the process gets one; the write can come from any of them.
//
// This is a DIAGNOSTIC and it is opt-in. It suspends every thread briefly to set
// the registers, and it costs an exception on the first write. It has no place in
// a normal session, which is why it rides on the same Discovery switch as the
// per-frame camera logging rather than running by default.
class TorchWatch {
public:
    // `torch` is the entity from Torch::Resolve(), `fieldOffset` the field to
    // watch. Arms once and does nothing on later calls.
    void ArmOnce(uintptr_t torch, uint32_t fieldOffset);

    // Removes the handler and clears the debug registers. Safe to call when
    // nothing was ever armed.
    void Disarm();

private:
    bool m_armed = false;
    void* m_handler = nullptr;
};

}  // namespace metroex
