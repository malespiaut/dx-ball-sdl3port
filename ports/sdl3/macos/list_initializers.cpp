/* Mach-O section names are "segment,section" and have no name-sorted
   placement. Put every ordered board slot in one section instead; the slots
   are defined in layout order, and test_board_layout checks the result.
   Mach-O has no protected visibility and no copy relocations to avoid. */
#include <stdlib.h>
#include "list_initializers.h"
#define section(name) section("__DATA,__dxb_board")
#define visibility(name) visibility("default")
#include "../../../src/list_initializers.cpp"
