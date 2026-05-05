#ifndef PD_TEXT_H
#define PD_TEXT_H
/*
 * pd_text.h — shared 5×7 pixel font for Pocket-DAW plugin Page 0 UI.
 *
 * Header-only. All symbols are `static inline` so each plugin's
 * translation unit gets its own copy — no link-time sharing, no
 * duplicate-symbol risk between plugin .so/.dll files.
 *
 * Glyph format: each character is 5 columns wide × 7 rows tall. Each
 * byte in the 5-byte glyph table is one column, with bit 0 = top
 * pixel. Characters advance 6 px (5 + 1 px gap). Ported verbatim from
 * `plugins/fx/jt-sidechain/jt-sidechain.c` (the `SC_DIG` / `SC_ALPHA`
 * tables) so existing plugins that already use that format render
 * identically when switched to this header.
 *
 * Covers:
 *   0-9, A-Z (case-insensitive — a-z folds to A-Z)
 *   punctuation: ':', '-', '.', space
 *
 * Basic usage:
 *   #include "../../pd_text.h"       // from plugins/synths/<name>/
 *   ...
 *   // Plain string
 *   pdt_drawStr(r, 10, 20, "JT SYNTH");
 *
 *   // Coloured string — caller doesn't need to set draw colour
 *   pdt_drawStrC(r, 10, 20, 255, 30, 60, 255, "JT SYNTH");
 *
 *   // Scaled title (scale 2 = 10×14 px glyphs, 12 px advance)
 *   pdt_drawStrScale(r, 10, 2, "PITCHER", 2);
 *
 *   // Numeric value
 *   pdt_drawNum(r, 50, 20, 127);
 *
 *   // Pre-compute width for right-align
 *   int w = pdt_strWidth("BPM", 1);  // 17 (3 chars × 6 − 1 trailing gap)
 *
 * Colours: the non-coloured variants draw whatever the caller last set
 * via `SDL_SetRenderDrawColor`. The `*C` variants set the colour
 * internally and do not restore the previous colour — call them last
 * or re-set your drawing colour afterwards.
 */

/* Callers must include sdk/pocketdaw.h (or plugins/pdsynth_api.h which
 * redirects to it) BEFORE including this header — pdt_drawParamCell takes
 * a PdDrawContext* argument and pdt_drawParamCell's body calls
 * pd_register_control, both of which are declared in pocketdaw.h. */
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>

/* ── Glyph tables (ported from jt-sidechain.c lines 352-387) ─────── */

static const uint8_t PDT_DIG[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x62,0x51,0x49,0x49,0x46}, /* 2 */
    {0x22,0x41,0x49,0x49,0x36}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
};

static const uint8_t PDT_ALPHA[26][5] = {
    {0x7E,0x09,0x09,0x09,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x3A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x26,0x49,0x49,0x49,0x32}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x10,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};

/* ── Basic 1× glyph draw ─────────────────────────────────────────── */

static inline void pdt_drawGlyph(SDL_Renderer* r, int x, int y, char c) {
    const uint8_t* g = NULL;
    if (c >= '0' && c <= '9') g = PDT_DIG[c - '0'];
    else if (c >= 'A' && c <= 'Z') g = PDT_ALPHA[c - 'A'];
    else if (c >= 'a' && c <= 'z') g = PDT_ALPHA[c - 'a'];
    else if (c == ':') {
        SDL_RenderDrawPoint(r, x + 2, y + 1);
        SDL_RenderDrawPoint(r, x + 2, y + 5);
        return;
    } else if (c == '-') {
        for (int i = 1; i < 4; i++) SDL_RenderDrawPoint(r, x + i, y + 3);
        return;
    } else if (c == '.') {
        SDL_RenderDrawPoint(r, x + 2, y + 6);
        return;
    } else if (c == ' ') {
        return;
    } else {
        return;
    }
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) SDL_RenderDrawPoint(r, x + col, y + row);
        }
    }
}

/* ── Scaled glyph draw (scale 1..N). Scale > 1 uses filled rects
 * instead of points so large titles stay crisp on the pixel grid. ── */
static inline void pdt_drawGlyphScale(SDL_Renderer* r, int x, int y, char c, int scale) {
    if (scale <= 1) { pdt_drawGlyph(r, x, y, c); return; }
    const uint8_t* g = NULL;
    int punct = 0;
    if (c >= '0' && c <= '9') g = PDT_DIG[c - '0'];
    else if (c >= 'A' && c <= 'Z') g = PDT_ALPHA[c - 'A'];
    else if (c >= 'a' && c <= 'z') g = PDT_ALPHA[c - 'a'];
    else if (c == ':' || c == '-' || c == '.' || c == ' ') punct = 1;
    else return;

    if (punct) {
        if (c == ' ') return;
        SDL_Rect pr;
        if (c == ':') {
            pr.x = x + 2 * scale; pr.y = y + 1 * scale; pr.w = scale; pr.h = scale;
            SDL_RenderFillRect(r, &pr);
            pr.y = y + 5 * scale;
            SDL_RenderFillRect(r, &pr);
        } else if (c == '-') {
            pr.x = x + 1 * scale; pr.y = y + 3 * scale; pr.w = 3 * scale; pr.h = scale;
            SDL_RenderFillRect(r, &pr);
        } else if (c == '.') {
            pr.x = x + 2 * scale; pr.y = y + 6 * scale; pr.w = scale; pr.h = scale;
            SDL_RenderFillRect(r, &pr);
        }
        return;
    }
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) {
                SDL_Rect pr = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(r, &pr);
            }
        }
    }
}

/* ── String draw at 1× ──────────────────────────────────────────── */

static inline void pdt_drawStr(SDL_Renderer* r, int x, int y, const char* s) {
    if (!s) return;
    int cx = x;
    for (int i = 0; s[i]; i++) {
        pdt_drawGlyph(r, cx, y, s[i]);
        cx += 6;
    }
}

/* Coloured variant — sets the renderer draw colour before drawing and
 * leaves it set. */
static inline void pdt_drawStrC(SDL_Renderer* r, int x, int y,
                                uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                                const char* s) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    pdt_drawStr(r, x, y, s);
}

/* ── String draw at N× scale ───────────────────────────────────── */

static inline void pdt_drawStrScale(SDL_Renderer* r, int x, int y,
                                    const char* s, int scale) {
    if (!s) return;
    if (scale <= 1) { pdt_drawStr(r, x, y, s); return; }
    int cx = x;
    int advance = 6 * scale;
    for (int i = 0; s[i]; i++) {
        pdt_drawGlyphScale(r, cx, y, s[i], scale);
        cx += advance;
    }
}

static inline void pdt_drawStrScaleC(SDL_Renderer* r, int x, int y,
                                     uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                                     const char* s, int scale) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    pdt_drawStrScale(r, x, y, s, scale);
}

/* ── Numeric convenience ────────────────────────────────────────── */

static inline void pdt_drawNum(SDL_Renderer* r, int x, int y, int val) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", val);
    pdt_drawStr(r, x, y, buf);
}

static inline void pdt_drawNumC(SDL_Renderer* r, int x, int y,
                                uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                                int val) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    pdt_drawNum(r, x, y, val);
}

/* ── Width calculation (for right-alignment / centering) ────────── */

static inline int pdt_strWidth(const char* s, int scale) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) n++;
    if (n == 0) return 0;
    /* n characters × 6 px advance, but the trailing gap after the last
     * character isn't drawn, so effective width is 6·n − 1. */
    int base = 6 * n - 1;
    return scale > 1 ? base * scale : base;
}

/* ── Filled rect helper (most plugins already have their own; this
 * convenience lets pdt_drawParamCell below work without a callback) ── */
static inline void pdt_fillRect(SDL_Renderer* r, int x, int y, int w, int h) {
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(r, &rc);
}
static inline void pdt_drawRect(SDL_Renderer* r, int x, int y, int w, int h) {
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderDrawRect(r, &rc);
}

/* ── Standard param-strip cell.
 *
 * Renders one cell of a plugin's Page 0 param strip: dim background
 * box, accent border (brighter + bright white when selected / editing),
 * centred 3-char label at the top, horizontal value-bar at the bottom.
 *
 *   ctx            — PdDrawContext; used to auto-register the cell's
 *                    bounds with the host for spatial nav + mouse
 *                    hit-test (SDK v4.5). NULL is safe — registration
 *                    becomes a no-op, drawing still works.
 *   paramIdx       — plugin param index this cell edits; forwarded
 *                    to the registration entry.
 *   cx, cy, cw, ch — cell bounds
 *   label          — short label string (typically 3 chars)
 *   value          — normalized 0..1
 *   isSel          — nonzero when this cell is ctx->selectedParam
 *   isEdit         — nonzero when ctx->editMode is active on this cell
 *   accR/accG/accB — plugin's accent colour for selection highlight
 *
 * Every existing Page 0 that uses this helper gets a visually
 * consistent strip, the same selection/edit feedback, and — with the
 * new ctx + paramIdx arguments — automatic registration so the host's
 * spatial nav + mouse hit-test work with zero per-plugin wiring.
 * ── */
static inline void pdt_drawParamCell(SDL_Renderer* r,
                                     const PdDrawContext* ctx,
                                     int paramIdx,
                                     int cx, int cy, int cw, int ch,
                                     const char* label, float value,
                                     int isSel, int isEdit,
                                     uint8_t accR, uint8_t accG, uint8_t accB) {
    /* Auto-register the cell's interactive region so the host's
     * pluginNav NavGroup can spatial-nav + hit-test into it. Safe
     * on NULL ctx (pre-v4.5 hosts or non-draw-context callers). */
    pd_register_control(ctx, paramIdx, cx, cy, cw, ch);
    /* Background fill — bright when selected/editing, otherwise a dim
     * neutral box. Editing uses white tint so it always pops regardless
     * of the plugin's accent colour. */
    if (isEdit) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 60);
        pdt_fillRect(r, cx, cy, cw, ch);
    } else if (isSel) {
        SDL_SetRenderDrawColor(r, accR, accG, accB, 50);
        pdt_fillRect(r, cx, cy, cw, ch);
    } else {
        SDL_SetRenderDrawColor(r, 12, 12, 18, 255);
        pdt_fillRect(r, cx, cy, cw, ch);
    }

    /* Border — bright white when editing, accent when selected, dim
     * accent otherwise. */
    uint8_t bR = isEdit ? 255 : accR;
    uint8_t bG = isEdit ? 255 : accG;
    uint8_t bB = isEdit ? 255 : accB;
    uint8_t bA = isEdit ? 255 : (isSel ? 220 : 70);
    SDL_SetRenderDrawColor(r, bR, bG, bB, bA);
    pdt_drawRect(r, cx, cy, cw, ch);

    /* Centred label. Uses the 1× 5×7 font (≈6px per char advance). */
    if (label && label[0]) {
        int lw = pdt_strWidth(label, 1);
        int lx = cx + (cw - lw) / 2;
        int ly = cy + 4;
        uint8_t lR = isSel ? 255 : 200;
        uint8_t lG = isSel ? 255 : 200;
        uint8_t lB = isSel ? 255 : 200;
        pdt_drawStrC(r, lx, ly, lR, lG, lB, 255, label);
    }

    /* Value bar — horizontal fill at the bottom of the cell. */
    int barH = 6;
    int bx = cx + 3;
    int bw = cw - 6;
    int by = cy + ch - barH - 3;
    if (bw < 2) bw = 2;
    SDL_SetRenderDrawColor(r, 16, 16, 22, 255);
    pdt_fillRect(r, bx, by, bw, barH);
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    int fillW = (int)(value * (bw - 2));
    uint8_t fR = isEdit ? 255 : accR;
    uint8_t fG = isEdit ? 255 : accG;
    uint8_t fB = isEdit ? 255 : accB;
    SDL_SetRenderDrawColor(r, fR, fG, fB, isSel ? 240 : 160);
    pdt_fillRect(r, bx + 1, by + 1, fillW, barH - 2);
}

/* ── pdt_drawToggleCell ───────────────────────────────────────────────
 * Same calling convention as pdt_drawParamCell, but renders a binary
 * ON/OFF switch instead of a value bar. Use this for params declared
 * with `"widget": "toggle"` in the manifest — makes it instantly
 * readable whether the FX / mode is engaged, and removes the temptation
 * to try to drag a boolean like it's a knob.
 *
 * Visual: label on top (typically 3 chars), big "ON"/"OFF" word beneath
 * it. The entire cell fills with the accent colour when ON, stays in
 * the neutral dim colour when OFF. Selection + edit states styled the
 * same as pdt_drawParamCell so focus feedback is identical.
 * ── */
static inline void pdt_drawToggleCell(SDL_Renderer* r,
                                      const PdDrawContext* ctx,
                                      int paramIdx,
                                      int cx, int cy, int cw, int ch,
                                      const char* label, float value,
                                      int isSel, int isEdit,
                                      uint8_t accR, uint8_t accG, uint8_t accB) {
    /* Auto-register for spatial nav + mouse hit-test, same as pdt_drawParamCell. */
    pd_register_control(ctx, paramIdx, cx, cy, cw, ch);

    int on = (value >= 0.5f) ? 1 : 0;

    /* Cell background. ON → accent-filled; OFF → dim neutral. Selection
     * and edit states dominate — they need to be obvious for focus feedback. */
    if (isEdit) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
    } else if (isSel && on) {
        SDL_SetRenderDrawColor(r, accR, accG, accB, 220);
    } else if (on) {
        SDL_SetRenderDrawColor(r, accR, accG, accB, 150);
    } else if (isSel) {
        SDL_SetRenderDrawColor(r, accR, accG, accB, 45);
    } else {
        SDL_SetRenderDrawColor(r, 12, 12, 18, 255);
    }
    pdt_fillRect(r, cx, cy, cw, ch);

    /* Border styling mirrors pdt_drawParamCell's logic. */
    uint8_t bR = isEdit ? 255 : accR;
    uint8_t bG = isEdit ? 255 : accG;
    uint8_t bB = isEdit ? 255 : accB;
    uint8_t bA = isEdit ? 255 : (isSel ? 230 : (on ? 220 : 80));
    SDL_SetRenderDrawColor(r, bR, bG, bB, bA);
    pdt_drawRect(r, cx, cy, cw, ch);

    /* Label at the top. */
    if (label && label[0]) {
        int lw = pdt_strWidth(label, 1);
        int lx = cx + (cw - lw) / 2;
        int ly = cy + 4;
        uint8_t lR = (on || isSel) ? 255 : 200;
        uint8_t lG = (on || isSel) ? 255 : 200;
        uint8_t lB = (on || isSel) ? 255 : 200;
        pdt_drawStrC(r, lx, ly, lR, lG, lB, 255, label);
    }

    /* ON / OFF text underneath, centred. Uppercase for legibility on
     * small screens; bright when ON or selected, dim when idle OFF. */
    const char* state = on ? "ON" : "OFF";
    int sw = pdt_strWidth(state, 1);
    int sx = cx + (cw - sw) / 2;
    int sy = cy + ch - 11;
    uint8_t tR, tG, tB, tA;
    if (on) {
        /* ON: inverse — dark text on bright cell — maximum contrast. */
        tR = 10; tG = 10; tB = 14; tA = 255;
    } else {
        tR = accR; tG = accG; tB = accB; tA = isSel ? 230 : 150;
    }
    pdt_drawStrC(r, sx, sy, tR, tG, tB, tA, state);
}

#endif /* PD_TEXT_H */
