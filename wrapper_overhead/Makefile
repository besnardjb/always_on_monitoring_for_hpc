CC=mpicc

all: test interpos_tu interpos_static interpos_lib calibrate

calibrate: calibrate.c timer.c
	$(CC) $^ -o $@

test: t.c  timer.c
	$(CC) -g $^ -o $@

interpos_static: interpos.c timer.c
	$(CC) -O3 -DSTATIC_INTER=1 $^ -o $@

interpos_tu: interpos.c cr.c  timer.c
	$(CC) -O3 $^ -o $@

libinter.so: cr.c  timer.c
	$(CC) -shared -fpic $^ -o $@

interpos_lib: interpos.c libinter.so
	$(CC)  -O3 interpos.c  timer.c -o $@ -L. -linter  -Wl,-rpath=$(PWD) 


clean:
	rm -fr test interpos_tu interpos_static interpos_lib calibrate libinter.so
