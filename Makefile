CC= g++ -m32
CCDBG= g++ -g -m32
CFLAGS=-Wall -Wextra -W -I.. -O2 -I/home/slapware/chilkat-9.3.2-x86-linux-gcc/include -pthread -pedantic -Wwrite-strings -Wno-long-long -c 
LFLAGS=-pthread 
RWLIBS=-lchilkat-9.3.2 -lmysqlcppconn -lresolv -lboost_system -lboost_filesystem -lboost_date_time -lboost_regex -lpthread -lssl -lcrypto -lnsl
SOURCES=epubStore.cpp Markup.cpp
OBJS=epubStore.o Markup.o
# slap check
COMPILEFLAGS= -Wwrite-strings -O2 -pthread --pedantic -Wall -W -Wno-long-long -c
LINKER= g++
LINKFLAGS= -pthread 
LINKLIBS= -lnsl 

all: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) $(RWLIBS) -o epubStore

debug: $(OBJS)
	$(CCDBG) $(LFLAGS) $(OBJS) $(RWLIBS) -o epubStoreDbg

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(RWFLAGS) $(OBJECTS) -o $@

epubStore.o:
	$(CC) $(CFLAGS) epubStore.cpp

Markup.o:
	$(CC) $(CFLAGS) Markup.cpp

# Remove all objects and other temporary files.
clean:
	rm -rf *.o *.gch *.ii *.ti 

# Remove all objects and other temporary files.
realclean:
	rm -rf *.o *.gch *.ii *.ti epubStore epubStoreDbg

tar:
	tar cfv epubStore.tar epubStore.cpp conReport Makefile

