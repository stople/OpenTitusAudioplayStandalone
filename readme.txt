Audioplay standalone edition v1.0

Audioplay standalone edition is a demonstration of open source TTF audio player code, written in C.
This code was written in 2008/2009, I've just modified it slightly to make a standalone application of it.

Audioplay is a part of the OpenTitus project, which aims at making an open source environment to play Titus the fox, with better modification possibilities.
If you are interested in recent info regarding the OpenTitus project, have a look in opentitus.txt.

You need the original Titus the fox game in order to use Audioplay. Simply put the audioplay executable in that folder, launch your command line interface and run it.
At first it will prompt you to extract FOX.EXE from FOX.COM. Press Y.
Now you should listen to TTF music, but with awful quality! This is because the only OPL2 emulator I've been able to implement is this bad (taken from AdPlug, a very old MAME OPL2 emulator).....

Usage: audioplay song_number output output_size
Song number is 0 to 15
Output: Optional, DIRECT (default) or DRO for making outputX.dro
Output size: size of DRO file, default 4000

Example: audioplay 2 DRO 4000

If you want to listen to the music in its beauty, you can output a DRO v0.1 file and play that in a OPL player with a better emulator (opl2wav works great!), or even better: implement a better OPL2 emulator in audioplay! If you can manage the last option, I would be happy to include that fix in OpenTitus/Audioplay! I suggest the use of Dosbox' adlib emulator, that is written in C++, and is very dependent on the rest of dosbox. The emulator should be reachable from C, f.ex. as an external library or similar. The emulator should be compatible with GPLv3.

The OpenTitus team
Oct 1st, 2010
