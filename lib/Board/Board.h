#pragma once

#if defined(BOARD_LILYGO_EPD47_S3)
#include <BoardEPD47.h>
namespace Board = BoardEPD47;
namespace BoardPins = BoardEPD47Pins;
#elif defined(BOARD_T5S3_PRO) || defined(BOARD_T5S3)
#include <BoardT5S3.h>
namespace Board = BoardT5S3;
namespace BoardPins = BoardT5S3Pins;
#else
#error "Select a supported board environment in platformio.ini"
#endif
