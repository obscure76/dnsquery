CC := g++
CXXFLAGS := -I/usr/include/mysql -I/usr/include/mysql++ -I/usr/include/ldns -std=c++11
LDFLAGS := -L/usr/lib
LDLIBS := -lmysqlpp -lmysqlclient -lboost_system -lboost_thread -lldns 
EXECUTABLE := main
all: dnsquery

clean: rm -f $(EXECUTABLE) *.o 
