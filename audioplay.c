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
#include <math.h>


#define ADLIB_DATA_COUNT 10
#define ADLIB_INSTRUMENT_COUNT 19
#define ADLIB_SFX_COUNT 14

const int FREQ_RATE = 44100;
#define BUF_SIZE 2048
//const int AMPLITUDE = 28000;
const int AMPLITUDE = 28000;


char playing;
int rate;

unsigned char opera[] = {0,0,1,2,3,4,5,8,9,0xA,0xB,0xC,0xD,0x10,0x11,0x12,0x13,0x14,0x15};
unsigned char voxp[] = {1,2,3,7,8,9,13,17,15,18,14};
unsigned int gamme[] = {343,363,385,408,432,458,485,514,544,577,611,647,0};
unsigned int bgamme[] = {36485,34437,32505,30680,28959,27333,25799,24351,22984,21694,20477,19327,10};
unsigned char btempo[] = {4,6,6,10,8,3,0,0,6,0,10,2,3,3,0,0};

FM_OPL *opl;

FILE* gifp;

typedef enum _OUTPUT_FORMAT OUTPUT_FORMAT;
enum _OUTPUT_FORMAT{DIRECT, DRO, BUZZER};

int song;
OUTPUT_FORMAT output_format;

int debug_test[20];

int debug_counter;

unsigned int lastnote, tick; //debug

uint16_t MUS_OFFSET, INS_OFFSET, SFX_OFFSET, BUZ_OFFSET;
uint8_t SONG_COUNT, SFX_COUNT;
uint8_t AUDIOVERSION, AUDIOTYPE; //AUDIOTYPE: 1=TTF/MOK, 2=Blues Brothers
uint8_t FX_ON;
uint16_t FX_TIME;
uint8_t AUDIOTIMING;
int lastaudiotick;
uint8_t audiodelay;
int16_t pointer_diff;

uint16_t buzzerFreq;


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
    unsigned char instrument_idx[ADLIB_DATA_COUNT];

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
    int sample_nr;
} SDL_PLAYER;


SDL_PLAYER sdl_player_data;

int play();
int init();
int clean();
void updatechip(int reg, int val);
int fillchip(ADLIB_DATA *aad);
void insmaker(unsigned char *insdata, int channel);
int load_file(char *filename, unsigned char **raw_data);
void all_vox_zero();
void callback(void *userdata, Uint8 *audiobuf, int len);
//int main(int argc, char *argv[]);

unsigned int loaduint16(unsigned char c1, unsigned char c2){
    return (((unsigned int)c1 * 256) & 0xFF00) + (unsigned int)c2;
}

int loadint16(unsigned char c1, unsigned char c2){
    short int tmpint = ((short int)c1 << 8) + (short int)c2;
    return (int)tmpint;
}


int play(){
    int i;
    printf("Playing... (terminate with Ctrl-C)\n");
    while(1){ //Play until ctrl-c
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
    
    buzzerFreq = 0;

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

void setBuzzerFreqFromDivider(unsigned int divider)
{
    double temp = (double)1193182 / (double)divider;
    buzzerFreq = (unsigned int)temp;
}

void debugPrint(const char* format, ...)
{
    return;
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

void notePrint(const char* format, ...)
{
    //return;
    va_list args;
    va_start (args, format);
    vprintf (format, args);
    va_end (args);
}

void dumpBlankNote(int i)
{
    //if (i != 0) return;
    notePrint("|...........");
}

void dumpNote(ADLIB_DATA *aad, int i)
{
    //if (i != 0) return;
    char notes0[] = {'C','C','D','D','E','F','F','G','G','A','A','B'};
    char notes1[] = {'-','#','-','#','-','-','#','-','#','-','#','-'};
    
    int freq = aad->freq[i];
    int octave = aad->octave[i];
    octave += 3;
    if (freq > 11)
    {
        dumpBlankNote(i);
        return;
    }
    
    int instrument = aad->instrument_idx[i];
    //instrument = 13;

    notePrint("|%c%c%i%02iv64...", notes0[freq], notes1[freq], octave, instrument);
    
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
    printf("\n");

    for (i = 0; i < ADLIB_DATA_COUNT; i++) {
        if (output_format == BUZZER && i != 0) break;
        //if (i != 0) break;
        
        if (aad->pointer[i] == NULL) continue; //pointer: music data
        
        if (aad->delay_counter[i] > 1) { //Wait for next instruction
            debugPrint("WAIT cnt:%i ", aad->delay_counter[i]);
            dumpBlankNote(i);
            if (aad->lie[i]) debugPrint("LIE lie:%i ", aad->lie[i]);
            if (aad->delay_counter[i] == 2 && output_format == BUZZER && aad->lie[i] != 1)
            {
                //Make a "switch off"-sound
                //setBuzzerFreqFromDivider(15000); //TTF
                buzzerFreq = 0; //BB
            }
            
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
                debugPrint("DURATION %i ", freq);
                aad->duration[i] = freq;
                break;

            case 1: //Change volume
                debugPrint("VOLUME %i ", freq);
                aad->volume[i] = freq;
                if (output_format == BUZZER)
                {
                    debugPrint("\nVolume on buzzer...\n");
                    break;
                }
                
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
                debugPrint("TEMPO %i ", freq);
                aad->tempo[i] = freq;
                break;

            case 3: //Change triple_duration
                debugPrint("TRIPLE %i ", freq);
                aad->triple_duration[i] = freq;
                break;

            case 4: //Change lie (0: turn off note when changing tone. 1: Switch tone without turning off)
                debugPrint("LIE %i ", freq);
                aad->lie[i] = freq;
                break;

            case 5: //Change vox (OPL operator index 0x00-0x15)
                debugPrint("VOX/CHANNEL %i ", freq);
                aad->vox[i] = freq;
                break;

            case 6: //Change instrument
                debugPrint("INSTRUMENT %i ", freq);
                if (output_format == BUZZER)
                {
                    debugPrint("\nIns on buzzer...\n");
                    break;
                }
                if (freq == 1) { //Not melodic
                    aad->instrument[i] = &(aad->instrument_data[aad->octave[i] + 15]); //(1st perc instrument is the 16th instrument)
                    aad->instrument_idx[i] = aad->octave[i] + 15; //(1st perc instrument is the 16th instrument)
                    aad->vox[i] = aad->instrument[i]->vox;
                    aad->perc_stat = aad->perc_stat | (0x10 >> ((signed int)aad->vox[i] - 6)); //set a bit in perc_stat (trig perc sound 0-4)
                } else {
                    if (freq > 1)
                        freq--;
                    aad->instrument[i] = &(aad->instrument_data[freq]);
                    aad->instrument_idx[i] = freq;
                    if (((signed int)aad->vox[i] - 6) >= 0) {
                        //tmp1 = (0x10 << ((signed int)aad->vox[i] - 6)) & 0xFF; //
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
                    debugPrint("CALLSUB %i ", tmp1);
                    aad->pointer[i] = aad->data + tmp1 + pointer_diff;
                    break;

                case 1: //Update loop counter
                    aad->loop_counter[i] = *(aad->pointer[i]);
                    debugPrint("LOOPCTR %i ", aad->loop_counter[i]);
                    aad->pointer[i]++;
                    break;

                case 2: //Loop
                    if (aad->loop_counter[i] > 1) {
                        aad->loop_counter[i]--;
                        tmp1 = ((unsigned int)aad->pointer[i][1] << 8) & 0xFF00;
                        tmp1 += (unsigned int)aad->pointer[i][0] & 0xFF;
                        debugPrint("LOOP dest:%i ctr:%i", tmp1, aad->loop_counter[i]);
                        aad->pointer[i] = aad->data + tmp1 + pointer_diff;
                    } else {
                        debugPrint("LOOP end");
                        aad->pointer[i] += 2;
                    }
                    break;

                case 3: //Return from sub
                    debugPrint("RETURNSUB ");
                    aad->pointer[i] = aad->return_point[i];
                    break;

                case 4: //Jump
                    tmp1 = ((unsigned int)aad->pointer[i][1] << 8) & 0xFF00;
                    tmp1 += (unsigned int)aad->pointer[i][0] & 0xFF;
                    debugPrint("JUMP %i ", tmp1);
                    aad->pointer[i] = aad->data + tmp1 + pointer_diff;
                    break;

                case 15: //Finish
                    debugPrint("FINISH ");
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

        debugPrint("NOTE oct:%i freq:%i ", oct, freq);

        dumpNote(aad, i);
        
        //Play note
        if (output_format == BUZZER)
        {
            if (freq == 12) buzzerFreq = 0;
            else setBuzzerFreqFromDivider(bgamme[freq] >> oct);
        }
        
        else if (gamme[aad->freq[i]] != 0) {
            if (aad->instrument[i]->vox == 0xFE) { //Play a frequence
                updatechip(0xA0 + aad->vox[i], (unsigned char)(gamme[aad->freq[i]] & 0xFF)); //Output lower 8 bits of frequence
                
                //B0:
                //00000011 - freqH
                //00011100 - octave
                //00100000 - key on

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
                    aad->instrument_idx[i] = aad->octave[i] + 15; //(1st perc instrument is the 16th instrument)
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
                    updatechip(0xA6, 0x57); // Perc frequency
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
    uint8_t buffer[100];
    uint16_t data_offset, data_size;
    ifp = fopen(filename, "rb");

    if (ifp == NULL) {
        fprintf(stderr, "Can't open input file: %s!\n", filename);
        return(-1);
    }

    
    if ((i = fread(buffer, 1, 18, ifp)) != 18) {
        fprintf(stderr, "Reading error: read %d bytes, should have been %d, in file %s!\n", i, size, filename);
        fclose(ifp);
        return -5;
    }
    if (strncmp (buffer, "OPENTITUSAUDIO", 14) != 0) {
        fprintf(stderr, "Invalid audio file format in file %s!\n", filename);
        fclose(ifp);
        return -5;
    }
    sscanf (buffer + 14, "%c%c", &AUDIOVERSION, &AUDIOTYPE);
    if (AUDIOVERSION != 1) {
        fprintf(stderr, "Unsupported version of audiofile %s!\n", filename);
        fclose(ifp);
        return -5;
    }
    if ((AUDIOTYPE != 1) && (AUDIOTYPE != 2)) {
        fprintf(stderr, "Unsupported type of audio in file %s!\n", filename);
        fclose(ifp);
        return -5;
    }

    if (AUDIOTYPE == 1) { //TTF/MOK

        if ((i = fread(buffer, 1, 17, ifp)) != 17) {
            fprintf(stderr, "Reading error: read %d bytes, should have been %d, in file %s!\n", i, size, filename);
            fclose(ifp);
            return -5;
        }

        data_offset = loaduint16(buffer[1], buffer[0]);
        data_size = loaduint16(buffer[3], buffer[2]);
        pointer_diff = loadint16(buffer[6], buffer[5]);
        MUS_OFFSET = loaduint16(buffer[8], buffer[7]);
        INS_OFFSET = loaduint16(buffer[10], buffer[9]);
        SFX_OFFSET = loaduint16(buffer[12], buffer[11]);
        BUZ_OFFSET = loaduint16(buffer[14], buffer[13]);
        SONG_COUNT = buffer[15];
        SFX_COUNT = buffer[16];

    } else if (AUDIOTYPE == 2) { //BB
        
        if ((i = fread(buffer, 1, 15, ifp)) != 15) {
            fprintf(stderr, "Reading error: read %d bytes, should have been %d, in file %s!\n", i, size, filename);
            fclose(ifp);
            return -5;
        }

        data_offset = loaduint16(buffer[1], buffer[0]);
        data_size = loaduint16(buffer[3], buffer[2]);
        pointer_diff = loadint16(buffer[6], buffer[5]);
        MUS_OFFSET = loaduint16(buffer[8], buffer[7]);
        SFX_OFFSET = loaduint16(buffer[10], buffer[9]);
        BUZ_OFFSET = loaduint16(buffer[12], buffer[11]);
        SONG_COUNT = buffer[13];
        SFX_COUNT = buffer[14];

    }
    if (SFX_COUNT > ADLIB_SFX_COUNT) {
        SFX_COUNT = ADLIB_SFX_COUNT;
    }

    *raw_data = (unsigned char *)malloc(data_size);
    if (&raw_data == NULL) {
        fprintf(stderr, "Not enough memory to load file: %s!\n", filename);
        fclose(ifp);
        return -4;
    }

    fseek(ifp, data_offset, SEEK_SET);

    if ((i = fread(*raw_data, 1, data_size, ifp)) != data_size) {
        fprintf(stderr, "Reading error: read %d bytes, should have been %d, in file %s!\n", i, size, filename);
        free (*raw_data);
        fclose(ifp);
        return -5;
    }
    fclose(ifp);
    return data_size;
}


int load_data(ADLIB_DATA *aad, unsigned char *raw_data, int song_number)
{
    int i; //Index
    int j; //Offset to the current offset
    int k;
    unsigned int tmp1; //Source offset
    unsigned int tmp2; //Next offset

    aad->perc_stat = 0x20; //Rythm mode

    //Load instruments
    j = INS_OFFSET;

    all_vox_zero();
    
    //Load instruments
    if (AUDIOTYPE == 1) { //TTF/MOK
        j = INS_OFFSET;
        for (i = 0; i < song_number; i++) {
            do {
                j += 2;
                tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
            } while (tmp1 != 0xFFFF);
            j += 2;
        }
    } else if (AUDIOTYPE == 2) { //BB
        j = MUS_OFFSET + song_number * 8 + 2 + pointer_diff;
        tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        j = tmp1 + pointer_diff;
    }

    
    tmp2 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);

    for (i = 0; i < ADLIB_INSTRUMENT_COUNT + 1; i++)
        aad->instrument_data[i].vox = 0xFF; //Init; instrument not in use

    for (i = 0; (i < ADLIB_INSTRUMENT_COUNT + 1) && ((j + 2) < aad->data_size); i++) {
        tmp1 = tmp2;
        tmp2 = ((unsigned int)raw_data[j + 2] & 0xFF) + (((unsigned int)raw_data[j + 3] << 8) & 0xFF00);
        j += 2;

        if (tmp2 == 0xFFFF) //Terminate for loop
            break;

        if (tmp1 == 0) //Instrument not in use
            continue;

        if (i > 14) //Perc instrument (15-18) have an extra byte, melodic (0-14) have not
            aad->instrument_data[i].vox = raw_data[(tmp1++) + pointer_diff];
        else
            aad->instrument_data[i].vox = 0xFE;

        for (k = 0; k < 5; k++)
            aad->instrument_data[i].op[0][k] = raw_data[(tmp1++) + pointer_diff];

        for (k = 0; k < 5; k++)
            aad->instrument_data[i].op[1][k] = raw_data[(tmp1++) + pointer_diff];

        aad->instrument_data[i].fb_alg = raw_data[tmp1 + pointer_diff];

    }

    //Set skip delay
    if (AUDIOTYPE == 1) { //TTF/MOK
        aad->skip_delay = tmp1;
        aad->skip_delay_counter = tmp1;
    } else if (AUDIOTYPE == 2) { //BB
        j = MUS_OFFSET + song_number * 8 + 4 + pointer_diff;
        tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        aad->skip_delay = tmp1;
        aad->skip_delay_counter = tmp1;
    }

    //Load music
    j = MUS_OFFSET;

    if (AUDIOTYPE == 1) { //TTF/MOK
        for (i = 0; i < song_number; i++) {
            do {
                j += 2;
                tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
            } while (tmp1 != 0xFFFF);
            j += 2;
        }
    } else if (AUDIOTYPE == 2) { //BB
        j = MUS_OFFSET + song_number * 8 + pointer_diff;
        tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        j = tmp1 + pointer_diff;
    }

    aad->cutsong = -1;
    for (i = 0; (i < ADLIB_DATA_COUNT + 1) && (j < aad->data_size); i++) {
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
        aad->instrument_idx[i] = 0;
        //aad->instrument[i] = &(aad->instrument_data[0]);
        aad->delay_counter[i] = 0;
        aad->freq[i] = 0;
        aad->octave[i] = 0;
        aad->return_point[i] = NULL;
        aad->loop_counter[i] = 0;
        aad->pointer[i] = aad->data + tmp1 + pointer_diff;
        aad->lie_late[i] = 0;
        j += 2;
    }
}

int load_data_buzzer(ADLIB_DATA *aad, unsigned char *raw_data, int song_number)
{
    int i; //Index
    int j; //Offset to the current offset
    int k;
    unsigned int tmp1; //Source offset
    unsigned int tmp2; //Next offset

    //Set skip delay
    if (AUDIOTYPE == 1) { //TTF/MOK
        aad->skip_delay = btempo[song_number];
        aad->skip_delay_counter = btempo[song_number];
    } else if (AUDIOTYPE == 2) { //BB
        j = BUZ_OFFSET + song_number * 4 + 2 + pointer_diff;
        tmp1 = ((unsigned int)raw_data[j] & 0xFF) + (((unsigned int)raw_data[j + 1] << 8) & 0xFF00);
        aad->skip_delay = tmp1;
        aad->skip_delay_counter = tmp1;
    }

    //Load music
    if (AUDIOTYPE == 1) { //TTF/MOK
        j = BUZ_OFFSET + song_number * 2;
    } else if (AUDIOTYPE == 2) { //BB
        j = BUZ_OFFSET + song_number * 4 + pointer_diff;
    }

    aad->cutsong = -1;
    for (i = 0; i < 1 && (j < aad->data_size); i++) {
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
        aad->instrument_idx[i] = 0;
        //aad->instrument[i] = &(aad->instrument_data[0]);
        aad->delay_counter[i] = 0;
        aad->freq[i] = 0;
        aad->octave[i] = 0;
        aad->return_point[i] = NULL;
        aad->loop_counter[i] = 0;
        aad->pointer[i] = aad->data + tmp1 + pointer_diff;
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

double squaresine(double val)
{
    if (sin(val) > 0) return 1;
    else return -1;
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
      
      //generate input to emulator
      sdlp->playing = fillchip(&(sdlp->aad));
      //buzzerFreq = 440;
   }
    i = (long)(minicnt / refresh + 4) & ~3;
    if (towrite < i)
        i = towrite;

    //Fetch audio output from emulator
    if (output_format == BUZZER)
    {
        //write i samples with buzzer freq
        //https://stackoverflow.com/a/45002609
        short *p = (short *)pos;
        for(int j = 0; j < i; j++, sdlp->sample_nr++)
        {
            double time = (double)sdlp->sample_nr / (double)FREQ_RATE;
            if (buzzerFreq == 0) *p++ = (Sint16)0;
            else
            {
                *p++ = (Sint16)(AMPLITUDE * squaresine(2.0f * M_PI * buzzerFreq * time)); // render 441 HZ sine wave
            }
        }
        
    } else {
        YM3812UpdateOne(opl, (short *)pos, i);
    }
    
    
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
    
    printf("\nTITUS standalone audio player v2.0\n\n");
    printf("A working music player, which converts music stored in an extracted OPENTITUSAUDIO file into a OPL2 stream.\n");
    printf("Usage: audioplay file song_number output output_size\n\n");
    printf("Song number is 0 to 15.\n");
    printf("Output: Optional, DIRECT (default) or DRO for making outputX.dro.\n");
    printf("Output size: size of DRO file, default 4000.\n");
    printf("Example: audioplay 2 DRO 4000\n\n");
    printf("Audioplay's OPL2 emulator is really bad, that's why I added the option to output DRO v0.1, which can be used with a better emulator.\n");
    printf("If you have the knowledge to implement a better OPL2 emulator in OpenTitus, reachable from C, it will be much appreciated. I think the best option is to port the one from dosbox.\n");
    printf("To listen to a DRO v0.1 file, just google opl2wav.\n\n");

    song = 0;
    //output_format = DIRECT;
    output_format = BUZZER;
    size = 4000;

    for (i = 0; i < argc; i++) {
        switch (i) {
        case 1: //source file (argv[1])

            break;

        case 2: //song number
            song = atoi(argv[2]);
            if ((song < 0) || (song > 15)) {
                printf("Invalid song number\n");
                return -1;
            }
            break;

        case 3: //output type
            if (strcmp(argv[3], "DRO") == 0) {
                output_format = DRO;
            } else if (strcmp(argv[3], "BUZZER") == 0) {
                output_format = BUZZER;
            } else if ((strcmp(argv[3], "DIRECT") == 0) || (strcmp(argv[3], "") == 0)) {
                output_format = DIRECT;
            } else {
                printf("Invalid output\n");
                return -1;
            }
            break;

        case 4: //output size
            size = atoi(argv[4]);
            if (song == 0)
                size = 4000;
            break;
        }
    }
//return (0);


    init();

    sdl_player_data.aad.data_size = load_file(argv[1], &(sdl_player_data.aad.data));
    if (sdl_player_data.aad.data_size < 0) {
        clean();
        return 0;
    }
    
    //initializeOpenTitusAudioFile(sdl_player_data.aad.data);
    
    if (output_format == DRO) {
        gen_dro(&(sdl_player_data.aad), MUS_OFFSET, INS_OFFSET, song, size);
    } else if (output_format == DIRECT){
        printf("Loading track %d...\n", song);
        load_data(&(sdl_player_data.aad), sdl_player_data.aad.data, song);
        play();
    } else if (output_format == BUZZER){
        printf("Loading track %d...\n", song);
        load_data_buzzer(&(sdl_player_data.aad), sdl_player_data.aad.data, song);
        play();
    }

    free (sdl_player_data.aad.data);

    clean();
    return 0;
}
