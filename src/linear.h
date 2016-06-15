#pragma once
#include "fixed.h"
#include <stdio.h>

typedef struct {
	size_t d[2];
	fixed64_t *value;
} matrix_t;

typedef struct {
	size_t len;
	fixed64_t *value;
} vector_t;

typedef struct {
	matrix_t a;
	vector_t b;
	vector_t beta;
	int precision;
	long long gates;
	int num_iterations; // for cgd
	char *port; // from phase 1 config. TODO: find a nicer way to do this
	int num_data_providers;
} linear_system_t;

// helper function that maps indices into a symmetric matrix
// to an index into a one-dimensional array
size_t idx(size_t i, size_t j);

// inner product of two vectors of the same length
fixed64_t inner_product(vector_t *, vector_t *);

// functions to solve LSs
void cholesky(void *);
void ldlt(void *);
void cgd(void *);

// IO helpers
int read_matrix(FILE *, matrix_t *, int);
int read_vector(FILE *, vector_t *, int);
