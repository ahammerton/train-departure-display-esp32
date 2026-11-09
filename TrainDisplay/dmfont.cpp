#include "dmfont.h"

static const DMGlyph *glyphFor(const DMFont &font, char c) {
  uint8_t code = (uint8_t)c;
  if (code < font.first || code > font.last) code = '?';
  return &font.glyphs[code - font.first];
}

int dmTextWidth(const DMFont &font, const char *text) {
  int w = 0;
  for (const char *p = text; *p; p++) w += glyphFor(font, *p)->adv;
  return w;
}

int dmBoxWidth(const DMFont &font, const char *text) {
  int w = 0;
  const DMGlyph *last = nullptr;
  for (const char *p = text; *p; p++) {
    last = glyphFor(font, *p);
    w += last->adv;
  }
  return last ? w - last->adv + last->boxRight : 0;
}

int dmBoxHeight(const DMFont &font, const char *text) {
  int h = 0;
  for (const char *p = text; *p; p++) {
    const DMGlyph *g = glyphFor(font, *p);
    if (g->w && g->yo + g->h > h) h = g->yo + g->h;
  }
  return h;
}

int dmDrawText(U8G2 &u8g2, const DMFont &font, int x, int y, const char *text) {
  const int dispW = u8g2.getDisplayWidth();
  for (const char *p = text; *p; p++) {
    const DMGlyph *g = glyphFor(font, *p);
    if (x >= dispW) break;  // rest of the string is off the right edge
    int gx = x + g->xo;
    if (gx + g->w > 0 && g->w > 0) {
      const uint8_t *row = font.bitmaps + g->offset;
      const int rowBytes = (g->w + 7) / 8;
      for (int r = 0; r < g->h; r++, row += rowBytes) {
        int py = y + g->yo + r;
        for (int c = 0; c < g->w; c++) {
          if (row[c >> 3] & (0x80 >> (c & 7))) {
            int px = gx + c;
            // drawPixel ignores pixels outside the clip window and display
            if (px >= 0 && py >= 0) u8g2.drawPixel(px, py);
          }
        }
      }
    }
    x += g->adv;
  }
  return x;
}
