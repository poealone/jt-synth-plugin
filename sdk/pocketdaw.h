/**
 * ╔═══════════════════════════════════════════════════════════╗
 * ║          PocketDAW Plugin SDK — Unified Header            ║
 * ║                  Version 3.0                              ║
 * ╠═══════════════════════════════════════════════════════════╣
 * ║  One header. Synths. FX. Visuals. The whole kitchen.      ║
 * ╚═══════════════════════════════════════════════════════════╝
 *
 * QUICK START
 * ───────────
 * 1. #include "pocketdaw.h"
 * 2. Build your plugin (synth or FX)
 * 3. Cross-compile:
 *      aarch64-none-linux-gnu-gcc -shared -fPIC -O2 -o my-plugin.so my-plugin.c -lm
 * 4. Add a manifest.json alongside it
 * 5. Drop into plugins/synths/<name>/ or plugins/fx/<name>/
 * 6. Launch PocketDAW — it appears automatically
 *
 * WHAT YOU CAN BUILD
 * ──────────────────
 *  Synths     — Instruments played from MIDI tracks
 *               Implement: PD_SYNTH_* exports
 *               Install:   plugins/synths/my-synth/
 *
 *  Effects    — Audio processors in mixer strips
 *               Implement: PD_FX_* exports
 *               Install:   plugins/fx/my-effect/
 *
 *  Visuals    — Custom animated graphics on the plugin editor page
 *               Implement: pdsynth_draw() returning 1 to own the screen
 *               Access:    PdDrawContext → scope buffers, peaks, note state
 *               Works for: Both synths and effects
 *
 * HOST PRIMITIVES (available to all plugins via PdSynthHostV3)
 * ─────────────────────────────────────────────────────────────
 *  osc_sample()        — 6 waveform oscillators
 *  wavetable_sample()  — 3-type wavetable with morphing
 *  filter_sample()     — State-variable filter (LP/HP/BP/Notch)
 *  adsr_tick()         — 4-stage ADSR envelope
 *  fx_process()        — Reverb, delay, chorus, distortion, bitcrush
 *
 * TARGET PLATFORM
 * ───────────────
 *  Device:    RG35XX (aarch64 Linux, MUOS)
 *  Screen:    320×240 (rendered 2x = 640×480 logical)
 *  Audio:     44100 Hz, stereo float32
 *  Renderer:  SDL2 (for custom visuals in pdsynth_draw)
 *  SDK:       C99 — no C++ required
 *
 * VERSION HISTORY
 * ───────────────
 *  v1   — Core synth API (create/process/note/params)
 *  v2   — Sample loading, host callbacks, FX sidechain
 *  v2.1 — Full-screen FX editor, accent color, GR meter
 *  v3   — Host synth primitives, custom draw, reactive visuals,
 *          unified header (this file), PD Synth reference plugin
 *  v3.1 — Desktop mouse/touch support (host-side, no new plugin exports)
 *          Knob click-to-focus, vertical drag to adjust (0.005/px),
 *          all overlay menus clickable, piano roll mouse editing
 *  v4.0 — Transport + MIDI access for plugins (PdHostV4, PdFxHostV2)
 *          pdfx_transport_changed, pdfx_draw_overlay, editor overlay context
 *  v4.1 — Audio host access (PdHostAudio): device enumeration, track audio,
 *          capture API (open/close/read), write_to_sample_slot
 *  v4.2 — Visual engine integration (PdHostViz): plugin layers, waveform/scope,
 *          plugin scope restriction (MIDI vs Sample editor)
 *  v4.3 — Pattern resize (get/set_pattern_step_count), transport_changed
 *          callbacks fired by host, armed recording workflow, pdfx_draw export
 */

#ifndef POCKETDAW_H
#define POCKETDAW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════
 * VERSION
 * ═══════════════════════════════════════════════════════════ */

#define PD_SDK_VERSION          4       /* Major SDK version */
#define PD_SDK_VERSION_MINOR    3       /* v4.3 — capture, transport callbacks, pattern resize */
#define PDSYNTH_API_VERSION     3       /* Synth API version */
#define PDSYNTH_API_VERSION_MIN 1       /* Minimum compatible synth version */
#define PDFX_API_VERSION        2       /* FX API version */
#define PDFX_API_MINOR          3       /* v2.3: audio ring-buffer pattern for waveform display */

/* ═══════════════════════════════════════════════════════════
 * LIMITS
 * ═══════════════════════════════════════════════════════════ */

#define PD_MAX_PARAMS      64           /* Max params per plugin (v5.1: bumped from 32 for grouped-FX plugins) */
#define PD_MAX_FX_PARAMS   16           /* Max params per FX plugin */
#define PD_MAX_SAMPLES    128           /* Max samples a synth plugin can load */
#define PD_MAX_NAME        64           /* Max length of name strings */
#define PD_MAX_SAMPLE_SLOTS 16          /* Audio sample slots (0-15) */
#define PD_MAX_PATTERN_STEPS 64         /* Maximum steps per pattern */
#define PD_VIZ_BUF_LEN    128          /* Samples in the scope ring buffer */

/* ═══════════════════════════════════════════════════════════
 * PLUGIN SCOPE — v4.2 / v5.1 (insertion-context restriction)
 * ═══════════════════════════════════════════════════════════
 * Declared in manifest.json "scope" field. Bitmask flags — host shows
 * the plugin only in pickers whose context bit overlaps the plugin scope.
 * Default (0 or omitted) = PD_SCOPE_UNIVERSAL (all contexts).
 *
 * Manifest accepts a single token ("midi", "sample", "mixer") or a
 * comma-separated list ("mixer,sample"). Omitted = universal.
 */
#define PD_SCOPE_MIDI       0x01    /* MIDI Edit insert FX (per-MIDI-track)        */
#define PD_SCOPE_SAMPLE     0x02    /* Sample Edit insert FX (per-sample-track)    */
#define PD_SCOPE_MIXER      0x04    /* Mixer strip FX (post-fader, pre-bus)        */
#define PD_SCOPE_UNIVERSAL  0x07    /* All insertion contexts (default)            */

/* Legacy aliases (backward compat with old pdsynth_api.h includes) */
#define PDSYNTH_MAX_PARAMS  PD_MAX_PARAMS
#define PDSYNTH_MAX_NAME    PD_MAX_NAME
#define PDSYNTH_MAX_SAMPLES PD_MAX_SAMPLES
#define PDFX_MAX_PARAMS     PD_MAX_FX_PARAMS
#define PDFX_MAX_NAME       PD_MAX_NAME

/* ═══════════════════════════════════════════════════════════
 * SCREEN & AUDIO CONSTANTS
 * ═══════════════════════════════════════════════════════════ */

#define PD_SCREEN_W      320    /* Logical screen width */
#define PD_SCREEN_H      240    /* Logical screen height */
#define PD_SAMPLE_RATE   44100  /* Host audio sample rate */

/* ═══════════════════════════════════════════════════════════
 * WIDGET TYPES   (manifest "widget" field — v3.2)
 * ═══════════════════════════════════════════════════════════
 * Used in manifest.json param definitions:
 *   { "name": "Attack", "default": 0.01, "widget": "knob" }
 *   { "name": "Bypass", "default": 0.0,  "widget": "toggle" }
 *   { "name": "Mode",   "default": 0.0,  "widget": "select",
 *     "options": ["Mono","Stereo","Mid-Side"] }
 *
 * The host renders and handles all mouse interaction.
 * Plugin creators only implement set_param/get_param.
 */

#define PD_WIDGET_KNOB      0   /* Rotary knob (default) */
#define PD_WIDGET_SLIDER    1   /* Horizontal slider bar */
#define PD_WIDGET_TOGGLE    2   /* On/off switch (0.0 or 1.0) */
#define PD_WIDGET_SELECT    3   /* Cycle through named options */
#define PD_MAX_OPTIONS      8   /* Max options for SELECT widget */
#define PD_MAX_OPTION_LEN  16   /* Max chars per option label */

/* ═══════════════════════════════════════════════════════════
 * CONTROL ORIENTATION & NAVIGATION — v5.0
 * ═══════════════════════════════════════════════════════════
 * Declares how a control behaves spatially:
 *   - orientation:  physical axis the control value moves along
 *   - valueAxis:    which D-pad axis adjusts the value
 *   - focusAxis:    which D-pad axis moves between sibling controls
 *
 * Plugins declare these per-param in manifest.json:
 *   { "name": "Threshold", "widget": "slider",
 *     "orientation": "vertical", "navGroup": "sliders", "navOrder": 0 }
 *
 * The host auto-infers sensible defaults when fields are omitted.
 */

/* Control orientation */
#define PD_ORIENT_NONE        0   /* No spatial orientation (buttons, toggles) */
#define PD_ORIENT_VERTICAL    1   /* Vertical slider: value changes on Y axis */
#define PD_ORIENT_HORIZONTAL  2   /* Horizontal slider: value changes on X axis */
#define PD_ORIENT_RADIAL      3   /* Knob: value changes via rotation */

/* D-pad axis mapping for value/focus */
#define PD_AXIS_NONE          0   /* No axis (A/B button toggles) */
#define PD_AXIS_UP_DOWN       1   /* UP increases, DOWN decreases */
#define PD_AXIS_LEFT_RIGHT    2   /* RIGHT increases, LEFT decreases */

/* Navigation group layout direction */
#define PD_GROUP_HORIZONTAL   0   /* Controls flow left-to-right */
#define PD_GROUP_VERTICAL     1   /* Controls flow top-to-bottom */
#define PD_GROUP_GRID         2   /* 2D grid arrangement */

#define PD_MAX_NAV_GROUPS     8   /* Max navigation groups per plugin */
#define PD_NAV_GROUP_NAME_LEN 16  /* Max chars in group name */

/** Per-control navigation descriptor.
 *  Host builds these from manifest metadata or auto-infers from widget type. */
typedef struct {
    int orientation;   /* PD_ORIENT_* */
    int valueAxis;     /* PD_AXIS_* — D-pad axis that adjusts value */
    int focusAxis;     /* PD_AXIS_* — D-pad axis that moves between controls */
    int navGroup;      /* Group index (0 .. PD_MAX_NAV_GROUPS-1), -1 = ungrouped */
    int navOrder;      /* Position within group (0-based) */
} PdControlNav;

/** Navigation group descriptor.
 *  Defines how a set of controls are spatially arranged. */
typedef struct {
    char name[PD_NAV_GROUP_NAME_LEN];
    int  layout;       /* PD_GROUP_HORIZONTAL / VERTICAL / GRID */
    int  wrap;         /* 1 = wrap around at ends, 0 = clamp */
    int  cols;         /* Column count for PD_GROUP_GRID (ignored otherwise) */
} PdNavGroup;

/* ═══════════════════════════════════════════════════════════
 * EDIT MODE / FOCUS STATE — v5.0
 * ═══════════════════════════════════════════════════════════
 * Used in PdDrawContext.editMode to communicate the current
 * gamepad interaction state to custom-draw plugins.
 *
 *   PD_EDIT_NAVIGATE    — D-pad moves focus between control groups.
 *                          Plugin should show highlight on selectedParam.
 *   PD_EDIT_PARAM_SEL   — User selected a group/band; D-pad picks which
 *                          parameter within the group to edit.
 *   PD_EDIT_VALUE       — D-pad adjusts the focused control's value.
 *                          Plugin should show an "editing" indicator.
 *
 * Plugins read ctx->editMode to decide how to render the
 * selected parameter (highlight ring vs. active glow, etc.).
 */
#define PD_EDIT_NAVIGATE    0  /* Group focus: D-pad navigates, A confirms */
#define PD_EDIT_PARAM_SEL   2  /* Param select: D-pad picks param in group, A edits, B back */
#define PD_EDIT_VALUE       1  /* Editing: D-pad adjusts value, B cancels */

/* ═══════════════════════════════════════════════════════════
 * OSCILLATOR TYPES   (pd_osc_sample / PdSynthHostV3)
 * ═══════════════════════════════════════════════════════════ */

#define PD_OSC_SINE      0
#define PD_OSC_SAW       1
#define PD_OSC_SQUARE    2
#define PD_OSC_TRIANGLE  3
#define PD_OSC_NOISE     4
#define PD_OSC_PULSE     5

/* ═══════════════════════════════════════════════════════════
 * FILTER TYPES   (filter_sample / PdSynthHostV3)
 * ═══════════════════════════════════════════════════════════ */

#define PD_FILTER_NONE      0
#define PD_FILTER_LOWPASS   1
#define PD_FILTER_HIGHPASS  2
#define PD_FILTER_BANDPASS  3
#define PD_FILTER_NOTCH     4

/* ═══════════════════════════════════════════════════════════
 * FX TYPES   (fx_process / PdSynthHostV3)
 * ═══════════════════════════════════════════════════════════ */

#define PD_FX_REVERB      0   /* params: [mix, size, damp, width] */
#define PD_FX_DELAY       1   /* params: [time, feedback, mix] */
#define PD_FX_CHORUS      2   /* params: [rate, depth, mix] */
#define PD_FX_DISTORTION  3   /* params: [drive, tone, mix] */
#define PD_FX_BITCRUSH    4   /* params: [bits, rate, mix] */

/* ═══════════════════════════════════════════════════════════
 * CORE TYPES — Shared by synth and FX plugins
 * ═══════════════════════════════════════════════════════════ */

/** Opaque plugin instance handles */
typedef void* PdSynthInstance;
typedef void* PdFxInstance;

/** Stateful filter — one per voice per channel. Zero-init to reset. */
typedef struct {
    float z1, z2;
    float prevIn, prevOut;
} PdFilterState;

/* ═══════════════════════════════════════════════════════════
 * VISUAL CONTEXT — Reactive graphics for both synths and FX
 * ═══════════════════════════════════════════════════════════
 *
 * Passed to pdsynth_draw() each frame when the plugin editor is open.
 * Use ctx->renderer (cast to SDL_Renderer*) with SDL2 to draw anything.
 * Return 1 to own the entire graphics page (suppresses host knobs).
 * Return 0 to draw behind the host-rendered knobs.
 *
 * The scope buffers contain LIVE audio from the track output — use them
 * to draw oscilloscopes, spectrum displays, VU meters, animations, etc.
 *
 * ── Desktop Mouse/Touch Interaction (v3.1) ────────────────
 * On desktop builds the host handles plugin editor mouse input:
 *
 * Knob layout (≤12 params, layout="knobs"):
 *   - Left-click on a knob → focus/select that parameter
 *   - Click-and-drag vertically → adjusts value (0.005 per pixel upward)
 *   - Scroll wheel on focused knob → fine adjustment
 *
 * Slider layout (>12 params):
 *   - Click on a row → select parameter
 *   - Drag on the value bar → adjusts value
 *
 * Custom draw page (pdsynth_draw returning 1):
 *   - The host does NOT intercept clicks on the custom area
 *   - Your plugin owns the entire page — handle mouse via ctx if needed
 *   - ctx->x/y/w/h defines the drawable bounds (same area as input)
 */
typedef struct {
    /* ── Renderer ─────────────────────────────────────── */
    void*    renderer;      /* SDL_Renderer* — draw anything here */
    int      x, y;         /* Top-left of your draw area */
    int      w, h;         /* Width and height (fits 320×240 screen) */
    int      page;         /* 0=graphics page, 1=params page */
    uint32_t timeMs;       /* Milliseconds since launch (for animation) */

    /* ── Audio-Reactive Data ──────────────────────────── */
    float    peak;          /* Combined peak level 0-1 */
    float    peakL;         /* Left channel peak 0-1 */
    float    peakR;         /* Right channel peak 0-1 */

    /* Live oscilloscope — PD_VIZ_BUF_LEN samples of recent track audio.
     * Oldest sample at [0], newest at [scopeLen-1]. Range: -1.0 to 1.0. */
    const float* scopeBufL;    /* Left channel oscilloscope data */
    const float* scopeBufR;    /* Right channel oscilloscope data */
    int          scopeLen;     /* Number of valid samples (≤ PD_VIZ_BUF_LEN) */

    /* Spectrum data (FFT magnitudes). NULL if unavailable this frame. */
    const float* spectrumBins; /* Magnitude per bin 0-1 (low→high frequency) */
    int          spectrumLen;  /* Number of bins (0 if unavailable) */

    /* ── Note/Playback State ──────────────────────────── */
    int      noteOn;        /* 1 if any voice/note is currently active */
    float    lastNote;      /* Frequency (Hz) of last triggered note */
    int      lastMidi;      /* MIDI note number of last trigger (0-127) */
    int      voiceCount;    /* Active voice count */

    /* ── Mouse/Touch State (v3.1) ─────────────────────────── */
    /* CONTRACT: mouseX/mouseY are coordinates relative to (ctx->x, ctx->y) — */
    /* i.e. (0,0) is the top-left of this plugin's draw area. Hit-test against */
    /* your own local rects directly; do NOT add or subtract ctx->x/ctx->y.    */
    /* Current host build passes ctx->x = ctx->y = 0 (full-screen plugin pane), */
    /* so absolute and relative coincide today — but rely on the contract, not */
    /* the coincidence: ctx->x/y may become non-zero in future embedded views. */
    /* On device (RG35XX), mouseX/Y are always 0 and mouseDown is always 0.    */
    int      mouseX;        /* Cursor X relative to ctx->x       */
    int      mouseY;        /* Cursor Y relative to ctx->y       */
    int      mouseDown;     /* 1 if left button / finger held    */

    /* ── Navigation State ────────────────────────────────── */
    /* Host passes the current param cursor so the plugin can */
    /* highlight the active band/param in its custom UI.      */
    int      selectedParam; /* Active param index (0..paramCount-1), -1=none */
    int      editMode;      /* 0=navigating params, 1=focused on custom draw */

    /* ── Widget Info (v3.2) ────────────────────────────────── */
    int      paramWidgetType;  /* PD_WIDGET_* of selectedParam */
    int      paramOptionCount; /* Number of options (SELECT only) */

    /* ── Transport State (v3.3) ───────────────────────────── */
    /* Populated every frame from atomic transport state.       */
    /* Old plugins (v3.2) never read past paramOptionCount.     */
    float    bpm;                /* Current BPM (60-240)                        */
    int      currentStep;        /* Playback step (0-based 16ths), -1 if stopped */
    int      stepsPerBar;        /* Steps per bar (e.g. 16 for 4/4)             */
    int      isPlaying;          /* 1 if transport playing, 0 if stopped        */
    int64_t  playbackPosSamples; /* Position in samples from start              */
    int      timeSigNum;         /* Time signature numerator (2-7)              */
    int      timeSigDen;         /* Time signature denominator (4 or 8)         */
    int      isLooping;          /* 1 if looping enabled                        */

    /* ── Track Context (v3.3) ─────────────────────────────── */
    int      trackIndex;         /* Which track this plugin is on (-1 = unknown) */
    int      trackCount;         /* Total tracks in current pattern              */

    /* ── Sidechain Context (v3.4) ─────────────────────────── */
    int      sidechainSource;    /* Track providing sidechain input (-1 = none)  */

    /* ── Control Registration (v4.5) ─────────────────────────
     * Per-frame buffer the host provides for plugins to report
     * the pixel bounds of each interactive region they drew.
     * The host then builds a spatial nav graph and mouse hit-test
     * table from the registered entries.
     *
     * Plugins call pd_register_control(ctx, paramIdx, x, y, w, h)
     * from inside pdsynth_draw / pdfx_draw — the helper below
     * handles null-checking so safe on pre-v4.5 hosts that pass
     * NULL here.
     *
     * Plugins built against older SDKs leave this field
     * uninitialised on the wire — the host zero-inits the struct
     * so reads return NULL in that case, and the host then falls
     * back to the manifest NavTable path.  Fully additive;
     * no ABI break for existing binaries. */
    struct PdControlReg* controlReg;
} PdDrawContext;

/* ═══════════════════════════════════════════════════════════
 * CONTROL REGISTRATION — v4.5
 * ═══════════════════════════════════════════════════════════
 *
 * Plugins that draw custom Page 0 UIs tell the host where each
 * interactive region lives by calling pd_register_control() from
 * inside their draw function.  The host then:
 *
 *   - builds a spatial nav graph (nearest-neighbour by direction)
 *     so D-pad navigation jumps between controls geometrically
 *     instead of following hardcoded per-plugin neighbour links
 *   - hit-tests mouse clicks against the registered rects so the
 *     desktop cursor works on plugin Page 0 without every plugin
 *     reinventing its own click handler
 *
 * Plugins that don't register anything (legacy plugins, or ones
 * that handle their own mouse internally) keep working via the
 * existing manifest NavTable + plugin-internal hit-test path.
 * Registration is additive, not mandatory.
 *
 * The shared helper `plugins/pd_text.h`'s `pdt_drawParamCell()`
 * already registers the cell it draws, so any plugin that uses
 * it gets spatial nav + cursor for free — no code change needed
 * beyond passing `ctx` and `paramIdx` through the call.
 */

#define PD_CONTROL_REG_MAX 64

typedef struct {
    int paramIdx;    /* plugin param index this region edits */
    int x, y, w, h;  /* pixel bounds in the draw canvas */
} PdControlRegEntry;

typedef struct PdControlReg {
    int count;       /* plugin writes: number of entries in use */
    int max;         /* host writes: capacity of entries[] */
    PdControlRegEntry entries[PD_CONTROL_REG_MAX];
} PdControlReg;

/* Register one interactive region. Safe to call with ctx == NULL
 * or ctx->controlReg == NULL (older hosts); it becomes a no-op so
 * plugins don't need to null-check every call site. */
static inline void pd_register_control(const PdDrawContext* ctx, int paramIdx,
                                       int x, int y, int w, int h) {
    if (!ctx || !ctx->controlReg) return;
    PdControlReg* reg = ctx->controlReg;
    if (reg->count >= reg->max) return;
    PdControlRegEntry* e = &reg->entries[reg->count++];
    e->paramIdx = paramIdx;
    e->x = x;
    e->y = y;
    e->w = w;
    e->h = h;
}

/* ═══════════════════════════════════════════════════════════
 * HOST TRANSPORT — v4.0 (read-only transport + trigger actions)
 * ═══════════════════════════════════════════════════════════
 *
 * Provided via pdsynth_set_host_v4() / pdfx_set_host_v2().
 * All getters read atomic state — safe from audio or UI thread.
 * Action triggers (play/stop) are deferred to the main thread.
 */
typedef struct {
    void* hostData;

    float (*get_bpm)(void* hostData);                  /* Current BPM (60-240)            */
    int   (*is_playing)(void* hostData);               /* 1 if playing, 0 if stopped      */
    int   (*get_current_step)(void* hostData);         /* Playback step (0-based), -1=off */
    int   (*get_steps_per_bar)(void* hostData);        /* Steps per bar (e.g. 16)         */
    int   (*get_time_sig_num)(void* hostData);         /* Time sig numerator (2-7)        */
    int   (*get_time_sig_den)(void* hostData);         /* Time sig denominator (4 or 8)   */
    int   (*is_looping)(void* hostData);               /* 1 if looping enabled            */
    int   (*is_song_mode)(void* hostData);             /* 1 if arrangement, 0 if pattern  */
    int   (*get_playback_pos_samples)(void* hostData); /* Position in samples from start  */
    int   (*get_sample_rate)(void* hostData);          /* e.g. 44100                      */
    void  (*request_play)(void* hostData);             /* Deferred play (next frame)      */
    void  (*request_stop)(void* hostData);             /* Deferred stop (next frame)      */
    /* v4.3 — Pattern resizing */
    int   (*get_pattern_step_count)(void* hostData);   /* Current pattern step count       */
    void  (*set_pattern_step_count)(void* hostData, int steps); /* Expand/shrink pattern  */
} PdHostTransport;

/* ═══════════════════════════════════════════════════════════
 * MIDI EVENT — Unified event for input/output (v4.0)
 * ═══════════════════════════════════════════════════════════ */

#define PD_MIDI_NOTE_ON      0x01
#define PD_MIDI_NOTE_OFF     0x02
#define PD_MIDI_CC           0x03
#define PD_MIDI_PITCH_BEND   0x04
#define PD_MAX_MIDI_EVENTS   256

typedef struct {
    uint8_t  type;      /* PD_MIDI_NOTE_ON / NOTE_OFF / CC / PITCH_BEND */
    uint8_t  channel;   /* MIDI channel 0-15 (0 = default)              */
    uint8_t  data1;     /* Note number (0-127) or CC number             */
    uint8_t  data2;     /* Velocity (0-127) or CC value                 */
    int32_t  offset;    /* Sample offset within current buffer          */
} PdMidiEvent;

/* ═══════════════════════════════════════════════════════════
 * HOST MIDI — v4.0 (note data read + MIDI output)
 * ═══════════════════════════════════════════════════════════
 *
 * Read functions access pattern data (UI/main thread only).
 * Output functions queue events via lock-free ring buffer
 * (safe from audio thread).
 */
typedef struct {
    void* hostData;

    /* ── Track MIDI Read (UI/main thread) ───────────────── */
    int  (*get_track_count)(void* hostData);
    int  (*get_note_count)(void* hostData, int trackIndex);
    int  (*get_note)(void* hostData, int trackIndex, int noteIndex,
                     int* outNote, int* outVelocity,
                     int* outStartStep, int* outDuration);

    /* ── MIDI Output (audio thread safe) ────────────────── */
    int  (*send_midi_event)(void* hostData, const PdMidiEvent* event);
    int  (*send_note_on)(void* hostData, int destTrack,
                         uint8_t note, uint8_t velocity, int32_t sampleOffset);
    int  (*send_note_off)(void* hostData, int destTrack,
                          uint8_t note, int32_t sampleOffset);
} PdHostMidi;

/* ═══════════════════════════════════════════════════════════
 * HOST AUDIO — v4.1 (track audio access, devices, recording)
 * ═══════════════════════════════════════════════════════════
 *
 * Provided via PdHostV4.audio / PdFxHostV2.audio (NULL on v4.0 hosts).
 * Track audio and peaks read from atomic scope ring buffers (thread-safe).
 * Device enumeration and capture are main-thread only.
 */

/* v4.5 — Sample slot metadata (returned by get_sample_slot_info) */
typedef struct {
    int         loaded;      /* 1 if slot has audio, 0 if empty */
    int         frames;      /* Total sample frames */
    int         sampleRate;  /* Sample rate of the audio engine */
    int         channels;    /* 1=mono, 2=stereo */
    const char* name;        /* Display name (e.g. "kick.wav", "REC-3") */
} PdSampleSlotInfo;

typedef struct {
    void* hostData;

    /* ── Track Audio Read (thread-safe) ─────────────────── */
    int   (*get_track_audio)(void* hostData, int trackIndex,
                             float* outL, float* outR, int maxFrames);
    float (*get_track_peak)(void* hostData, int trackIndex);
    float (*get_master_peak)(void* hostData);

    /* ── Sample Data Access (main thread) ───────────────── */
    int   (*get_sample_data)(void* hostData, int slot,
                             const float** outData, int* outFrames, int* outRate);
    int   (*get_sample_count)(void* hostData);

    /* ── Device Enumeration (main thread) ───────────────── */
    int         (*get_output_device_count)(void* hostData);
    const char* (*get_output_device_name)(void* hostData, int index);
    int         (*get_input_device_count)(void* hostData);
    const char* (*get_input_device_name)(void* hostData, int index);
    int         (*get_current_sample_rate)(void* hostData);
    int         (*get_current_buffer_size)(void* hostData);

    /* ── Recording / Capture ─────────────────────────────── */
    /** Open a capture device for recording/monitoring.
     *  Call when arming — keep open for input metering.
     *  Returns: 0 on success, -1 on failure.
     *  Thread: safe from any thread (uses atomic state). */
    int   (*open_capture)(void* hostData, int deviceIndex);

    /** Close the capture device. Call on disarm or plugin destroy. */
    void  (*close_capture)(void* hostData);

    /** Check if a capture device is currently open. Returns 1/0. */
    int   (*is_capturing)(void* hostData);

    /** Read captured audio. Dequeues from SDL capture queue.
     *  Handles S16/F32/U8 format conversion and mono→stereo expansion.
     *  Returns: number of frames read (0 = no data available, not an error).
     *  Thread: safe from any thread. */
    int   (*read_capture)(void* hostData, float* outL, float* outR, int maxFrames);

    /** Write float audio data to a sample slot (0 to PD_MAX_SAMPLE_SLOTS-1).
     *  Converts float→S16 PCM, creates Mix_Chunk, loads into audio engine.
     *  After writing, the waveform is visible in the sample editor.
     *  Returns: 0 on success, -1 on failure. */
    int   (*write_to_sample_slot)(void* hostData, int slot,
                                  const float* dataL, const float* dataR,
                                  int frames, int sampleRate);

    /* v4.4 — Track-level input routing */
    /** Get the input device assigned to a track (-1 = none). */
    int   (*get_track_input_device)(void* hostData, int trackIndex);
    /** Check if a track is armed for recording. Returns 1/0. */
    int   (*is_track_armed)(void* hostData, int trackIndex);
    /** Read captured audio from the track's shared buffer.
     *  The host reads from SDL once per audio frame and stores in a per-track
     *  buffer. Multiple plugins can read from this without contention.
     *  Returns number of frames available (0 if no data). */
    int   (*read_track_capture)(void* hostData, int trackIndex,
                                float* outL, float* outR, int maxFrames);

    /* v4.5 — Sample slot metadata + name access */
    /** Get metadata for a sample slot (0-15).
     *  Returns 0 on success, -1 if slot is invalid. */
    int         (*get_sample_slot_info)(void* hostData, int slot, PdSampleSlotInfo* info);
    /** Get the display name of a sample slot. Returns NULL if empty/invalid. */
    const char* (*get_sample_slot_name)(void* hostData, int slot);
} PdHostAudio;

/* ═══════════════════════════════════════════════════════════
 * VISUAL ENGINE LAYER API — v4.2
 * ═══════════════════════════════════════════════════════════
 *
 * All plugin visuals should go through the DAW's native VizEngine.
 * Plugins configure layers; the host renders them consistently.
 * This ensures unified performance, styling, and compositing.
 */

/* Layer types (match VizEngine internal enum) */
#define PD_VIZ_NONE       0
#define PD_VIZ_PULSE      1
#define PD_VIZ_BARS       2
#define PD_VIZ_RING       3
#define PD_VIZ_LINES      4
#define PD_VIZ_WAVEFORM   5
#define PD_VIZ_PARTICLES  6
#define PD_VIZ_SCANLINES  7
#define PD_VIZ_FLASH      8
#define PD_VIZ_DIAMOND    9
#define PD_VIZ_STARFIELD  10
#define PD_VIZ_SCOPE      11
#define PD_VIZ_GRID       12
#define PD_VIZ_RAIN       13

/* Color modes */
#define PD_VIZ_COLOR_SOLID    0
#define PD_VIZ_COLOR_RAINBOW  1
#define PD_VIZ_COLOR_REACTIVE 2
#define PD_VIZ_COLOR_CHANNEL  3

/* Anchor points */
#define PD_VIZ_ANCHOR_CENTER  0
#define PD_VIZ_ANCHOR_BOTTOM  1
#define PD_VIZ_ANCHOR_TOP     2
#define PD_VIZ_ANCHOR_LEFT    3
#define PD_VIZ_ANCHOR_RIGHT   4
#define PD_VIZ_ANCHOR_FULL    5

#define PD_VIZ_MAX_PLUGIN_LAYERS 4  /* Max layers per plugin instance */

typedef struct {
    int      type;           /* PD_VIZ_BARS, PD_VIZ_WAVEFORM, etc.       */
    uint8_t  color[4];       /* RGBA primary                              */
    uint8_t  color2[4];      /* RGBA secondary (REACTIVE blend target)    */
    int      colorMode;      /* PD_VIZ_COLOR_*                            */
    int      anchor;         /* PD_VIZ_ANCHOR_*                           */
    float    x, y;           /* Normalized position (0-1)                 */
    float    width, height;  /* Normalized size (0-1)                     */
    float    radius;         /* For circular elements (normalized)        */
    float    reactivity;     /* 0-3 how much audio drives visual          */
    float    smoothing;      /* 0-1 peak smoothing factor                 */
    float    threshold;      /* Min peak level to render (0-1)            */
    float    speed;          /* Animation speed multiplier                */
    float    rotation;       /* Rotation speed (radians/frame)            */
    int      count;          /* Element count (bars, particles, etc.)     */
    float    thickness;      /* Line/bar/particle thickness               */
    float    decay;          /* Fade rate (0-1)                           */
    int      fill;           /* 1=filled, 0=outline                       */
    int      mirror;         /* 1=horizontal mirror                       */
    int      pulse;          /* 1=size pulses with beat                   */
} PdVizLayerConfig;

/* Host viz layer management — all operations are main-thread only */
typedef struct {
    void* hostData;

    /** Add a visual layer to the VizEngine. Returns handle (0+), -1 if full.
     *  The layer is rendered by the engine alongside built-in preset layers. */
    int   (*add_layer)(void* hostData, const PdVizLayerConfig* config);

    /** Update an existing layer's configuration. Returns 0=ok, -1=invalid handle. */
    int   (*update_layer)(void* hostData, int handle, const PdVizLayerConfig* config);

    /** Remove a single layer by handle. */
    void  (*remove_layer)(void* hostData, int handle);

    /** Remove all layers owned by this plugin (called on destroy). */
    void  (*remove_all_layers)(void* hostData);

    /** Get current number of plugin layers active. */
    int   (*get_layer_count)(void* hostData);

    /** Set plugin visual brightness (0.0-1.0). */
    void  (*set_brightness)(void* hostData, float brightness);
} PdHostViz;

/* ═══════════════════════════════════════════════════════════
 * HOST V4 — Combined delivery struct for synth plugins
 * ═══════════════════════════════════════════════════════════
 *
 * Received via pdsynth_set_host_v4() after instance creation.
 * Includes transport, MIDI, audio, and viz access.
 * PdSynthHostV3 primitives remain on the separate set_host_v3() path.
 */
typedef struct {
    uint32_t              structSize;  /* sizeof(PdHostV4) — version guard */
    const PdHostTransport* transport;
    const PdHostMidi*      midi;
    const PdHostAudio*     audio;      /* v4.1 — NULL on v4.0 hosts */
    const PdHostViz*       viz;        /* v4.2 — NULL on v4.1 hosts */
} PdHostV4;

/* ═══════════════════════════════════════════════════════════
 * FX HOST V2 — Combined delivery struct for FX plugins
 * ═══════════════════════════════════════════════════════════
 *
 * Received via pdfx_set_host_v2() after instance creation.
 */
typedef struct {
    uint32_t              structSize;  /* sizeof(PdFxHostV2) — version guard */
    const PdHostTransport* transport;
    const PdHostMidi*      midi;
    const PdHostAudio*     audio;      /* v4.1 — NULL on v4.1 hosts */
    const PdHostViz*       viz;        /* v4.2 — NULL on v4.1 hosts */
} PdFxHostV2;

/* ═══════════════════════════════════════════════════════════
 * EDITOR OVERLAY CONTEXT — v4.0
 * ═══════════════════════════════════════════════════════════
 *
 * Passed to pdsynth_draw_overlay() / pdfx_draw_overlay() when
 * the host's piano roll or sample editor is visible and the
 * plugin is active on the track being edited.
 *
 * The plugin draws ON TOP of the editor grid (after notes/waveform,
 * before the cursor). Return 0 to let host draw cursor on top.
 * Return 1 to suppress the host cursor (plugin owns interaction).
 */

#define PD_EDITOR_PIANO_ROLL   0
#define PD_EDITOR_SAMPLE       1

typedef struct {
    uint32_t structSize;       /* sizeof(PdEditorOverlayCtx) — version guard */
    void*    renderer;         /* SDL_Renderer*                               */
    int      editorType;       /* PD_EDITOR_PIANO_ROLL or PD_EDITOR_SAMPLE    */

    /* ── Viewport (pixel coordinates) ───────────────────── */
    int      viewX, viewY;     /* Top-left of the editor grid area            */
    int      viewW, viewH;     /* Size of the editor grid area                */

    /* ── Piano Roll Data (editorType == PD_EDITOR_PIANO_ROLL) ── */
    int      scrollNote;       /* Bottom-most visible MIDI note               */
    int      scrollStep;       /* Left-most visible step                      */
    int      visibleNotes;     /* Number of visible note rows                 */
    int      visibleSteps;     /* Number of visible step columns              */
    int      totalSteps;       /* Total steps in the pattern                  */
    float    pixelsPerStep;    /* Horizontal pixels per step                  */
    float    pixelsPerNote;    /* Vertical pixels per note row                */

    /* ── Sample Editor Data (editorType == PD_EDITOR_SAMPLE) ─── */
    const float* waveformData; /* Sample PCM (left channel, -1 to 1)         */
    int      waveformLen;      /* Number of samples                           */
    float    trimStart;        /* Normalized start trim (0.0-1.0)             */
    float    trimEnd;          /* Normalized end trim (0.0-1.0)               */
    int      sampleRate;       /* Sample's sample rate                        */

    /* ── Transport snapshot ─────────────────────────────── */
    float    bpm;
    int      currentStep;      /* -1 if stopped */
    int      isPlaying;
    int      trackIndex;       /* Which track is being edited */

    /* ── Audio-Reactive Data (v4.1) ─────────────────────── */
    float    peak;             /* Current master peak 0-1            */
    float    peakL, peakR;     /* Per-channel peaks                  */
    const float* spectrumBins; /* 64-bin spectrum (NULL if unavail)  */
    int      spectrumLen;      /* Number of bins                     */
    const float* scopeBufL;    /* 128-sample track scope ring buffer */
    const float* scopeBufR;
    int      scopeLen;

    /* ── Waveform Display Metrics (v4.1) ────────────────── */
    float    pixelsPerSample;  /* Horizontal zoom level              */
    int      scrollOffset;     /* Waveform scroll position (samples) */
} PdEditorOverlayCtx;

/* ═══════════════════════════════════════════════════════════
 * HOST SYNTH PRIMITIVES — v3
 * ═══════════════════════════════════════════════════════════
 *
 * Received via pdsynth_set_host_v3() after instance creation.
 * These are the building blocks — mix and match to build anything.
 *
 * Example usage:
 *   float s = host->osc_sample(PD_OSC_SAW, v->phase);
 *   s = host->filter_sample(&v->flt, PD_FILTER_LOWPASS, 0.4f, 0.7f, s, 44100.f);
 *   float env = host->adsr_tick(&v->stage, &v->level, noteOn, 5.f, 200.f, 0.8f, 400.f, 44100.f);
 *   host->fx_process(PD_FX_REVERB, bufL, bufR, frames, 44100.f, params);
 */
typedef struct {
    /** Sample an oscillator at a given phase.
     *  type: PD_OSC_* | phase: 0.0-1.0 (advance by freq/sampleRate each sample)
     *  Returns: sample -1.0 to 1.0 */
    float (*osc_sample)(int type, float phase);

    /** Sample a morphing wavetable.
     *  idx: wavetable set (0=saw, 1=square, 2=digital)
     *  position: 0.0-1.0 morphs between frames in the set
     *  phase: 0.0-1.0 playback phase
     *  Returns: sample -1.0 to 1.0 */
    float (*wavetable_sample)(int idx, float position, float phase);

    /** Apply a state-variable filter to one sample.
     *  state: PdFilterState* (per-voice, per-channel — zero-init once per note)
     *  type: PD_FILTER_* | cutoff: 0.0-1.0 | resonance: 0.0-1.0
     *  input: sample -1.0 to 1.0 | sampleRate: e.g. 44100.0
     *  Returns: filtered sample */
    float (*filter_sample)(PdFilterState* state, int type, float cutoff,
                           float resonance, float input, float sampleRate);

    /** Tick an ADSR envelope by one sample.
     *  stage: int* — envelope stage (0=idle,1=attack,2=decay,3=sustain,4=release)
     *  level: float* — current level (modified in-place each call)
     *  noteOn: 1=held, 0=released
     *  attackMs, decayMs, sustain (0-1), releaseMs: envelope shape
     *  sampleRate: audio rate
     *  Returns: current envelope level 0-1 */
    float (*adsr_tick)(int* stage, float* level, int noteOn,
                       float attackMs, float decayMs, float sustain,
                       float releaseMs, float sampleRate);

    /** Apply an FX algorithm in-place to a stereo buffer.
     *  fxType: PD_FX_* | bufL, bufR: float[samples] (read+write)
     *  params: 4 normalized values 0-1 (see PD_FX_* constant comments)
     *
     *  Param layout per type:
     *    REVERB:     [mix, size, damp, width]
     *    DELAY:      [time, feedback, mix, _unused]
     *    CHORUS:     [rate, depth, mix, _unused]
     *    DISTORTION: [drive, tone, mix, _unused]
     *    BITCRUSH:   [bits, rate, mix, _unused]  */
    void (*fx_process)(int fxType, float* bufL, float* bufR, int samples,
                       float sampleRate, const float* params);
} PdSynthHostV3;

/* ═══════════════════════════════════════════════════════════
 * SAMPLE LOADING — v2 Host Callbacks (synth plugins)
 * ═══════════════════════════════════════════════════════════ */

/** Audio sample returned by the host (host owns the memory) */
typedef struct {
    float*      dataL;      /* Left channel float PCM (−1.0 to 1.0) */
    float*      dataR;      /* Right channel (same as dataL for mono) */
    int         frames;     /* Total sample frames */
    int         sampleRate; /* Original sample rate */
    int         channels;   /* 1=mono, 2=stereo */
    const char* name;       /* Filename without path */
} PdSynthSample;

/** v2 host callbacks — provided via pdsynth_set_host() */
typedef struct {
    void* hostData;

    /** Load a sample file. Path relative to plugin directory or absolute.
     *  Convention: "samples/kick.wav" — relative to plugin folder.
     *  Returns PdSynthSample*, or NULL on failure.
     *  Memory is valid until pdsynth_destroy(). */
    const PdSynthSample* (*load_sample)(void* hostData, const char* path);

    /** Load from the global DAW sample library by name.
     *  Example: "reetro/kick_01.wav" → samples/reetro/kick_01.wav */
    const PdSynthSample* (*load_library_sample)(void* hostData, const char* name);

    /** Number of loaded samples */
    int (*sample_count)(void* hostData);

    /** Get loaded sample by index (0-based) */
    const PdSynthSample* (*get_sample)(void* hostData, int index);

    /** Unload a sample slot */
    void (*unload_sample)(void* hostData, int index);

    /** Write a message to the DAW log */
    void (*log)(void* hostData, const char* message);

    /** Request the host to open a file browser for sample loading.
     *  slotIndex: slot to load into (-1 = next available).
     *  Asynchronous — check sample_count() to detect completion.
     *  Returns 1 if browser opened, 0 on failure. */
    int (*request_sample_browser)(void* hostData, int slotIndex);

    /** Get file path of a loaded sample (for display). NULL if unavailable. */
    const char* (*get_sample_path)(void* hostData, int index);
} PdSynthHostCallbacks;

/* ═══════════════════════════════════════════════════════════
 * SYNTH PLUGIN EXPORTS
 * ═══════════════════════════════════════════════════════════
 *
 * REQUIRED — implement all of these:
 *   pdsynth_api_version()
 *   pdsynth_name()
 *   pdsynth_param_count()
 *   pdsynth_create()
 *   pdsynth_destroy()
 *   pdsynth_process()
 *   pdsynth_note()
 *   pdsynth_get_param()
 *   pdsynth_set_param()
 *
 * OPTIONAL — implement any you need:
 *   pdsynth_param_name()        — named params in the UI
 *   pdsynth_reset()             — called on transport stop
 *   pdsynth_pitch_bend()        — MIDI pitch bend
 *   pdsynth_mod_wheel()         — MIDI mod wheel
 *   pdsynth_preset_count()      — number of built-in presets
 *   pdsynth_preset_name()       — preset names
 *   pdsynth_load_preset()       — load a preset
 *   pdsynth_get_preset()        — current preset index
 *   pdsynth_set_host()          — receive sample loading callbacks (v2)
 *   pdsynth_set_plugin_dir()    — receive plugin directory path (v2)
 *   pdsynth_get_waveform()      — supply waveform for host visualizer
 *   pdsynth_set_host_v3()       — receive host synth primitives (v3)
 *   pdsynth_draw()              — custom graphics on the editor page (v3)
 *   pdsynth_sample_loaded()     — notified when a sample finishes loading
 */

/* ── Synth Audio Buffer ───────────────────────────────── */
typedef struct {
    float   sampleRate;
    int     bufferSize;     /* Frames to generate */
    float*  outputL;        /* Write your audio here */
    float*  outputR;
} PdSynthAudio;

/* ── Note Event ─────────────────────────────────────────── */
typedef struct {
    uint8_t note;           /* MIDI note 0-127 */
    uint8_t velocity;       /* 0-127 (0 = note off) */
    uint8_t channel;        /* Reserved */
    uint8_t type;           /* 0=noteOff, 1=noteOn */
} PdSynthNote;

/* ── Required Synth Exports (declare in your .c file) ─── */
int              pdsynth_api_version(void);
const char*      pdsynth_name(void);
int              pdsynth_param_count(void);
PdSynthInstance  pdsynth_create(float sampleRate);
void             pdsynth_destroy(PdSynthInstance inst);
void             pdsynth_process(PdSynthInstance inst, PdSynthAudio* audio);
void             pdsynth_note(PdSynthInstance inst, PdSynthNote* event);
float            pdsynth_get_param(PdSynthInstance inst, int index);
void             pdsynth_set_param(PdSynthInstance inst, int index, float value);

/* ── Optional Synth Exports ─────────────────────────────── */
const char*      pdsynth_param_name(int index);
void             pdsynth_reset(PdSynthInstance inst);
void             pdsynth_pitch_bend(PdSynthInstance inst, float bend);
void             pdsynth_mod_wheel(PdSynthInstance inst, float value);
int              pdsynth_preset_count(void);
const char*      pdsynth_preset_name(int index);
void             pdsynth_load_preset(PdSynthInstance inst, int index);
int              pdsynth_get_preset(PdSynthInstance inst);
void             pdsynth_set_host(PdSynthInstance inst, const PdSynthHostCallbacks* host);
void             pdsynth_set_plugin_dir(PdSynthInstance inst, const char* dir);
int              pdsynth_get_waveform(PdSynthInstance inst, float* buf, int maxSamples);
void             pdsynth_set_host_v3(PdSynthInstance inst, const PdSynthHostV3* host);
int              pdsynth_draw(PdSynthInstance inst, const PdDrawContext* ctx);
void             pdsynth_sample_loaded(PdSynthInstance inst, int slot,
                                       const float* data, int length);

/* ── Optional Synth Exports (v4.0) ─────────────────────── */
void             pdsynth_set_host_v4(PdSynthInstance inst, const PdHostV4* host);
void             pdsynth_midi_cc(PdSynthInstance inst, int cc, float value);
void             pdsynth_transport_changed(PdSynthInstance inst, const PdHostTransport* transport);
int              pdsynth_draw_overlay(PdSynthInstance inst, const PdEditorOverlayCtx* ctx);
void             pdsynth_get_viz_data(PdSynthInstance inst, float* outBars, int maxBars);

/* ── Optional Synth Exports (v4.5) ─────────────────────── */
/** Called by host when a global sample slot is loaded, replaced, or cleared.
 *  Plugins that bind to host sample slots should invalidate cached data. */
void             pdsynth_host_sample_changed(PdSynthInstance inst, int slot);

/* ═══════════════════════════════════════════════════════════
 * FX PLUGIN EXPORTS
 * ═══════════════════════════════════════════════════════════
 *
 * REQUIRED:
 *   pdfx_api_version()
 *   pdfx_name()
 *   pdfx_param_count()
 *   pdfx_create()
 *   pdfx_destroy()
 *   pdfx_process()
 *   pdfx_get_param()
 *   pdfx_set_param()
 *
 * OPTIONAL:
 *   pdfx_param_name()           — named params in editor
 *   pdfx_reset()                — clear delay lines etc.
 *   pdfx_needs_sidechain()      — return 1 to request sidechain input (v2)
 *   pdfx_set_sidechain()        — receive sidechain audio each callback (v2)
 *   pdfx_set_host()             — receive host FX callbacks (v2)
 *   pdfx_preset_count()
 *   pdfx_preset_name()
 *   pdfx_load_preset()
 *   pdfx_get_preset()
 *   pdfx_get_gain_reduction()   — GR meter in full-screen editor (v2.1)
 *   pdfx_get_accent_color()     — dynamic UI accent color (v2.1)
 *   pdfx_format_param()         — formatted param display strings (v2.1)
 *   pdfx_param_group()          — param section grouping (v2.1)
 */

/* ── FX Audio Buffer ─────────────────────────────────────── */
typedef struct {
    float   sampleRate;
    int     bufferSize;
    float*  inputL;         /* Read input  (pre-FX) */
    float*  inputR;
    float*  outputL;        /* Write output (post-FX, may alias input) */
    float*  outputR;
} PdFxAudio;
/* SDK v2.3 — Waveform / oscilloscope pattern:
 * Plugins that want to display a live audio waveform should maintain their
 * own ring buffers inside the plugin state struct and write to them during
 * pdfx_process() before returning. Read inputL/inputR for pre-FX audio and
 * outputL/outputR for post-FX audio. Display the ring buffers during
 * pdfx_draw(). Use a volatile int write-head for thread safety (audio
 * thread writes, UI thread reads — visual-only data, occasional glitch ok).
 *
 *   Example (mono mix, 512-sample ring buffer):
 *     int wpos = state->wavePos;
 *     for (int i = 0; i < a->bufferSize; i++) {
 *         state->wavePre[wpos]  = (a->inputL[i]  + a->inputR[i])  * 0.5f;
 *         state->wavePost[wpos] = (a->outputL[i] + a->outputR[i]) * 0.5f;
 *         wpos = (wpos + 1) % WAVE_LEN;
 *     }
 *     state->wavePos = wpos;
 */

/* ── Sidechain Host Callbacks ───────────────────────────── */
typedef struct {
    void* hostData;
    int          (*get_track_count)(void* hostData);
    const char*  (*get_track_name)(void* hostData, int trackIndex);
    void         (*request_track_picker)(void* hostData);
    int          (*get_sidechain_source)(void* hostData);
} PdFxHostCallbacks;

/* ── Required FX Exports ────────────────────────────────── */
int          pdfx_api_version(void);
const char*  pdfx_name(void);
int          pdfx_param_count(void);
PdFxInstance pdfx_create(float sampleRate);
void         pdfx_destroy(PdFxInstance inst);
void         pdfx_process(PdFxInstance inst, PdFxAudio* audio);
float        pdfx_get_param(PdFxInstance inst, int index);
void         pdfx_set_param(PdFxInstance inst, int index, float value);

/* ── Optional FX Exports ────────────────────────────────── */
const char*  pdfx_param_name(int index);
void         pdfx_reset(PdFxInstance inst);
int          pdfx_needs_sidechain(void);
void         pdfx_set_sidechain(PdFxInstance inst, const float* scL,
                                const float* scR, int frames);
void         pdfx_set_host(PdFxInstance inst, PdFxHostCallbacks* host);
int          pdfx_preset_count(void);
const char*  pdfx_preset_name(int index);
void         pdfx_load_preset(PdFxInstance inst, int index);
int          pdfx_get_preset(PdFxInstance inst);
float        pdfx_get_gain_reduction(PdFxInstance inst);
void         pdfx_get_accent_color(PdFxInstance inst,
                                   uint8_t* r, uint8_t* g, uint8_t* b);
const char*  pdfx_format_param(PdFxInstance inst, int index);
const char*  pdfx_param_group(int index);
/* v2.2 — custom draw (receives SDL_Renderer* separately from PdDrawContext) */
int          pdfx_draw(PdFxInstance inst, void* renderer, PdDrawContext* ctx);

/* ── Optional FX Exports (v4.5 — Plugin Capabilities) ──── */

/** Plugin capability flags — returned by pdfx_capabilities().
 *  Tells the host what this plugin does so the host can provide
 *  appropriate lifecycle handling without hardcoding plugin names. */
#define PDFX_CAP_RECORDS_AUDIO    0x01  /* Plugin records audio to sample slots */
#define PDFX_CAP_FINALIZE_ON_EXIT 0x02  /* Host should call pdfx_finalize on editor close */

/** Return capability flags. 0 = basic effect (no special behavior). */
int          pdfx_capabilities(void);

/** Finalize result — tells the host what the plugin did on exit. */
typedef struct {
    int   targetSlot;      /* Sample slot written to (-1 = none)              */
    int   clearSteps;      /* 1 = host should clear track steps after record  */
    int   expandPattern;   /* 1 = host already expanded pattern (informational) */
} PdFxFinalizeResult;

/** Called by host when FX editor closes (if PDFX_CAP_FINALIZE_ON_EXIT set).
 *  Plugin should finalize any active recording and return what it did. */
PdFxFinalizeResult pdfx_finalize(PdFxInstance inst);

/* ── Optional FX Exports (v4.0) ────────────────────────── */
void         pdfx_set_host_v2(PdFxInstance inst, const PdFxHostV2* host);
void         pdfx_process_midi(PdFxInstance inst,
                               const PdMidiEvent* in, int inCount,
                               PdMidiEvent* out, int* outCount, int maxOut);
void         pdfx_transport_changed(PdFxInstance inst, const PdHostTransport* transport);
int          pdfx_draw_overlay(PdFxInstance inst, const PdEditorOverlayCtx* ctx);
void         pdfx_get_viz_data(PdFxInstance inst, float* outBars, int maxBars);

/* ═══════════════════════════════════════════════════════════
 * MANIFEST.JSON — Plugin configuration file
 * ═══════════════════════════════════════════════════════════
 *
 * Place alongside your .so file. Same format for synths and FX.
 *
 * SYNTH MANIFEST (plugins/synths/my-synth/manifest.json):
 * ─────────────────────────────────────────────────────────
 * {
 *   "name":        "My Synth",
 *   "author":      "Your Name",
 *   "version":     "1.0",
 *   "description": "One-line description.",
 *   "library":     "my-synth.so",
 *   "params": [
 *     { "name": "Cutoff",   "default": 0.8 },
 *     { "name": "Resonance","default": 0.0 },
 *     { "name": "Attack",   "default": 0.01 }
 *   ],
 *   "presets": ["Init", "Fat Bass", "Pad"],
 *   "samples": [
 *     { "path": "samples/kick.wav",  "note": 36 },
 *     { "path": "samples/snare.wav", "note": 38 }
 *   ],
 *   "ui": {
 *     "accent":     [255, 80, 30],
 *     "skin":       "skin.bmp",          // 320×240 BMP background
 *     "mascot":     "mascot.bmp",        // Animated spritesheet BMP
 *     "waveframe":  "waveframe.bmp",     // Waveform frame overlay BMP
 *     "knobStrip":  "knobs.bmp",         // Vertical knob filmstrip
 *     "knobFrames": 64,                  // Frames in filmstrip
 *     "knobSize":   48,                  // Frame pixel size
 *     "pages":      true,                // Enable graphics page + params page
 *     "viz": {
 *       "type":     "waveform",          // "waveform" | "spectrum" | "custom"
 *       "x":  6,  "y": 14,
 *       "w": 300, "h": 32,
 *       "color":    [255, 80, 30],
 *       "reactive": true,                // React to THIS track's audio
 *       "bars":     16                   // Spectrum bars (8-32)
 *     }
 *   },
 *   "tags": ["subtractive", "polyphonic"]
 * }
 *
 * FX MANIFEST (plugins/fx/my-effect/manifest.json):
 * ─────────────────────────────────────────────────
 * {
 *   "name":      "My Effect",
 *   "author":    "Your Name",
 *   "version":   "1.0",
 *   "type":      "effect",
 *   "library":   "my-effect.so",
 *   "sidechain": false,
 *   "params": [
 *     { "name": "Mix",      "default": 0.5 },
 *     { "name": "Feedback", "default": 0.3 }
 *   ],
 *   "ui": {
 *     "accent": [60, 180, 255],
 *     "editor": {
 *       "scope":     true,   // Waveform oscilloscope in editor
 *       "meters":    true,   // L/R level meters
 *       "grMeter":   false,  // Gain reduction meter (for compressors)
 *       "particles": true    // Audio-reactive floating particles
 *     }
 *   }
 * }
 *
 * SPRITE ASSETS (place alongside .so):
 * ─────────────────────────────────────
 *   skin.bmp      — 320×240 background (replaces the default dark bg)
 *   knobs.bmp     — Vertical filmstrip BMP, frames stacked top-to-bottom
 *                   Each frame is knobSize×knobSize pixels
 *                   Frame 0=min value, last frame=max value
 *   mascot.bmp    — Animated character, frames left-to-right
 *                   Rendered bottom-right corner; 4 frames intro/idle
 *   waveframe.bmp — Waveform display overlay frame (decorative border)
 *   samples/      — WAV/OGG/FLAC/MP3 files for sampler plugins
 *
 * NOTE: All BMP files are in standard 24-bit BMP format.
 *       Use convert, GIMP, or any bitmap editor.
 *       Transparent color: magenta (255, 0, 255).
 */

/* ═══════════════════════════════════════════════════════════
 * QUICK RECIPES
 * ═══════════════════════════════════════════════════════════
 *
 * ── SIMPLE OSCILLATOR SYNTH ─────────────────────────────
 *
 *   #include "pocketdaw.h"
 *   typedef struct { float phase; float sr; } MySynth;
 *
 *   int pdsynth_api_version(void) { return PD_SDK_VERSION; }
 *   const char* pdsynth_name(void) { return "My Synth"; }
 *   int pdsynth_param_count(void) { return 0; }
 *
 *   PdSynthInstance pdsynth_create(float sr) {
 *       MySynth* s = calloc(1, sizeof(*s)); s->sr = sr; return s; }
 *   void pdsynth_destroy(PdSynthInstance i) { free(i); }
 *
 *   void pdsynth_note(PdSynthInstance i, PdSynthNote* n) {}  // stateless example
 *
 *   void pdsynth_process(PdSynthInstance i, PdSynthAudio* a) {
 *       MySynth* s = i;
 *       for (int n = 0; n < a->bufferSize; n++) {
 *           // Use host primitives via pdsynth_set_host_v3() ...
 *           float v = sinf(s->phase * 6.2831f); // or just sin
 *           a->outputL[n] += v * 0.4f;
 *           a->outputR[n] += v * 0.4f;
 *           s->phase += 440.0f / s->sr;
 *           if (s->phase >= 1.0f) s->phase -= 1.0f;
 *       }
 *   }
 *   float pdsynth_get_param(PdSynthInstance i, int idx) { return 0; }
 *   void  pdsynth_set_param(PdSynthInstance i, int idx, float v) {}
 *
 * ── AUDIO-REACTIVE OSCILLOSCOPE VISUAL ──────────────────
 *
 *   // Return 1 to own the page, 0 to draw behind knobs
 *   int pdsynth_draw(PdSynthInstance inst, const PdDrawContext* ctx) {
 *       SDL_Renderer* r = ctx->renderer;
 *       // Draw background
 *       SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
 *       SDL_Rect bg = { ctx->x, ctx->y, ctx->w, ctx->h };
 *       SDL_RenderFillRect(r, &bg);
 *       // Draw oscilloscope from scope buffer
 *       SDL_SetRenderDrawColor(r, 0, 200, 100, 255);
 *       int midY = ctx->y + ctx->h / 2;
 *       for (int i = 1; i < ctx->scopeLen; i++) {
 *           int x1 = ctx->x + (i-1) * ctx->w / ctx->scopeLen;
 *           int x2 = ctx->x + i     * ctx->w / ctx->scopeLen;
 *           int y1 = midY - (int)(ctx->scopeBufL[i-1] * ctx->h * 0.4f);
 *           int y2 = midY - (int)(ctx->scopeBufL[i]   * ctx->h * 0.4f);
 *           SDL_RenderDrawLine(r, x1, y1, x2, y2);
 *       }
 *       return 0; // 0 = draw knobs on top; 1 = full custom, no knobs
 *   }
 *
 * ── USE HOST FX BUS ──────────────────────────────────────
 *
 *   void pdsynth_set_host_v3(PdSynthInstance inst, const PdSynthHostV3* h) {
 *       MySynth* s = inst; s->hostV3 = h;
 *   }
 *   // Then in pdsynth_process():
 *   float p[4] = { 0.6f, 0.5f, 0.4f, 0.8f }; // [mix, size, damp, width]
 *   s->hostV3->fx_process(PD_FX_REVERB, bufL, bufR, frames, 44100.f, p);
 *
 * BUILD COMMAND
 * ─────────────
 * aarch64-none-linux-gnu-gcc -shared -fPIC -O2 -march=armv8-a \
 *   -I/path/to/sdk/ my-plugin.c -lm -o my-plugin.so
 */

#ifdef __cplusplus
}
#endif

#endif /* POCKETDAW_H */
