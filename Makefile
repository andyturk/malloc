# override root locations in local_vars.mk
-include local_vars.mk

# The following variables ending in _ROOT are directories where external
# things are installed. The location of these may change depending on
# how the host machine is configured. Override the definitions here in
# the optional local_vars.mk
GTEST_ROOT              ?= googletest

# Where build products go
BUILD                   := build
OBJ                      = $(BUILD)/obj

# Source files
C_SRC                   += 
#CXX_SRC                 += main.cc

CXX_SRC                 += $(wildcard *test.cc)

GTEST_SRC               += $(GTEST_ROOT)/src/gtest-all.cc
GTEST_SRC               += $(GTEST_ROOT)/src/gtest_main.cc
GTEST_OBJS               = $(addprefix $(OBJ)/, $(GTEST_SRC:.cc=.o))

# Object files
OBJECTS                  = $(addprefix $(OBJ)/, $(C_SRC:.c=.o) $(CXX_SRC:.cc=.o)) $(GTEST_OBJS)

CFLAGS                  += -I$(GTEST_ROOT)/googletest
CFLAGS                  += -I$(GTEST_ROOT)/googletest/include
#CFLAGS                  += -I$(GTEST_ROOT)/googlemock
CFLAGS                  += -I$(GTEST_ROOT)/googlemock/include
CFLAGS                  += -I$(GTEST_ROOT)
CFLAGS                  += -g3
CFLAGS                  += -Wall

#CXXFLAGS                += -isystem
CXXFLAGS                += -std=c++11
CXXFLAGS                += $(CFLAGS)

DIRS                    += $(BUILD) $(BUILD)/deps
DIRS                    += $(sort $(dir $(OBJECTS)))

VPATH                   = $(GTEST_ROOT)

default : run

help :
	@echo "The following targets are available:"
	@echo "  make run               -- compile and run tests"
	@echo "  make a.out             -- compile and link executable"
	@echo "  make clean             -- nukes build products"
	@echo "  make info              -- stuff for debugging the Makefile"

clean :
	@rm -rf $(BUILD)

info :
	@echo USER: $(USER)
	@echo OBJECTS: $(OBJECTS)
	@echo C_SRC: $(C_SRC)
	@echo CXX_SRC: $(CXX_SRC)
	@echo DIRS : $(DIRS)
	@echo GTEST_ROOT : $(GTEST_ROOT)
	@echo GTEST_SRC : $(GTEST_SRC)
	@echo GTEST_OBJS : $(GTEST_OBJS)

a.out : $(BUILD)/a.out

run : $(BUILD)/a.out | $(DIRS)
	@$(BUILD)/a.out

dirs : $(DIRS)

foo :
	echo foo

$(DIRS) :
	@echo Creating $(@)
	@mkdir -p $(@)

$(OBJECTS) : | $(DIRS)

#$(GTEST_LIB) : $(GTEST_OBJS)
#	@$(AR) $(ARFLAGS) $(@) $(^)

$(BUILD)/a.out : $(OBJECTS) $(GTEST_OBJECTS) $(MAKEFILE_LIST) | $(DIRS)
	@echo Linking $(@)
	@$(CXX) \
                $(LDFLAGS) \
                -o $(@) $(OBJECTS) $(GTEST_OBJECTS)

$(OBJ)/%.o : %.c
	@echo Compiling $(<F)
	@$(CC) $(CFLAGS) -c $< -o $(@) -MD -MF $(BUILD)/deps/$(notdir $*.d)

$(OBJ)/%.o : %.cc
	@echo Compiling $(<F)
	@$(CXX) $(CXXFLAGS) -c $< -o $(@) -MD -MF $(BUILD)/deps/$(notdir $*.d)

$(OBJ)/%.o : $(GTEST_ROOT)/%.cc
	@echo Compiling $(<F)
	@$(CXX) $(CXXFLAGS) -c $< -o $(@) -MD -MF $(BUILD)/deps/$(notdir $*.d)

$(OBJ)/%.E : %.c
	@$(CC) $(CFLAGS) -E -c $< -o $(@)

$(OBJ)/%.E : %.cc
	@$(CXX) $(CXXFLAGS) -E -c $< -o $(@)

$(OBJ)/%.o : %.s
	@echo Assembling $(<F)
	@$(AS) $(ASFLAGS) $< -o $(@)

.PHONY : clean info default run help

old_default :
	c++ -std=c++11 -g main.cc
	./a.out

# Pull in all dependency files
-include $(wildcard $(BUILD)/deps/*.d)
