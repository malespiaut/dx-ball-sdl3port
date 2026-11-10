/* The ordered board slots form one DxBallBoardStorage-shaped region on every
   host linker (ELF, PE, Mach-O): the terminal board copy reads past the bank
   into the globals that follow it. */
#include "check.h"
#include "board_storage.h"
#include <stddef.h>
#include <stdint.h>

/* Integer addresses: pointer arithmetic past the bank may not reach another
   object, so compilers fold such pointer comparisons to false. */
#define AT(field) ((uintptr_t)dxball_board_storage + offsetof(DxBallBoardStorage, field))
#define ADDRESS(object) ((uintptr_t)(object))

int main(void)
{
    CHECK_EQ(ADDRESS(&dxball_palette_tick) - AT(palette_tick), 0);
    CHECK_EQ(ADDRESS(dxball_unclassified_clock_tail) - AT(unclassified_clock_tail), 0);
    CHECK_EQ(ADDRESS(&dxball_explosions) - AT(explosions), 0);
    CHECK_EQ(ADDRESS(&dxball_ball_count) - AT(ball_count), 0);
    CHECK_EQ(ADDRESS(dxball_unclassified_count_tail) - AT(unclassified_count_tail), 0);
    CHECK_EQ(ADDRESS(dxball_board_tiles) - AT(tiles), 0);
    return CHECK_DONE();
}
