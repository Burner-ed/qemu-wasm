#include "qemu/osdep.h"
#include "qemu/module.h"
#include "ui/console.h"
#include "ui/surface.h"
#include <emscripten.h>

typedef struct WasmDisplay {
    DisplayChangeListener dcl;
    DisplaySurface *surface;
} WasmDisplay;

EM_JS(void, wasm_display_surface, (int width, int height, int format,
                                   int stride, int data), {
    if (Module.canvas && Module.canvas.getContext) {
        Module.canvas.width = width;
        Module.canvas.height = height;
        const context = Module.canvas.getContext('2d');
        Module.__qemuWasmSurface = {
            context: context,
            image: context.createImageData(width, height),
            width: width,
            height: height,
        };
    }
    if (Module.qemuWasmDisplay && Module.qemuWasmDisplay.surface) {
        Module.qemuWasmDisplay.surface(width, height, format, stride, data);
    }
});

EM_JS(void, wasm_display_update, (int data, int stride, int x, int y,
                                  int width, int height), {
    const surface = Module.__qemuWasmSurface;
    if (surface) {
        const source = HEAPU8;
        const pixels = surface.image.data;
        for (let row = 0; row < height; row++) {
            const source_row = data + (y + row) * stride + x * 4;
            const target_row = ((y + row) * surface.width + x) * 4;
            for (let column = 0; column < width; column++) {
                const source_pixel = source_row + column * 4;
                const target_pixel = target_row + column * 4;
                pixels[target_pixel] = source[source_pixel + 2];
                pixels[target_pixel + 1] = source[source_pixel + 1];
                pixels[target_pixel + 2] = source[source_pixel];
                pixels[target_pixel + 3] = 255;
            }
        }
        surface.context.putImageData(surface.image, 0, 0,
                                     x, y, width, height);
    }
    if (Module.qemuWasmDisplay && Module.qemuWasmDisplay.update) {
        Module.qemuWasmDisplay.update(data, stride, x, y, width, height);
    }
});

static void wasm_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

static void wasm_gfx_switch(DisplayChangeListener *dcl,
                            DisplaySurface *new_surface)
{
    WasmDisplay *display = container_of(dcl, WasmDisplay, dcl);

    display->surface = new_surface;
    if (new_surface) {
        wasm_display_surface(surface_width(new_surface),
                             surface_height(new_surface),
                             surface_format(new_surface),
                             surface_stride(new_surface),
                             (int)(uintptr_t)surface_data(new_surface));
    }
}

static void wasm_gfx_update(DisplayChangeListener *dcl,
                            int x, int y, int width, int height)
{
    WasmDisplay *display = container_of(dcl, WasmDisplay, dcl);

    if (!display->surface) {
        return;
    }
    wasm_display_update((int)(uintptr_t)surface_data(display->surface),
                        surface_stride(display->surface), x, y, width, height);
}

static bool wasm_gfx_check_format(DisplayChangeListener *dcl,
                                  pixman_format_code_t format)
{
    return format == PIXMAN_x8r8g8b8;
}

static const DisplayChangeListenerOps wasm_ops = {
    .dpy_name = "wasm",
    .dpy_refresh = wasm_refresh,
    .dpy_gfx_update = wasm_gfx_update,
    .dpy_gfx_switch = wasm_gfx_switch,
    .dpy_gfx_check_format = wasm_gfx_check_format,
};

static void wasm_display_init(DisplayState *ds, DisplayOptions *opts)
{
    (void)ds;
    (void)opts;

    for (unsigned int index = 0;; index++) {
        QemuConsole *con = qemu_console_lookup_by_index(index);
        WasmDisplay *display;

        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }

        display = g_new0(WasmDisplay, 1);
        display->dcl.con = con;
        display->dcl.ops = &wasm_ops;
        register_displaychangelistener(&display->dcl);
    }
}

static QemuDisplay qemu_display_wasm = {
    .type = DISPLAY_TYPE_WASM,
    .init = wasm_display_init,
};

static void register_wasm(void)
{
    qemu_display_register(&qemu_display_wasm);
}

type_init(register_wasm);
