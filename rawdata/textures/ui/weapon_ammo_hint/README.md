# Weapon tooltip Tab keycaps

The hint is one blank keycap. `TAB` is native text in each `ammo_switch_hint/text` XML node, never part of the texture. Vanilla uses a beige Soviet-style key; SGM uses a newer charcoal key. SVGs are the editable artwork sources; PNGs are their raster exports. No image generation service is required for these simple vector UI assets.

Game texture: `ui/ui_weapon_ammo_hint.dds`, 256 x 256, DXT5 with alpha, base level only. Keycap region: (4, 4, 240, 144); opaque panel region: (4, 244, 8, 8). Both variants use the same descriptor IDs: `ui_weapon_ammo_keycap` and `ui_weapon_ammo_panel`; the SGM addon supplies its own texture through the normal VFS.

Geometry is in virtual UI units: canonical keycap 40 x 24, with one horizontal `kx` correction in the existing C++ layout. Native text uses `align="c"` and `vert_align="c"` in the same CUIStatic, so it follows the actual resized widget. Texture and all key faces are centered at (120, 72), allowing centered text without manual offsets. Header top 6 + key height 24 + name gap 4 = name top 34; name height 18 + grid gap 6 = initial grid top 58. Runtime font metrics still determine the actual flow height.
