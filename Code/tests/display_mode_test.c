#include "../display_mode.h"
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static GraphicsInfo modes[5];
static unsigned frees, calls, fail, truncated;
static uint64_t query(GraphicsOutput *graphics, uint32_t index, uint64_t *size, GraphicsInfo **info)
{
    (void)graphics;
    if (index >= 5) { *info = 0; return 1; }
    *size = index == truncated ? 1 : sizeof(GraphicsInfo); *info = &modes[index]; return 0;
}
static uint64_t set(GraphicsOutput *graphics, uint32_t index)
{
    ++calls;
    if (index >= 5 || index == fail) return 1;
    graphics->mode->mode = index; graphics->mode->info = &modes[index]; return 0;
}
static uint64_t free_info(void *pointer) { (void)pointer; ++frees; return 0; }
int display_mode_test(void)
{
    modes[0] = (GraphicsInfo){0,800,600,1,{0},800};
    modes[1] = (GraphicsInfo){0,1280,720,0,{0},1280};
    modes[2] = (GraphicsInfo){0,2560,1440,1,{0},2560};
    modes[3] = (GraphicsInfo){0,1920,1080,1,{0},1920};
    modes[4] = (GraphicsInfo){0,1920,1080,3,{0},1920}; // Blt-only is not a usable framebuffer.
    GraphicsMode mode = {5,0,&modes[0],sizeof(GraphicsInfo),0,0};
    GraphicsOutput graphics = {query,set,0,&mode};
    BootServices services = {0}; services.free_pool = free_info;
    frees = calls = 0; fail = truncated = 99;
    display_select_mode(&graphics,&services);
    CHECK(mode.mode == 3 && calls == 1 && frees == 5);
    mode.mode = 2; mode.info = &modes[2]; calls = frees = 0;
    display_select_mode(&graphics,&services); CHECK(mode.mode == 2 && !calls && !frees);
    mode.mode = 0; mode.info = &modes[0]; fail = 3; calls = 0;
    display_select_mode(&graphics,&services); CHECK(mode.mode == 0 && calls == 2);
    fail = 99; truncated = 3;
    display_select_mode(&graphics,&services); CHECK(mode.mode == 1);
    mode.mode = 0; mode.info = &modes[0]; truncated = 99; modes[3].pixels_per_scanline = 1;
    display_select_mode(&graphics,&services); CHECK(mode.mode == 1);
    graphics.query_mode = 0; display_select_mode(&graphics,&services); CHECK(mode.mode == 1);
    return 0;
}
