#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <cstdio>
#include <unistd.h>

// Function prototypes
int create_pipe(int pipe_ends[2]);
int write_pipe(int fd, const void *buf, size_t count);
int read_pipe(int fd, void *buf, size_t count);

#endif
