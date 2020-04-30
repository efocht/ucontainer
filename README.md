# ucontainer

Little suid root program that allows a normal user who has no privileges to run docker directly
to run commands inside a docker container.

This scenario is for controlled environments where admins prepare docker containers which are accessible through a local registry. Home directories and other remotely mounted directories need to be available for the users from within the container, therefore they are passed with the `-v` option to the docker command.

Remote directories that need to be mounted must be configured in /etc/containers/ucontainer.conf, one directory in each line. At most 32 directories are currently supported. Entries can be commented by a `#` in the first column.

A trivial Makefile can be used to build and install into /usr/local/bin. It must be called as **root** because the suid root bit and proper file ownership must be set.

## Usage
```
$ ucontainer -h
Run a command in a container on behalf of a normal user.
The container bind-mounts a set of external directories.

Usage:
	ucontainer [-i|--interactive] <image_name> [cmd ...]
```

## Example
```
$ ucontainer centos:7.7.1908 ./test
Hello world!
exited, status=0
```


