// The tests use no POSIX strings.h functions. Block the game's text header
// when glibc's string.h includes <strings.h> during a GNU C build.
