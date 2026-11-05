       #include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include "SDL/include/SDL.h"
#undef main
#include "SDL/SDL_mixer.h"
#include "SDL/SDL_image.h"
#endif
#ifndef WIN32
#include "SDL.h"
#include "SDL_mixer.h"
#include "SDL_image.h"
#else
#include "Windows.h"
#endif

#define true 1
#define false 0

static int  lid = 0;  // Teletype lid is up(1) or down(0), down is a lot more quiet
#define DELTA_UP 20 // Sound with lid up are 20 higher up in the list
#define KEY_BASE 5  // there are 7 samples of a keypress, the first is sample #5 (+20 for closed lid)
#define CR_BASE 1 // and 3 CR samples, starting from #1
#define CHAR_BASE 16 // and 2 print character samples,
#define SPACE_BASE 18 // and 2 print space samples,

// === STY ====
#define TTYMUTE 0
#define TTYSOUNDCLOSED 1
#define TTYSOUNDOPEN 2
extern int tty_soundmode; // 0=mute, 1=lid_closed, 2=lid_open
// ============


#define NUM_WAVEFORMS 40

/*
 * Where the .wav samples live.
 *
 * Upstream hardcoded "..." into the table below, so the
 * Teletype was silent unless the project happened to be installed at that one
 * path - and there was no way to tell it otherwise at build time or run time.
 * The table now holds bare file names; the directory comes from $PIDP10_SOUNDS,
 * falling back to the directory the package was built with.
 */
#ifndef PIDP10_SOUNDS_DIR
#define PIDP10_SOUNDS_DIR "/usr/share/pidp10/sounds"
#endif

static const char *
pidp10_sounds_dir(void)
{
    const char *d = getenv("PIDP10_SOUNDS");

    return (d && *d) ? d : PIDP10_SOUNDS_DIR;
}
const char* _waveFileNames[] =
{
  "down-bell.wav",   // 0
  "down-cr-01.wav",  // 1
  "down-cr-02.wav",  // 2
  "down-cr-03.wav",  // 3
  "down-hum.wav",    // 4
  "down-key-01.wav", // 5
  "down-key-02.wav", // 6
  "down-key-03.wav", // 7
  "down-key-04.wav", // 8
  "down-key-05.wav", // 9
  "down-key-06.wav", // 10
  "down-key-07.wav", // 11
  "down-lid.wav",    // 12
  "down-motor-off.wav",  // 13
  "down-motor-on.wav",   // 14
  "down-platen.wav",     // 15
  "down-print-chars-01.wav",     // 16
  "down-print-chars-02.wav",     // 17
  "down-print-spaces-01.wav",    // 18
  "down-print-spaces-02.wav",    // 19
  "up-bell.wav",     // 20
  "up-cr-01.wav",    // 21
  "up-cr-02.wav",    // 22
  "up-cr-03.wav",    // 23
  "up-hum.wav",      // 24
  "up-key-01.wav",   // 25
  "up-key-02.wav",   // 26
  "up-key-03.wav",   // 27
  "up-key-04.wav",   // 28
  "up-key-05.wav",   // 29
  "up-key-06.wav",   // 30
  "up-key-07.wav",   // 31
  "up-lid.wav",      // 32
  "up-motor-off.wav",    // 33
  "up-motor-on.wav",     // 34
  "up-platen.wav",       // 35
  "up-print-chars-01.wav",   // 36
  "up-print-chars-02.wav",   // 37
  "up-print-spaces-01.wav",  // 38
  "up-print-spaces-02.wav"   // 39
};

Mix_Chunk* _sample[NUM_WAVEFORMS]; // was 2

// Initializes the application data
int sound_init(void) 
{
    // Initialize the SDL library with the Video subsystem
    SDL_Init(SDL_INIT_AUDIO );
    atexit(SDL_Quit);

    memset(_sample, 0, sizeof(Mix_Chunk*) * 2);

    // Set up the audio stream
    //int result = Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 512);
    int result = Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 512);
    if( result < 0 )
    {
        fprintf(stderr, "Unable to open audio: %s\n", SDL_GetError());
        exit(-1);
    }

    result = Mix_AllocateChannels(4);
    if( result < 0 )
    {
        fprintf(stderr, "Unable to allocate mixing channels: %s\n", SDL_GetError());
        exit(-1);
    }

    // Load waveforms
    {
        const char *dir = pidp10_sounds_dir();
        int loaded = 0;

        for( int i = 0; i < NUM_WAVEFORMS; i++ )
        {
            char path[4096];

            snprintf(path, sizeof path, "%s/%s", dir, _waveFileNames[i]);
            _sample[i] = Mix_LoadWAV(path);
            if( _sample[i] == NULL )
            {
                if (loaded == 0)
                    fprintf(stderr, "sty: no sound samples in %s "
                            "(set PIDP10_SOUNDS, or install the pidp10 data "
                            "package)\n", dir);
            }
            else
                loaded++;
        }
        if (loaded == 0)
            fprintf(stderr, "sty: continuing muted\n");
    }
//fprintf(stderr, "sound_init done\r");

    Mix_PlayChannel(1, _sample[14 + lid*DELTA_UP ], 0 );
    while (Mix_Playing(1) != 0) {
    SDL_Delay(100); // wait 200 milliseconds
    }
    Mix_PlayChannel(1, _sample[4 + lid*DELTA_UP ], -1); // only runs the sample 65k, so not good but ok
    //sleep(10);
    return true;
}

int sound(char c)
{

            //Mix_PlayChannel(-1, _sample[5], 0);
            //return 1;
        if (c<32)
            return 0;

        lid = (tty_soundmode==2?1:0);       // sounds are different with the Teletype case opened.

        if (c==32)
        {
            Mix_PlayChannelTimed(-1, _sample[SPACE_BASE + lid*DELTA_UP + rand() % 2], 0, 100);
            //Mix_PlayChannel(-1, _sample[SPACE_BASE + lid*DELTA_UP + rand() % 2], 0);
            //printf("__ %c %d - %d\r\n", c , c, SPACE_BASE + lid*DELTA_UP + rand() % 2);
        }

        if ((c>=33) && (c<=95))
        {
            if (c==13)
            {
                Mix_PlayChannelTimed(-1, _sample[CR_BASE + lid*DELTA_UP + rand() % 2], 0,100);
                //Mix_PlayChannel(-1, _sample[CR_BASE + lid*DELTA_UP + rand() % 2], 0);
                //printf("CR %c %d - %d\r\n", c , c, CR_BASE + lid*DELTA_UP + rand() % 2);
                //usleep( rand() % 200);
            }
            else
            {
                Mix_PlayChannelTimed(-1, _sample[CHAR_BASE + lid*DELTA_UP + rand() % 2], 0,100);
                //Mix_PlayChannel(-1, _sample[CHAR_BASE + lid*DELTA_UP + rand() % 2], 0);
                //printf("CH %c %d - %d\r\n", c , c, CHAR_BASE + lid*DELTA_UP + rand() % 2);
            }
        }
        
        if ((c>=96) && (c<=127))    // surely, lowercase we can add as a Teletype option.
        {
            Mix_PlayChannelTimed(-1, _sample[CHAR_BASE + lid*DELTA_UP + rand() % 2], 0,100);
            //Mix_PlayChannel(-1, _sample[CHAR_BASE + lid + rand()*DELTA_UP % 2], 0);
            //printf("CH %c %d - %d\r\n", c , c, CHAR_BASE + lid*DELTA_UP + rand() % 2);
        }
        
        return 1;
}

int sound_close(void)
{

    Mix_PlayChannel(1, _sample[13 + lid*DELTA_UP ], 0); 

    while (Mix_Playing(1) != 0) {
    SDL_Delay(100); // wait 200 milliseconds
    }

    for( int i = 0; i < NUM_WAVEFORMS; i++ )
    {
        Mix_FreeChunk(_sample[i]);
    }

    Mix_CloseAudio();
    SDL_Quit();
    return 0;
}

int tty_backgroundhum(int hum)
{
if (hum)
    Mix_PlayChannel(1, _sample[4 + lid*DELTA_UP ], -1); // only runs the sample 65k, so not good but ok
else
    Mix_PlayChannel(1, _sample[4 + lid*DELTA_UP ], 0); // only runs the sample one more time
}
