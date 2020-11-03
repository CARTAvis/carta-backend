#include "Tile.h"

std::ostream& operator<<(std::ostream& os, const Tile& tile) {
    fmt::print(os, "x={}, y={}, layer={}", tile.x, tile.y, tile.layer);
    return os;
}
