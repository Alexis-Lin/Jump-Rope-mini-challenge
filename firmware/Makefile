CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -Iinclude
LDLIBS = -lm

demo: src/atom_app.c src/atom_render.c src/atom_pose_sim.c src/main_demo.c
	$(CC) $(CFLAGS) -o demo $^ $(LDLIBS)

clean:
	rm -rf demo out
.PHONY: demo clean
