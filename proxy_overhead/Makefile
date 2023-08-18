CC=mpicc

all: test calibrate

calibrate: calibrate.c timer.c
	$(CC) $^ -o $@

test: t.c  timer.c
	$(CC) -g $^ -o $@


clean:
	rm -fr test calibrate
