PREFIX=/usr/local

COMPILE_FLAGS = -O3 -funroll-loops -funroll-all-loops -I/usr/local/include
#COMPILE_FLAGS = -g

LINK_FLAGS    = `pkg-config jack --libs` `pkg-config sndfile --libs` `pkg-config fftw3f --libs` `pkg-config samplerate --libs` -L/usr/local/lib -ldsp -lconvolve

all: jack_convolve

all_c_cmul: jack_convolve

jack_convolve: jack_convolve.o 
	g++ -o jack_convolve jack_convolve.o  $(LINK_FLAGS)

jack_convolve.o: jack_convolve.cc
	g++ -c jack_convolve.cc $(COMPILE_FLAGS)

.PHONY: clean
clean:
	rm -f jack_convolve *~ *.o convolvetest core* *.lst

.PHONY: install
install: jack_convolve
	cp jack_convolve $(PREFIX)/bin/
