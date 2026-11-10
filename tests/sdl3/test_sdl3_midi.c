/* WinMM MIDI stream emulation: event timing, tempo, callbacks and control. */
#include "check.h"
#include "sdl3_midi.h"
#include <string.h>

typedef struct Buffer {
    DxBallMidiHeader header;
    DxBallUInt words[32];
} Buffer;

static long long clock_now;
static long long times[32];
static DxBallUInt messages[32];
static int message_count, silenced, done_count;
static DxBallMidiHeader *last_done;
static int resubmit;

static void sink_message(DxBallUInt value)
{
    if (message_count < 32) { times[message_count] = clock_now; messages[message_count++] = value; }
}
static void sink_render(float *stereo, int frames) { (void)stereo; clock_now += frames; }
static void sink_silence(void) { ++silenced; }

static void DXBALL_DDCALL on_done(DxBallMidiHandle stream, DxBallUInt message, size_t instance,
                                  DxBallMidiHeader *header, size_t unused)
{
    (void)unused;
    CHECK_EQ(message, 0x3c9);
    CHECK_EQ(instance, 0x1234);
    CHECK(header->flags & 1);
    ++done_count;
    last_done = header;
    if (resubmit) dxball_midi_stream_out(stream, header, 64); /* as dxball_midi_callback does */
}

static void run(long long frames)
{
    float chunk[2 * 512];
    while (frames > 0) {
        int part = frames > 512 ? 512 : (int)frames;
        dxball_sdl3_midi_advance(chunk, part);
        frames -= part;
    }
}

static void build(Buffer *buffer, const DxBallUInt *words, DxBallUInt count)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->header.data = (DxBallByte *)buffer->words;
    buffer->header.buffer_length = sizeof(buffer->words);
    buffer->header.bytes_recorded = count * 4;
    memcpy(buffer->words, words, count * 4);
}

static DxBallMidiHandle open_stream(void)
{
    DxBallMidiHandle stream = 0;
    DxBallUInt device = 0xffffffffUL;
    DxBallMidiProperty property = { 8, 96 };
    CHECK_EQ(dxball_midi_stream_open(&stream, &device, 1, on_done, 0x1234, 0x30000), 0);
    CHECK(stream != 0);
    CHECK_EQ(dxball_midi_stream_property(stream, &property, 0x80000001UL), 0);
    property.value = 0;
    CHECK_EQ(dxball_midi_stream_property(stream, &property, 0x40000001UL), 0);
    CHECK_EQ(property.value, 96);
    return stream;
}

static void reset_log(void) { clock_now = 0; message_count = 0; done_count = 0; silenced = 0; }

int main(void)
{
    static const DxBallUInt note[] = { 0, 0, 0x00403c90, 96, 0, 0x00003c80 };
    static const DxBallUInt tempo[] = {
        0, 0, 0x01000000 | 250000,         /* MEVT_TEMPO: twice as fast */
        96, 0, 0x00404090,
        0, 0, 0x80000005, 0x11111111, 0x22222222, /* MEVT_F_LONG payload skipped */
        0, 0, 0x00004080 };
    Buffer first, second;
    DxBallMidiHandle stream, other;
    DxBallUInt device = 0;

    dxball_bind_sdl3_midi();
    dxball_sdl3_midi_sink.message = sink_message;
    dxball_sdl3_midi_sink.render = sink_render;
    dxball_sdl3_midi_sink.silence = sink_silence;

    /* A new stream is paused; unprepared headers are refused. */
    reset_log();
    stream = open_stream();
    CHECK_EQ(dxball_midi_stream_open(&other, &device, 1, on_done, 0, 0x30000), 4);
    build(&first, note, sizeof(note) / sizeof(note[0]));
    CHECK_EQ(dxball_midi_stream_out(stream, &first.header, 64), 64);
    CHECK_EQ(dxball_midi_prepare_header(stream, &first.header, 64), 0);
    CHECK_EQ(dxball_midi_stream_out(stream, &first.header, 64), 0);
    CHECK(first.header.flags & 4);
    run(1000);
    CHECK_EQ(message_count, 0);

    /* Delta ticks become samples: 96 ticks at 500000 us/quarter = 22050. */
    reset_log();
    CHECK_EQ(dxball_midi_stream_restart(stream), 0);
    run(30000);
    CHECK_EQ(message_count, 2);
    CHECK_EQ(messages[0], 0x403c90); CHECK_EQ(times[0], 0);
    CHECK_EQ(messages[1], 0x3c80);   CHECK_EQ(times[1], 22050);
    CHECK_EQ(done_count, 1);
    CHECK(last_done == &first.header);
    CHECK_EQ(first.header.flags & 5, 1); /* done, no longer queued */

    /* Re-submission from the callback loops the music seamlessly. */
    reset_log();
    resubmit = 1;
    CHECK_EQ(dxball_midi_stream_out(stream, &first.header, 64), 0);
    run(50000);
    CHECK_EQ(message_count, 5);
    CHECK_EQ(times[1], 22050);
    CHECK_EQ(messages[2], 0x403c90); CHECK_EQ(times[2], 22050);
    CHECK_EQ(times[3], 44100);
    CHECK_EQ(done_count, 2);

    /* Pause holds the position and silences; restart continues. */
    reset_log();
    CHECK_EQ(dxball_midi_stream_pause(stream), 0);
    CHECK_EQ(silenced, 1);
    run(100000);
    CHECK_EQ(message_count, 0);
    CHECK_EQ(dxball_midi_stream_restart(stream), 0);
    run(22050);
    CHECK_EQ(message_count, 2);      /* note off, then the re-queued note on */
    CHECK_EQ(messages[0], 0x3c80);
    CHECK_EQ(times[0], 100000 + 66150 - 50000); /* paused frames add no musical time */

    /* Reset returns the queued header through the callback; then close. */
    resubmit = 0;
    reset_log();
    CHECK_EQ(dxball_midi_stream_close(stream), 65);
    CHECK_EQ(dxball_midi_out_reset(stream), 0);
    CHECK_EQ(done_count, 1);
    CHECK(silenced >= 1);
    CHECK_EQ(dxball_midi_unprepare_header(stream, &first.header, 64), 0);
    CHECK_EQ(dxball_midi_stream_close(stream), 0);
    CHECK_EQ(dxball_midi_stream_restart(stream), 5);

    /* Tempo events change later deltas; long events are skipped. */
    reset_log();
    stream = open_stream();
    build(&second, tempo, sizeof(tempo) / sizeof(tempo[0]));
    dxball_midi_prepare_header(stream, &second.header, 64);
    dxball_midi_stream_out(stream, &second.header, 64);
    dxball_midi_stream_restart(stream);
    run(20000);
    CHECK_EQ(message_count, 2);
    CHECK_EQ(messages[0], 0x404090); CHECK_EQ(times[0], 11025);
    CHECK_EQ(messages[1], 0x4080);   CHECK_EQ(times[1], 11025);
    CHECK_EQ(done_count, 1);
    CHECK_EQ(dxball_midi_stream_close(stream), 0);
    return CHECK_DONE();
}
