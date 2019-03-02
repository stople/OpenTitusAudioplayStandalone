/*   
 * Copyright (C) 2010 Eirik Stople
 *
 * OpenTitus is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 3  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <SDL/SDL.h>
#include "fmopl.h"
//#include "ymf262.h"
#include "sqz.h"


#define ADLIB_DATA_COUNT 10
#define ADLIB_INSTRUMENT_COUNT 19

#define FREQ_RATE 44100
#define BUF_SIZE 2048

#define MUS_OFFSET 28677
#define INS_OFFSET 29029

char playing;
int rate;

unsigned char opera[] = {0,0,1,2,3,4,5,8,9,0xA,0xB,0xC,0xD,0x10,0x11,0x12,0x13,0x14,0x15};
unsigned char voxp[] = {1,2,3,7,8,9,13,17,15,18,14};
unsigned int gamme[] = {343,363,385,408,432,458,485,514,544,577,611,647,0};

FM_OPL *opl;

FILE* gifp;

typedef enum _OUTPUT_FORMAT OUTPUT_FORMAT;
enum _OUTPUT_FORMAT{DIRECT, DRO};

int song;
OUTPUT_FORMAT output_format;

int debug_test[20];

int debug_counter;

unsigned int lastnote, tick; //debug

int segment = 0x6AF0;

typedef struct {
    unsigned char op[2][5]; //Two operators and five data settings 
    unsigned char fb_alg;
    unsigned char vox; //(only for perc instruments, 0xFE if this is melodic instrument, 0xFF if this instrument is disabled)
} ADLIB_INSTR;

typedef struct {
    unsigned char duration[ADLIB_DATA_COUNT];
    unsigned char volume[ADLIB_DATA_COUNT];
    unsigned char tempo[ADLIB_DATA_COUNT];
    unsigned char triple_duration[ADLIB_DATA_COUNT];
    unsigned char lie[ADLIB_DATA_COUNT];
    unsigned char vox[ADLIB_DATA_COUNT]; //(range: 0-10)
    ADLIB_INSTR *instrument[ADLIB_DATA_COUNT];

    unsigned char delay_counter[ADLIB_DATA_COUNT];
    unsigned char freq[ADLIB_DATA_COUNT];
    unsigned char octave[ADLIB_DATA_COUNT];
    unsigned char *return_point[ADLIB_DATA_COUNT];
    unsigned char loop_counter[ADLIB_DATA_COUNT];
    unsigned char *pointer[ADLIB_DATA_COUNT];
    unsigned char lie_late[ADLIB_DATA_COUNT];

    unsigned char perc_stat;

    unsigned char skip_delay;
    unsigned char skip_delay_counter;

    signed int cutsong; //Contains the number of active music channels

    unsigned char *data;
    int data_size;

    ADLIB_INSTR instrument_data[ADLIB_INSTRUMENT_COUNT];

    SDL_AudioSpec spec;
} ADLIB_DATA;

typedef struct {
    unsigned char sampsize;
    int playing;
    SDL_AudioSpec spec;
    ADLIB_DATA aad;
} SDL_PLAYER;


SDL_PLAYER sdl_player_data;

int play();
int init();
int clean();
void updatechip(int reg, int val);
int fillchip(ADLIB_DATA *aad);
void insmaker(unsigned char *insdata, int channel);
int load_file(char *filename, unsigned char **raw_data);
int load_data(ADLIB_DATA *aad, unsigned char *raw_data, int len, int mus_offset, int ins_offset, int song_number);
void all_vox_zero();
void callback(void *userdata, Uint8 *audiobuf, int len);
//int main(int argc, char *argv[]);



int play(){
    int i;
    printf("Playing... (terminate with Ctrl-C)\n");
    for (i = 0; i < 1; i++){
        SDL_PauseAudio(0);
        SDL_Delay(1000 * sdl_player_data.spec.freq / (sdl_player_data.spec.size / sdl_player_data.sampsize));
    }
}

int init(){
   memset(&(sdl_player_data.spec), 0x00, sizeof(SDL_AudioSpec));
    if(SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "unable to initialize SDL -- %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    sdl_player_data.spec.freq = FREQ_RATE;
    sdl_player_data.spec.format = AUDIO_S16SYS;
    sdl_player_data.spec.channels = 1; //Mono
    sdl_player_data.spec.samples = BUF_SIZE;
    sdl_player_data.spec.callback = callback;
    sdl_player_data.spec.userdata = &sdl_player_data;

    if(SDL_OpenAudio(&(sdl_player_data.spec), NULL) < 0) {
        fprintf(stderr, "unable to open audio -- %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    opl = OPLCreate(OPL_TYPE_YM3812, 3579545, FREQ_RATE);
    //YMF262Init(1, 14400000, FREQ_RATE);

    OPLResetChip(opl);
    //YMF262ResetChip(0);

    sdl_player_data.sampsize = sdl_player_data.spec.channels * (sdl_player_data.spec.format == AUDIO_U8 ? 1 : 2);

}

int clean(){
    OPLDestroy(opl);
    //YMF262Shutdown();

    if(!SDL_WasInit(SDL_INIT_AUDIO)) return;

    SDL_CloseAudio();
    SDL_Quit();

}

void updatechip(int reg, int val)
{
    unsigned char timerL, timerH;
    unsigned int timerLE;

    if (output_format == DRO) {
        if (lastnote < tick) {
            timerLE = tick - lastnote;
            lastnote = tick;
            if (timerLE <= 255){
                fprintf(gifp, "%c%c", 0, (unsigned char)timerLE);
            } else {
                timerL = (unsigned int)(timerLE & 0x00FF);
                timerH = (unsigned int)((timerLE >> 8) & 0x00FF);
                fprintf(gifp, "%c%c%c", 1, timerL, timerH);
            }
        }
        if (reg < 5)
            fprintf(gifp, "%c", 4);
        fprintf(gifp, "%c%c", (unsigned char)reg, (unsigned char)val);
    } else {
        //YMF262Write(0, 0, reg);
        //YMF262Write(0, 1, val);
        OPLWrite(opl, 0, (unsigned char)reg);
        OPLWrite(opl, 1, (unsigned char)val);
    }
}

int fillchip(ADLIB_DATA *aad)
{
    int i;
    unsigned char byte;
    unsigned char oct;  //.xxx....
    unsigned char freq; //....xxxx
    unsigned int tmp1, tmp2;
    unsigned char tmpC;

    aad->skip_delay_counter--;
    if (aad->skip_delay_counter == 0) {
        aad->skip_delay_counter = aad->skip_delay;
        return (aad->cutsong); //Skip (for modifying tempo)
    }
    for (i = 0; i < ADLIB_DATA_COUNT; i++) {
        if (aad->pointer[i] == NULL) continue;
        if (aad->delay_counter[i] > 1) {
            aad->delay_counter[i]--;
            continue;
        }

        do {
        byte = *aad->pointer[i];
        aad->pointer[i]++;
        oct = (byte >> 4) & 0x07;
        freq = byte & 0x0F;
        if (byte & 0x80) { //Escape)
            switch (oct) {
            case 0: //Change duration
                aad->duration[i] = freq;
                break;

            case 1: //Change volume
                aad->volume[i] = freq;
                tmpC = aad->instrument[i]->op[0][2];
                tmpC = (tmpC & 0x3F) - 63;

                tmp1 = (((256 - tmpC) << 4) & 0x0FF0) * (freq + 1);
                tmp1 = 63 - ((tmp1 >> 8) & 0xFF);
                tmp2 = voxp[aad->vox[i]];
                if (tmp2 <= 13)
                    tmp2 += 3;
                tmp2 = opera[tmp2];
                updatechip(0x40 + tmp2, tmp1);
                break;

            case 2: //Change tempo
                aad->tempo[i] = freq;
                break;

            case 3: //Change triple_duration
                aad->triple_duration[i] = freq;
                break;

            case 4: //Change lie
                aad->lie[i] = freq;
                break;

            case 5: //Change vox (channel)
                aad->vox[i] = freq;
                break;

            case 6: //Change instrument
                if (freq == 1) { //Not melodic
                    aad->instrument[i] = &(aad->instrument_data[aad->octave[i] + 15]); //(1st perc instrument is the 16th instrument)
                    aad->vox[i] = aad->instrument[i]->vox;
                    aad->perc_stat = aad->perc_stat | (0x10 >> ((signed int)aad->vox[i] - 6)); //set a bit in perc_stat
                } else {
                    if (freq > 1)
                        freq--;
                    aad->instrument[i] = &(aad->instrument_data[freq]);
                    if (((signed int)aad->vox[i] - 6) >= 0) {
                        tmp1 = (0x10 << ((signed int)aad->vox[i] - 6)) & 0xFF; //
                        aad->perc_stat = aad->perc_stat | tmp1;                //   clear a bit from perc_stat
                        tmp1 = ~(tmp1) & 0xFF;                                 //
                        aad->perc_stat = aad->perc_stat & tmp1;                //
                        updatechip(0xBD, aad->perc_stat); //update perc_stat
                    }
                }
                tmp2 = voxp[aad->vox[i]];
                if (aad->vox[i] <= 6)
                    tmp2 += 3;
                tmp2 = opera[tmp2]; //Adlib channel
                insmaker(aad->instrument[i]->op[0], tmp2);
                if (aad->vox[i] < 7) {
                    insmaker(aad->instrument[i]->op[1], tmp2 - 3);
                    updatechip(0xC0 + aad->vox[i], aad->instrument[i]->fb_alg);
                }
                break;

            case 7: //Extra functions
                switch (freq) {
                case 0: //Call a sub
                    aad->return_point[i] = aad->pointer[i] + 2;
                    tmp1 = ((unsigned int)aad->pointer[i][1] << 8) & 0xFF00;
                    tmp1 += (unsigned int)aad->pointer[i][0] & 0xFF;
                    aad->pointer[i] = aad->data + segment + tmp1;
                    break;

                case 1: //Update loop counter
                    aad->loop_counter[i] = *(aad->pointer[i]);
                    aad->pointer[i]++;
                    break;

                case 2: //Loop
                    if (aad->loop_counter[i] > 1) {
                        aad->loop_counter[i]--;
                        tmp1 = ((unsigned int)aad->pointer[i][1] << 8) & 0xFF00;
                        tmp1 += (unsigned int)aad->pointer[i][0] & 0xFF;
                        aad->pointer[i] = aad->data + segment + tmp1;
                    } else {
                        aad->pointer[i] += 2;
                    }
                    break;

                case 3: //Return from sub
                    aad->pointer[i] = aad->return_point[i];
                    break;

                case 4: //Jump
                    tmp1 = ((unsigned int)aad->pointer[i][1] << 8) & 0xFF00;
                    tmp1 += (unsigned int)aad->pointer[i][0] & 0xFF;
                    aad->pointer[i] = aad->data + segment + tmp1;
                    break;

                case 15: //Finish
                    aad->pointer[i] = NULL;
                    aad->cutsong--;
                    break;

                }
                break;
            }
        }

        } while ((byte & 0x80) && aad->pointer[i]);
        if (aad->pointer[i] == NULL) continue;

        aad->octave[i] = oct;
        aad->freq[i] = freq;

        //Play note
        if (gamme[aad->freq[i]] != 0) {
            if (aad->instrument[i]->vox == 0xFE) { //Play a frequence
                updatechip(0xA0 + aad->vox[i], (unsigned char)(gamme[aad->freq[i]] & 0xFF)); //Output lower 8 bits of frequence
                if (aad->lie_late[i] != 1) {
                    updatechip(0xB0 + aad->vox[i], 0); //Silence the channel
                }
                tmp1 = (aad->octave[i] + 2) & 0x07; //Octave (3 bits)
                tmp2 = (unsigned char)((gamme[aad->freq[i]] >> 8) & 0x03); //Frequency (higher 2 bits)
                updatechip(0xB0 + aad->vox[i], 0x20 + (tmp1 << 2) + tmp2); //Voices the channel, and output octave and last bits of frequency
                aad->lie_late[i] = aad->lie[i];

            } else { //Play a perc instrument
                if (aad->instrument[i] != &(aad->instrument_data[aad->octave[i] + 15])) { //New instrument

                    //Similar to escape, oct = 6 (change instrument)
                    aad->instrument[i] = &(aad->instrument_data[aad->octave[i] + 15]); //(1st perc instrument is the 16th instrument)
                    aad->vox[i] = aad->instrument[i]->vox;
                    aad->perc_stat = aad->perc_stat | (0x10 >> ((signed int)aad->vox[i] - 6)); //set a bit in perc_stat
                    tmp2 = voxp[aad->vox[i]];
                    if (aad->vox[i] <= 6)
                        tmp2 += 3;
                    tmp2 = opera[tmp2]; //Adlib channel
                    insmaker(aad->instrument[i]->op[0], tmp2);
                    if (aad->vox[i] < 7) {
                        insmaker(aad->instrument[i]->op[1], tmp2 - 3);
                        updatechip(0xC0 + aad->vox[i], aad->instrument[i]->fb_alg);
                    }

                    //Similar to escape, oct = 1 (change volume)
                    tmpC = aad->instrument[i]->op[0][2];
                    tmpC = (tmpC & 0x3F) - 63;
                    tmp1 = (((256 - tmpC) << 4) & 0x0FF0) * (aad->volume[i] + 1);
                    tmp1 = 63 - ((tmp1 >> 8) & 0xFF);
                    tmp2 = voxp[aad->vox[i]];
                    if (tmp2 <= 13)
                        tmp2 += 3;
                    tmp2 = opera[tmp2];
                    updatechip(0x40 + tmp2, tmp1);
                }
                tmpC = 0x10 >> ((signed int)aad->vox[i] - 6);
                updatechip(0xBD, aad->perc_stat & ~tmpC); //Output perc_stat with one bit removed
                if (aad->vox[i] == 6) {
                    updatechip(0xA6, 0x57); //
                    updatechip(0xB6, 0);    // Output the perc sound
                    updatechip(0xB6, 5);    //
                }
                updatechip(0xBD, aad->perc_stat); //Output perc_stat
            }
        } else {
            aad->lie_late[i] = aad->lie[i];
        }

        if (aad->duration[i] == 7)
            aad->delay_counter[i] = 0x40 >> aad->triple_duration[i];
        else
            aad->delay_counter[i] = 0x60 >> aad->duration[i];
    }
    return (aad->cutsong);
}

void gen_dro(ADLIB_DATA *aad, int mus_offset, int ins_offset, int song_number, int size){
    unsigned int i, j;
    unsigned char file[20];
    printf("Loading track %d...\n", song_number);
    sprintf(file, "output%d.dro", song_number);
    printf("Output to %s... ", file);
    fflush(stdout);
    gifp = fopen(file, "w");
    fprintf (gifp, "DBRAWOPL%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c", 0, 0, 1, 0, 0, 32, 0, 0, 86, 64, 0, 0, 0, 0, 0, 0);
    load_data(aad, aad->data, aad->data_size, mus_offset, ins_offset, song_number);
    lastnote = 0;
    tick = 0;
    for (i = 0; i < size; i++) {
        debug_counter = i;
        tick += 13;
        fillchip(aad);
    }
    fclose(gifp);
    printf("done\n");
}

void insmaker(unsigned char *insdata, int channel)
{
    updatechip(0x60 + channel, insdata[0]); //Attack Rate / Decay Rate
    updatechip(0x80 + channel, insdata[1]); //Sustain Level / Release Rate
    updatechip(0x40 + channel, insdata[2]); //Key scaling level / Operator output level
    updatechip(0x20 + channel, insdata[3]); //Amp Mod / Vibrato / EG type / Key Scaling / Multiple
    updatechip(0xE0 + channel, insdata[4]); //Wave type
}


int load_file(char *filename, unsigned char **raw_data)
{
    int size = 0, i;

    FILE *ifp;

    ifp = fopen(filename, "rb");

    if (ifp == NULL) {
        fprintf(stderr, "Can't open input file: %s!\n", filename);
        return(-1);
    }

    fseek(ifp, 0L, SEEK_END);
    size = ftell(ifp);
    fseek(ifp, 0, SEEK_SET);
    *raw_data = (unsigned char *)malloc(size + 1);
    if (&raw_data == NULL) {
        fprintf(stderr, "Not enough memory to load file: %s!\n", filename);
        fclose(ifp);
        return -4;
    }

    if ((i = fread(*raw_data, 1, size, ifp)) != size) {
        fprintf(stderr, "Reading error: read %d bytes, should have been %d, in file %s!\n", i, size, filename);
        free (*raw_data);
        fclose(ifp);
        return -5;
    }
    fclose(ifp);
    return size;
}


int load_data(ADLIB_DATA *aad, unsigned char *raw_data, int len, int mus_offset, int ins_offset, int song_number)
{
    int i; //Index
    int j; //Offset to the current offset
    int k;
    unsigned int tmp1; //Source offset
    unsigned int tmp2; //Next offset

    aad->perc_stat = 0x20;

    //Load instruments
    j = ins_offset;

    for (i = 0; i < song_number; i++) {
        do {
            j += 2;
            tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        } while (tmp1 != 0xFFFF);
        j += 2;
    }
    all_vox_zero();
    tmp2 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);

    for (i = 0; i < ADLIB_INSTRUMENT_COUNT + 1; i++)
        aad->instrument_data[i].vox = 0xFF; //Init; instrument not in use

    for (i = 0; (i < ADLIB_INSTRUMENT_COUNT + 1) && ((j + 2) < len); i++) {
        tmp1 = tmp2;
        tmp2 = ((unsigned int)raw_data[j + 2] & 0xFF) + (((unsigned int)raw_data[j + 3] << 8) & 0xFF00);
        j += 2;

        if (tmp2 == 0xFFFF) //Terminate for loop
            break;

        if (tmp1 == 0) //Instrument not in use
            continue;

        if (i > 14) //Perc instrument (15-18) have an extra byte, melodic (0-14) have not
            aad->instrument_data[i].vox = raw_data[(tmp1++) + segment];
        else
            aad->instrument_data[i].vox = 0xFE;

        for (k = 0; k < 5; k++)
            aad->instrument_data[i].op[0][k] = raw_data[(tmp1++) + segment];

        for (k = 0; k < 5; k++)
            aad->instrument_data[i].op[1][k] = raw_data[(tmp1++) + segment];

        aad->instrument_data[i].fb_alg = raw_data[tmp1 + segment];

    }

    //Set skip delay
    aad->skip_delay = tmp1;
    aad->skip_delay_counter = tmp1;

    //Load music
    j = mus_offset;

    for (i = 0; i < song_number; i++) {
        do {
            j += 2;
            tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        } while (tmp1 != 0xFFFF);
        j += 2;
    }

    aad->cutsong = -1;
    for (i = 0; (i < ADLIB_DATA_COUNT + 1) && (j < len); i++) {
        tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        aad->cutsong++;
        if (tmp1 == 0xFFFF) //Terminate for loop
            break;

        aad->duration[i] = 0;
        aad->volume[i] = 0;
        aad->tempo[i] = 0;
        aad->triple_duration[i] = 0;
        aad->lie[i] = 0;
        aad->vox[i] = (unsigned char)i;
        aad->instrument[i] = NULL;
        //aad->instrument[i] = &(aad->instrument_data[0]);
        aad->delay_counter[i] = 0;
        aad->freq[i] = 0;
        aad->octave[i] = 0;
        aad->return_point[i] = NULL;
        aad->loop_counter[i] = 0;
        aad->pointer[i] = aad->data + segment + tmp1;
        aad->lie_late[i] = 0;
        j += 2;
    }
}

void all_vox_zero()
{
    int i;
    for (i = 0xB0; i < 0xB9; i++)
        updatechip(i, 0); //Clear voice, octave and upper bits of frequence
    for (i = 0xA0; i < 0xB9; i++)
        updatechip(i, 0); //Clear lower byte of frequence

    updatechip(0x08, 0x00);
    updatechip(0xBD, 0x00);
    updatechip(0x40, 0x3F);
    updatechip(0x41, 0x3F);
    updatechip(0x42, 0x3F);
    updatechip(0x43, 0x3F);
    updatechip(0x44, 0x3F);
    updatechip(0x45, 0x3F);
    updatechip(0x48, 0x3F);
    updatechip(0x49, 0x3F);
    updatechip(0x4A, 0x3F);
    updatechip(0x4B, 0x3F);
    updatechip(0x4C, 0x3F);
    updatechip(0x4D, 0x3F);
    updatechip(0x50, 0x3F);
    updatechip(0x51, 0x3F);
    updatechip(0x52, 0x3F);
    updatechip(0x53, 0x3F);
    updatechip(0x54, 0x3F);
    updatechip(0x55, 0x3F);
}


void callback(void *userdata, Uint8 *audiobuf, int len)
{
  SDL_PLAYER	*sdlp = (SDL_PLAYER *)userdata;
  static long	minicnt = 0;
  long		i, towrite = len / sdlp->sampsize;
  char		*pos = (char *)audiobuf;
  float refresh = 75.0f;

  // Prepare audiobuf with emulator output
  while(towrite > 0) {

    while(minicnt < 0) {
      minicnt += sdlp->spec.freq;
      sdlp->playing = fillchip(&(sdlp->aad));
   }
    i = (long)(minicnt / refresh + 4) & ~3;
    if (towrite < i)
        i = towrite;

    YM3812UpdateOne(opl, (short *)pos, i);
    //YMF262UpdateOne(0, (INT16 *)pos, (int)i / 4);

    pos += i * sdlp->sampsize;
    towrite -= i;
     minicnt -= (long)(refresh * i);
  }
}

int main(int argc, char *argv[]){
    int i;

    unsigned char *output;
    unsigned char *inbuffer;
    int retval, in_len;
    char buffer [1024];
    char *newline;
    FILE *ifp, *ofp;
    int song, size;

    printf("\nTITUS standalone audio player v1.0\n\n");
    printf("A working music player, which converts music stored in the executable into a OPL2 stream.\n");
    printf("Usage: Put audioplay in your titus folder. Audioplay will look for FOX.EXE, if the file doesn't exist it will SQZ extract it from FOX.COM.\n\n");
    printf("Usage: audioplay song_number output output_size\n\n");
    printf("Song number is 0 to 15.\n");
    printf("Output: Optional, DIRECT (default) or DRO for making outputX.dro.\n");
    printf("Output size: size of DRO file, default 4000.\n");
    printf("Example: audioplay 2 DRO 4000\n\n");
    printf("Audioplay's OPL2 emulator is really bad, that's why I added the option to output DRO v0.1, which can be used with a better emulator.\n");
    printf("If you have the knowledge to implement a better OPL2 emulator in OpenTitus, reachable from C, it will be much appreciated. I think the best option is to port the one from dosbox.\n");
    printf("To listen to a DRO v0.1 file, just google opl2wav.\n\n");

    song = 0;
    output_format = DIRECT;
    size = 4000;

    for (i = 0; i < argc; i++) {
        switch (i) {
        case 1:
            song = atoi(argv[1]);
            if ((song < 0) || (song > 15)) {
                printf("Invalid song number\n");
                return -1;
            }
            break;

        case 2:
            if (strcmp(argv[2], "DRO") == 0) {
                output_format = DRO;
            } else if ((strcmp(argv[2], "DIRECT") == 0) || (strcmp(argv[2], "") == 0)) {
                output_format = DIRECT;
            } else {
                printf("Invalid output\n");
                return -1;
            }
            break;

        case 3:
            size = atoi(argv[3]);
            if (song == 0)
                size = 4000;
            break;
        }
    }
//return (0);
    printf("Looking for FOX.EXE... ");
    fflush(stdout);
    ifp = fopen("FOX.EXE", "rb");
    if (ifp == NULL) {
        printf("not found\n");
        printf("Looking for FOX.COM... ");
        fflush(stdout);
        ifp = fopen("FOX.COM", "rb");
        if (ifp == NULL) {
            printf("not found\n\n");
            printf("FOX.COM not found, have you placed the executable in the correct folder?\n");
            printf("(Keep in mind that some file systems are case sensitive, try to rename fox.com to FOX.COM)\n");
            return (-1);
        }
        printf("found\n");


        fputs("\nDo you want to SQZ-extract FOX.EXE from the source file FOX.COM (Y/N)? ", stdout);
        fflush(stdout);

        if (fgets(buffer, sizeof buffer, stdin) != NULL) {
            newline = strchr(buffer, '\n'); // search for newline character
            if (newline != NULL) {
                *newline = '\0'; // overwrite trailing newline
            }
        }

        printf("\n");

        if (buffer[1] == 0) {
            switch (buffer[0]) {
            case 'Y': case 'y':
                break;
            case 'N': case 'n':
                printf("Audioplay requires FOX.EXE in order to work.\n\n");
                return (-1);
                break;
            default:
                printf("Invalid selection (%s), selecting NO.\n", buffer);
                printf("Audioplay requires FOX.EXE in order to work.\n\n");
                return (-1);
            }
        } else {
            printf("Invalid selection (%s), selecting NO.\n\n", buffer);
            printf("Audioplay requires FOX.EXE in order to work.\n\n");
            return (-1);
        }

        fseek(ifp, 0L, SEEK_END);
        in_len = ftell(ifp) - 790;
        //FOX.COM contains a SQZ-compressed executable, at offset 790, in addition to SQZ-extracting code
        if (in_len + 790 != 44389) {
            printf("You have an incompatible version of FOX.COM.");
            fclose (ifp);
            return (-1);
        }
        fseek(ifp, 790, SEEK_SET);
        inbuffer = (unsigned char *)malloc(sizeof(unsigned char)*in_len);
        if (inbuffer == NULL) {
            fprintf(stderr, "Error: Not enough memory (input buffer) to decompress FOX.COM, needs %lu bytes!\n", sizeof(unsigned char) * in_len);
            fclose (ifp);
            return (-1);
        }

        if ((i = fread(inbuffer, 1, in_len, ifp)) != in_len) {
            fprintf(stderr, "Error: Invalid filesize: %d bytes, should have been %d bytes, in file %s!\n", i + 790, in_len + 790, "FOX.COM");
            free (inbuffer);
            fclose (ifp);
            return (-1);
        }

        fclose (ifp);

        retval = unSQZ(inbuffer, in_len, &output);

        if ((retval > 0) && (output != NULL)) {
            ofp = fopen("FOX.EXE", "wb");
            if (ofp == NULL) {
                fprintf(stderr, "Can't open output file!\n");
                free (inbuffer);
                free (output);
                return(-1);
            }
            fwrite(output, 1, retval, ofp);
            fclose (ofp);
        } else {
            fprintf(stderr, "Extracted content is invalid!\n");
            free (inbuffer);
            free (output);
            return(-1);
        }
        free (output);
        free (inbuffer);

        if (retval != 63985) {
            printf("FOX.COM extracted successfully, but the extracted executable is incompatible.");
            return (-1);
        }
    } else {
        printf("found\n");
        fseek(ifp, 0L, SEEK_END);
        in_len = ftell(ifp);
        if (in_len != 63985) {
            printf("You have an incompatible version of FOX.EXE, try to rename/delete it and restart audioplay.");
            fclose (ifp);
            return (-1);
        }
        fclose (ifp);
    }

    init();

    sdl_player_data.aad.data_size = load_file("FOX.EXE", &(sdl_player_data.aad.data));
    if (sdl_player_data.aad.data_size < 0) {
        clean();
        return 0;
    }
    if (output_format == DRO) {
        gen_dro(&(sdl_player_data.aad), MUS_OFFSET, INS_OFFSET, song, size);
    } else {
        printf("Loading track %d...\n", song);
        load_data(&(sdl_player_data.aad), sdl_player_data.aad.data, sdl_player_data.aad.data_size, MUS_OFFSET, INS_OFFSET, song);
        play();
    }

    free (sdl_player_data.aad.data);

    clean();
    return 0;
}
