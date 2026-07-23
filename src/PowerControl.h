#pragma once

// Request a full device power-off (battery-cut PMIC shutdown), identical to holding
// the hardware side button. The request is a flag consumed by the main loop, so it is
// safe to call from within an activity; the actual power-off runs in the main-loop
// context rather than deep inside the activity/render stack.
// Implemented in main.cpp.
void requestShutdown();
