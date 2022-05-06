#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <quapi/quapi.h>

#include "parse.h"
#include "utilities.h"

bool option_verbose = false;

static quapi_solver* solver = NULL;
static int varcount = 0;
static int clausecount = 0;

static void
print_ydbg(const char* fmt, va_list* ap) {
  fputs("[QUAPIFY] ", stderr);
  vfprintf(stderr, fmt, *ap);
  fputc('\n', stderr);
  fflush(stderr);
}

static void
ydbg(const char* fmt, ...) {
  if(!option_verbose)
    return;
  va_list ap;
  va_start(ap, fmt);
  print_ydbg(fmt, &ap);
  va_end(ap);
}

void
add_quantifier(int lit) {
  if(!solver) {
    return;
  } else {
    quapi_quantify(solver, lit);
  }
}

void
add_lit(int lit) {
  if(!solver) {
    if(lit == 0) {
      ++clausecount;
    } else {
      int abslit = ABS(lit);
      if(abslit > varcount)
        varcount = abslit;
    }
  } else {
    quapi_add(solver, lit);
  }
}

static void
help() {
  fprintf(stderr,
          "quapify - Apply assumptions to (non-assuming) SAT or QBF solvers\n");
  fprintf(stderr,
          "  (c) 2022 Maximilian Heisinger <maximilian.heisinger@jku.at> "
          "<mail@maxheisinger.at>,\n");
  fprintf(stderr, "           Martina Seidl <martina.seidl@jku.at>,\n");
  fprintf(stderr, "           Armin Biere <biere@gmail.com>\n");
  fprintf(stderr,
          "  Uses the Kissat DIMACS parser (MIT licensed), extended to QBF.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "USAGE:\n");
  fprintf(stderr,
          "  quapify <input formula> [OPTIONS] -- solver [solver args]:\n");
  fprintf(stderr, "OPTIONS:\n");
  fprintf(stderr, "  -p\t\tprint whole assumption, not just its index\n");
  fprintf(stderr, "  -v\t\tverbose output\n");
  fprintf(stderr, "  -r\t\tset parsing to relaxed (default is normal)\n");
  fprintf(stderr,
          "  -s\t\tset parsing to strict/pedantic (default is normal)\n");
  fprintf(stderr, "  -a <int>+\tadd an explicit assumption to be computed\n");
  fprintf(stderr, "OUTPUT FORMAT:\n");
  fprintf(stderr,
          "  Space separated fields: SOLVERSTATUS SOLVETIME[s] ASSUMPTION\n");
  fprintf(stderr, "EXAMPLES:\n");
  fprintf(stderr, "  ./quapify input.cnf -a 1 -a -1 -- ./solver --cnf\n");
  fprintf(stderr, "  ./quapify input.cnf -a 1 0 -1 0 -- ./solver --cnf\n");
}

struct config {
  size_t assumptions_count;
  size_t assumptions_max_assumption_size;
  size_t assumptions_size;
  size_t assumptions_capacity;
  int* assumptions;

  bool print_assumptions;

  strictness strictness;

  const char* input;
  const char* solver;
  char** solver_argv;
};

static inline void
add_to_assumptions(struct config* cfg, int assumption) {
  static size_t current_assumption_size = 0;
  if(assumption == 0) {
    if(current_assumption_size > cfg->assumptions_max_assumption_size) {
      cfg->assumptions_max_assumption_size = current_assumption_size;
    }
    ++cfg->assumptions_count;
    current_assumption_size = 0;
  } else {
    ++current_assumption_size;
  }

  if(cfg->assumptions_size == cfg->assumptions_capacity) {
    if(cfg->assumptions_capacity == 0) {
      cfg->assumptions_capacity = 16;
    } else {
      cfg->assumptions_capacity *= 2;
    }
    cfg->assumptions =
      realloc(cfg->assumptions, sizeof(int) * cfg->assumptions_capacity);
  }
  cfg->assumptions[cfg->assumptions_size++] = assumption;
}

static int*
print_assumption(FILE* f, int* assumption) {
  while(*assumption != 0)
    fprintf(f, "%d ", *(assumption++));
  return assumption + 1;
}

static void
print_assumptions(FILE* f, int* assumptions, size_t length) {
  int* end = assumptions + length;
  while(assumptions != end) {
    assumptions = print_assumption(f, assumptions);
    fprintf(f, "\n");
  }
}

static struct config
parse_cli(int argc, char* argv[]) {
  struct config cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.strictness = NORMAL_PARSING;

  int c;

  if(argc >= 2) {
    if(strcmp(argv[1], "-h") == 0) {
      help();
      exit(EXIT_SUCCESS);
    }

    cfg.input = argv[1];
    ++argv;
    --argc;
  }

  while((c = getopt(argc, argv, "rsvph:a:")) != -1)
    switch(c) {
      case 'a': {
        optind--;
        int l;
        for(; optind < argc && !isalpha(argv[optind][0]) &&
              (*argv[optind] != '-' ||
               (argv[optind][1] >= '0' && argv[optind][1] <= '9'));
            optind++) {
          l = atoi(argv[optind]);
          add_to_assumptions(&cfg, l);
        }
        if(l != 0) {
          add_to_assumptions(&cfg, 0);
        }
        break;
      }
      case 'h':
        help();
        exit(EXIT_SUCCESS);
      case 'v':
        option_verbose = true;
        break;
      case 'p':
        cfg.print_assumptions = true;
        break;
      case 'r':
        cfg.strictness = RELAXED_PARSING;
        break;
      case 's':
        cfg.strictness = PEDANTIC_PARSING;
        break;
      case '?':
        if(optopt == 'a')
          fprintf(stderr, "Option -%c requires argument.\n", optopt);
        else if(isprint(optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        exit(EXIT_SUCCESS);
      default:
        exit(EXIT_FAILURE);
    }

  if(cfg.assumptions_count == 0) {
    fprintf(stderr, "Need to supply at least one assumption with `-a`!\n");
    exit(EXIT_FAILURE);
  }

  if(optind >= argc) {
    fprintf(stderr, "Require -- <solver> [solver arguments]\n");
    exit(EXIT_FAILURE);
  }

  cfg.solver = argv[optind];
  cfg.solver_argv = &argv[optind + 1];

  return cfg;
}

static bool
run_kissat_parser(struct config* cfg) {
  file f;

  if(strcmp(cfg->input, "-") == 0) {
    kissat_read_already_open_file(&f, stdin, "/dev/stdin");
  } else {
    if(!kissat_open_to_read_file(&f, cfg->input)) {
      fprintf(
        stderr, "Error: Kissat reader could not open file \"%s\"!", cfg->input);
      return false;
    }
  }

  uint64_t lineno = 0;
  int max_var;
  const char* result =
    kissat_parse_dimacs(cfg->strictness, &f, &lineno, &max_var);

  if(result) {
    fprintf(stderr, "Error: Kissat reader returned message: %s", result);
    return false;
  }

  return true;
}

static double
tai_time(void) {
  double res = -1;
  struct timespec ts;
  if(!clock_gettime(CLOCK_TAI, &ts)) {
    res = 1e-9 * ts.tv_nsec;
    res += ts.tv_sec;
  }
  return res;
}

int
main(int argc, char* argv[]) {
  struct config cfg = parse_cli(argc, argv);

  if(option_verbose) {
    if(cfg.assumptions_size) {
      ydbg("Assumptions:");
      print_assumptions(stderr, cfg.assumptions, cfg.assumptions_size);
    }

    ydbg("Max Assumption Length: %zu", cfg.assumptions_max_assumption_size);
    ydbg("Input: \"%s\"", cfg.input);
    ydbg("Solver: \"%s\"", cfg.solver);
  }

  if(!run_kissat_parser(&cfg))
    return EXIT_FAILURE;

  solver = quapi_init(cfg.solver,
                      (const char**)cfg.solver_argv,
                      NULL,
                      varcount,
                      clausecount,
                      cfg.assumptions_max_assumption_size,
                      NULL,
                      NULL);

  // Run through once. Both to check if the file is okay and to only get
  // information at the beginning. We don't want to save the whole formula to
  // RAM in Quapify.

  if(!run_kissat_parser(&cfg))
    return EXIT_FAILURE;

  int* end = cfg.assumptions + cfg.assumptions_size;
  int *ass = cfg.assumptions, *assstart = cfg.assumptions;
  int assumption = 0;
  while(ass != end) {
    if(*ass != 0) {
      quapi_assume(solver, *ass);
    } else {
      double before_time = tai_time();
      int result = quapi_solve(solver);
      double after_time = tai_time();

      printf("%d %f ", result, after_time - before_time);

      if(cfg.print_assumptions)
        print_assumption(stdout, assstart);
      else
        printf("%d ", assumption++);

      printf("\n");

      assstart = ass + 1;
    }
    ++ass;
  }

  free(cfg.assumptions);
}
