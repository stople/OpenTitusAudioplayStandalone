AUDIOPLAY_TARG := audioplay
AUDIOPLAY_OBJS := \
	fmopl.o \
	sqz.o \
	audioplay.o

DIRS = .

all : $(AUDIOPLAY_TARG)

$(AUDIOPLAY_TARG) : $(AUDIOPLAY_OBJS)
	$(CC) $(CFLAGS) -o $(AUDIOPLAY_TARG) $(AUDIOPLAY_OBJS) `sdl-config --cflags --libs` -lm -lbinio

$(OPLTEST_TARG) : $(OPLTEST_OBJS)
	$(CC) $(CFLAGS) -o $(OPLTEST_TARG) $(OPLTEST_OBJS) -lm -lbinio

clean :
	rm -f $(foreach dir,$(DIRS),$(foreach suffix,/*.o, $(addsuffix $(suffix),$(dir))))
