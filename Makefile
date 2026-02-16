ROLEFLAG :=
  ROLEFLAG = -DMAXDRONE=3
  ROLEFLAG += -DDRONEID=0
  #ROLEFLAG += -DDRONEID=1
  #ROLEFLAG += -DDRONEID=2
  #ROLEFLAG += -DDRONEID=3

TARGET   = wfb

CFLAGS ?= -Ofast -g
CFLAGS += -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration
CFLAGS += $(ROLEFLAG)

LFLAGS  = -g

SRCDIR   = src
OBJDIR   = obj
BINDIR   = bin

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

$(BINDIR)/$(TARGET): $(OBJECTS)
	gcc $(OBJECTS) $(LFLAGS) -o $@

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	gcc $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(BINDIR)/$(TARGET)
