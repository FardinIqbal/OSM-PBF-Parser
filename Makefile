CC := gcc
SRCD := src
TSTD := tests
BLDD := build
BIND := bin
INCD := include

EXEC := pbf
TEST_EXEC := $(EXEC)_tests

MAIN  := $(BLDD)/main.o

ALL_SRCF := $(shell find $(SRCD) -type f -name *.c)
ALL_OBJF := $(patsubst $(SRCD)/%,$(BLDD)/%,$(ALL_SRCF:.c=.o))
ALL_FUNCF := $(filter-out $(MAIN) $(AUX), $(ALL_OBJF))

TEST_SRC := $(shell find $(TSTD) -type f -name *.c)

INC := -I $(INCD)

CFLAGS := -fcommon -Wall -Werror -Wno-unused-function -MMD
COLORF := -DCOLOR
DFLAGS := -g -DDEBUG -DCOLOR
PRINT_STAMENTS := -DERROR -DSUCCESS -DWARN -DINFO

STD := -std=gnu11
TEST_LIB := -lcriterion
LIBS := -lz

CFLAGS += $(STD)

.PHONY: clean all setup debug

all: setup $(BIND)/$(EXEC) $(BIND)/$(TEST_EXEC)

debug: CFLAGS += $(DFLAGS) $(PRINT_STAMENTS) $(COLORF)
debug: all

setup: $(BIND) $(BLDD)
$(BIND):
	mkdir -p $(BIND)
$(BLDD):
	mkdir -p $(BLDD)

$(BIND)/$(EXEC): $(MAIN) $(ALL_FUNCF)
	$(CC) $(CFLAGS) $(INC) $(MAIN) $(ALL_FUNCF) -o $@ $(LIBS)

$(BIND)/$(TEST_EXEC): $(ALL_FUNCF) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INC) $(ALL_FUNCF) $(TEST_SRC) $(TEST_LIB) $(LIBS) -o $@

$(BLDD)/%.o: $(SRCD)/%.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

clean:
	rm -rf $(BLDD) $(BIND)

.PRECIOUS: $(BLDD)/*.d
-include $(BLDD)/*.d
