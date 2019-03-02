#ifndef TITUS_ERROR_H
#define TITUS_ERROR_H

#define TITUS_ERROR_QUIT -1
#define TITUS_ERROR_NOT_ENOUGH_MEMORY -2
#define TITUS_ERROR_FILE_NOT_FOUND -3
#define TITUS_ERROR_INVALID_FILE -4
#define TITUS_ERROR_SDL_ERROR -5
#define TITUS_ERROR_AUDIO_ERROR -6
char lasterror[200];
int lasterrornr; //Only to be used when needed, f.ex. when return value is not int (f.ex. in function SDL_Text) (maybe this always should be used?)
void checkerror(void);

#endif
