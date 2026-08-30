#pragma once

#include <Arduino.h>
#include "midi_parser.h"

enum SequencerState {
    SEQ_STOPPED,
    SEQ_PLAYING,
    SEQ_PAUSED
};

class MIDISequencer {
public:
    static bool begin();
    static bool loadFile(const char* path);
    static bool loadMemory(const uint8_t* data, size_t size, const char* name = "Memory");
    static void setLoop(bool loop);
    static bool isLoopEnabled();
    static void play();
    static void pause();
    static void stop();
    static void update(); // 시퀀서 타이머 틱 처리

    static SequencerState getState();
    static const char* getCurrentSongName();
    static uint32_t readVarLen(const uint8_t** ptr, const uint8_t* end);

private:
    static SequencerState state;
    static SynthMode songSynthMode;
    static char songName[64];
    static uint8_t* midiData;
    static size_t midiDataSize;
    static bool isMemorySource;
    static bool loopEnabled;
    static uint16_t timeDivision;
    static uint32_t tempoUsPerQuarter;
    static unsigned long lastTickUs;

    struct TrackPointer {
        const uint8_t* start;
        const uint8_t* current;
        const uint8_t* end;
        uint32_t nextEventTick;
        uint8_t runningStatus;
        bool isDone;
    };

    static TrackPointer tracks[64];
    static uint16_t numTracks;
    static uint32_t currentTick;
    static uint32_t tickIntervalUs;

    static uint16_t channelRPN[16];
    static uint16_t channelNRPN[16];

    static bool parseTracks(const uint8_t* data, size_t size);
    static void processNextEvents();
    static void stopInternal();
};
