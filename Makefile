#
# trivial makefile for ucontainer
#

ucontainer: ucontainer.c
	gcc -o $@ $<

install: ucontainer
	chown root:docker ucontainer
	chmod 4755 ucontainer
	mv ucontainer /usr/local/bin

