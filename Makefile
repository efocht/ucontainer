#
# trivial makefile for ucontainer
#

ifneq ("$(wildcard /usr/bin/docker-current)","")
DOCKERBIN := /usr/bin/docker-current
else
DOCKERBIN := /usr/bin/docker
endif


ucontainer: ucontainer.c
	g++ -DDOCKER_BIN=\"$(DOCKERBIN)\" -o $@ $<

install: ucontainer
	chown root:docker ucontainer
	chmod 4755 ucontainer
	mv ucontainer /usr/local/bin

