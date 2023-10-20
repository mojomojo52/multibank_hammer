SDIR=src
IDIR=-I$(SDIR)/include -I$(SDIR)/evsets  
LDIR=lib
BUILD=obj
ODIR=src/.obj
DATA_DIR=$(PWD)/data/

CFLAGS=-g $(IDIR) -msse4.2 -ggdb -DDATA_DIR=\"$(DATA_DIR)\"
# CXX=g++
#LDFLAGS= -L $(SDIR)/evsets -levsets

OUT=tester

LDEPS=

GB_PAGE=/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
HUGEPAGE=/mnt/huge

all: $(OUT)
.PHONY: clean

SOURCES := $(wildcard $(SDIR)/*.c)
OBJECTS := $(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(SOURCES))
SOURCES += $(wildcard $(SDIR)/evsets/*.c)
OBJECTS += $(patsubst $(SDIR)/evsets/%.c, $(ODIR)/%.o, $(wildcard $(SDIR)/evsets/*.c))

$(ODIR)/%.o: $(SDIR)/%.c 
	mkdir -p $(ODIR)
	$(CXX) -o $@ -c $< $(CFLAGS) $(LDFLAGS) $(LDEPS)

$(ODIR)/%.o: $(SDIR)/evsets/%.c 
	mkdir -p $(ODIR)
	$(CXX) -o $@ -c $< $(CFLAGS) $(LDFLAGS) $(LDEPS)

$(OUT): $(OBJECTS)
	mkdir -p $(BUILD)
	$(CXX) -o $(BUILD)/$@ $^ $(CFLAGS) $(LDFLAGS) $(LDEPS)
	chmod +x $(BUILD)/$@

clean:
	rm -rf $(BUILD)
	rm -rf $(ODIR)

setup:
	echo "Mounting hugetlbfs"
	echo 2 | sudo tee -a $(GB_PAGE)
	@if ! [ -d $(HUGEPAGE) ]; then\
		sudo mkdir $(HUGEPAGE);\
		sudo mount -t hugetlbfs  -o pagesize=1G none $(HUGEPAGE);\
		sudo chown pit:pit $(HUGEPAGE);\
	fi


teardown:
	@if [ -d $(HUGEPAGE) ]; then\
		sudo umount -f $(HUGEPAGE);\
		sudo rm -r $(HUGEPAGE);\
	fi
	echo 0 | sudo tee -a $(GB_PAGE)

run:
	sudo $(BUILD)/$(OUT)
