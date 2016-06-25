#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sodium.h>
#include <time.h>
#include <czmq.h>
#include <obliv.h>
#include <obliv_common.h>

#include "secure_multiplication/secure_multiplication.pb-c.h"
#include "secure_multiplication/secure_multiplication.h"
#include "secure_multiplication/node.h"
#include "secure_multiplication/config.h"
#include "check_error.h"
#include "linear.h"
#include "secure_multiplication/phase1.h"
#include "input.h"
#include "util.h"
#include "secure_multiplication/node.h"


static int barrier(node *self) {
	// wait until everybody is here
	int flag = 42; // value is arbitrary
	if(self->party != 1) {
		check(orecv(self->peer[self->party-2], 0, &flag, sizeof(flag)) == sizeof(flag),
			"orecv: %s", strerror(errno));
	}
	if(self->party != self->num_parties) {
		check(osend(self->peer[self->party], 0, &flag, sizeof(flag)) == sizeof(flag),
			"osend: %s", strerror(errno));
		check(orecv(self->peer[self->party], 0, &flag, sizeof(flag)) == sizeof(flag),
			"orecv: %s", strerror(errno));
	}
	if(self->party != 1) {
		check(osend(self->peer[self->party-2], 0, &flag, sizeof(flag)) == sizeof(flag),
			"osend: %s", strerror(errno));
		orecv(self->peer[self->party-2],0,NULL,0);
	}
	return 0;

error:
	return -1;
}


int main(int argc, char **argv) {
	uint64_t *share_A = NULL, *share_b = NULL;
	config *c = NULL;
	node *self = NULL;
	int status;

	// parse arguments
	check(argc > 5, "Usage: %s [Input_file] [Precision] [Party] [Algorithm] [Num. iterations CGD]", argv[0]);
	char *end;
	int precision = (int) strtol(argv[2], &end, 10);
	check(!errno, "strtol: %s", strerror(errno));
	check(!*end, "Precision must be a number");
	int party = (int) strtol(argv[3], &end, 10);
	check(!errno, "strtol: %s", strerror(errno));
	check(!*end, "Party must be a number");
	char *algorithm = argv[4];
	check(!strcmp(algorithm, "cholesky") || !strcmp(algorithm, "ldlt")  || !strcmp(algorithm, "cgd"),
	      "Algorithm must be cholesky, ldlt, or cgd.");
	check(strcmp(algorithm, "cgd") || argc == 6, "Number of iterations for CGD must be provided");

	// read ls, we only need number of iterations
	linear_system_t ls;
	if(!strcmp(algorithm, "cgd")){
	       ls.num_iterations = atoi(argv[5]);
	} else {
	       ls.num_iterations = 0;
	}

	// read config
	status = config_new(&c, argv[1]);
	check(!status, "Could not read config");
	c->party = party;

	if(party == 2) {
		printf("{\"n\":\"%zd\", \"d\":\"%zd\" \"p\":\"%d\"}\n", c->n, c->d, c->num_parties - 1);
	}

	status = node_new(&self, c);
	check(!status, "Could not create node");

	if(party == 1) {
		//printf("Party %d running as TI\n", party);
		status = run_trusted_initializer(self, c, precision);
		check(!status, "Error while running trusted initializer");
	} else if(party > 2){
		//printf("Party %d running as DP\n", party);
		status = run_party(self, c, precision, NULL, &share_A, &share_b);
		check(!status, "Error while running party %d", party);
	}

	// wait until everybody has finished
	check(!barrier(self), "Error while waiting for other peers to finish");

	printf("Party %d finished phase 1\n", party);

	// At this point:
	// if party = 1 then I am the CSP
	// if party = 2 then I am the Evaluator
	// if party > 2 then
	//   - I am a data provider
  //   - share_A and share_b are my shares of the equation

	// phase 2 starts here
	if(party < 3){  // CSP and Evaluator
		ProtocolDesc *pd;
		if(party == 1) {
			pd = self->peer[1];
		} else {
			pd = self->peer[0];
		}
		orecv(pd, 0, NULL, 0); // flush
		setCurrentParty(pd, party);
		ls.a.d[0] = ls.a.d[1] = ls.b.len = c->d;
		ls.precision = precision;
		ls.beta.value = ls.a.value = ls.b.value = NULL;
		// Run garbled circuit
		// We'll modify linear.oc so that the inputs are read from a ls if the provided one is not NULL
		// else we'l use dcrRcvdIntArray...
		double time = wallClock();
		if(party == 2) {
		      printf("\n");
		      printf("Algorithm: %s\n", algorithm);
		}
		void (*algorithms[])(void *) = {cholesky, ldlt, cgd};
		int alg_index;
		if(!strcmp(algorithm, "cholesky")) {
	              alg_index = 0;
		} else if (!strcmp(algorithm, "ldlt")) {
	              alg_index = 1;
		} else {
		      alg_index = 2;
		}

		ls.self = self;
		execYaoProtocol(pd, algorithms[alg_index], &ls);

		if(party == 2) {
		  //check(ls.beta.len == d, "Computation error.");
		  printf("Time elapsed: %f\n", wallClock() - time);
		  printf("Number of gates: %d\n", ls.gates);
		  printf("Result: ");
		  for(size_t i = 0; i < ls.beta.len; i++) {
		    printf("%20.15f ", fixed_to_double(ls.beta.value[i], precision));
		  }
		  printf("\n");
		}

		if(party == 2) free(ls.beta.value);

	} else {
		printf("party %d connecting to CSP and Evaluator\n", party);
		DualconS* conn = dcsConnect(self);
		printf("party %d connected successfully to CSP and Evaluator\n", party);
		dcsSendIntArray(conn, share_A, c->d*(c->d + 1)/2);
		dcsSendIntArray(conn, share_b, c->d);
		dcsClose(conn);
	}

	node_destroy(&self);
	config_destroy(&c);
	free(share_A);
	free(share_b);
	return 0;
error:
	config_destroy(&c);
	node_destroy(&self);
	free(share_A);
	free(share_b);
	return 1;
}