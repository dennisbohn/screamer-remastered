/*
 * os_begin.c / os_end.c -- where the program's own statics begin and end.  The build puts these
 * two first and last among the program's files, and the linker lays out each file's data and
 * zero-filled statics in that order: what lies between the marks is the port's own, what lies
 * outside is the libraries' (SDL's, the C library's).  port.c's state save and restore between
 * the parts of the program take the port's alone, so SDL can be linked into the program.
 */
char os_data_begin[8] = { 1 };       /* (not zero: .data) */
char os_bss_begin[8];                /* (zero: .bss) */
