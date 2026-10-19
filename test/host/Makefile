# Host unit tests for the door logic (state machine, current guard, latch).
#
#   cd test/host && make        # build + run
#   make clean
#
# Only needs gcc and make; no ESP-IDF, no board.

COMP    := ../../components
CFLAGS  := -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
           -fsanitize=address,undefined \
           -I$(COMP)/door/include
LDLIBS  := -lm

TESTS := test_door_fsm

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@echo "ALL HOST TESTS PASSED"

test_door_fsm: test_door_fsm.c $(COMP)/door/door_fsm.c $(COMP)/door/motor_control.c \
               $(COMP)/door/current_guard.c $(COMP)/door/door_lock.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f $(TESTS)

.PHONY: all clean
