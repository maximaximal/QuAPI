#include <ctype.h>
#include <errno.h>
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
static size_t varcount = 0;
static size_t clausecount = 0;

static int* quantifiers = NULL;
static size_t quantifiers_size = 0;
static size_t quantifiers_capacity = 0;

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
    if(quantifiers_size == quantifiers_capacity) {
      if(quantifiers_capacity == 0) {
        quantifiers_capacity = 8;
      } else {
        quantifiers_capacity *= 2;
      }

      quantifiers = realloc(quantifiers, quantifiers_capacity * sizeof(int));
    }

    quantifiers[quantifiers_size++] = lit;
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
  fprintf(stderr, "  -d\t\tenable QuAPI debug using QUAPI_DEBUG envvar\n");
  fprintf(stderr, "  -t\t\tenable QuAPI trace using QUAPI_TRACE envvar\n");
  fprintf(stderr, "  -r\t\tset parsing to relaxed (default is normal)\n");
  fprintf(stderr,
          "  -s\t\tset parsing to strict/pedantic (default is normal)\n");
  fprintf(stderr, "  -a <int>+\tadd an explicit assumption to be computed\n");
  fprintf(stderr,
          "  -i <int>\tadd an integer-split-based collection of assumptions\n");
  fprintf(stderr, "  -I <int>\tselect only the I'th assumption to be solved\n");
  fprintf(stderr, "  -S\t\tstringify results (to SAT, UNSAT or UNKNOWN)\n");
  fprintf(stderr, "  -H\t\tprint header for output table\n");
  fprintf(stderr, "  -W\t\twrap assumption in \"\"\n");
  fprintf(stderr,
          "  -g\t\tjust generate a list of assumptions without solving\n");
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

  size_t intsplits_highest_intsplit;
  size_t intsplits_size;
  size_t intsplits_capacity;
  int* intsplits;

  bool print_assumptions;
  bool stringify_result;
  bool print_header;
  bool wrap_assumption;
  bool generate_assumption_list;
  int selected_assumption;

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

static inline void
add_intsplit_nesting_level(struct config* cfg, int intsplit) {
  if(intsplit <= 0) {
    err("Intsplits need to be > 0! Ignoring the split %d.", intsplit);
    return;
  }

  if(intsplit > cfg->intsplits_highest_intsplit)
    cfg->intsplits_highest_intsplit = intsplit;

  if(cfg->intsplits_size == cfg->intsplits_capacity) {
    if(cfg->intsplits_capacity == 0) {
      cfg->intsplits_capacity = 8;
    } else {
      cfg->intsplits_capacity *= 2;
    }
    cfg->intsplits =
      realloc(cfg->intsplits, cfg->intsplits_capacity * sizeof(int));
  }

  cfg->intsplits[cfg->intsplits_size++] = intsplit;
}

// https://stackoverflow.com/a/15327567
static inline int
ceil_log2(unsigned long long x) {
  static const unsigned long long t[6] = {
    0xFFFFFFFF00000000ull, 0x00000000FFFF0000ull, 0x000000000000FF00ull,
    0x00000000000000F0ull, 0x000000000000000Cull, 0x0000000000000002ull
  };

  int y = (((x & (x - 1)) == 0) ? 0 : 1);
  int j = 32;
  int i;

  for(i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
    y += k;
    x >>= k;
    j >>= 1;
  }

  return y;
}

unsigned char
get_bit(unsigned int n, unsigned int b) {
  if(b < 0 || b >= sizeof(int) * CHAR_BIT)
    return -1;
  return (n >> b) & 1;
};

static size_t
intsplits_current_length(struct config* cfg, size_t index) {
  assert(index < cfg->intsplits_size);
  int intSplit = cfg->intsplits[index];
  if(intSplit != 0) {
    return ceil_log2(intSplit);
  }
  return 1;
}

static size_t
intsplits_summed_length(struct config* cfg, size_t index) {
  size_t l = 0;
  for(size_t i = 0; i < index; ++i) {
    l += intsplits_current_length(cfg, i);
  }
  return l;
}

static bool
intsplit_state_to_assumption(struct config* cfg,
                             int* state,
                             size_t state_size) {
  const size_t l = intsplits_summed_length(cfg, state_size);
  if(l > quantifiers_size) {
    fprintf(stderr,
            "Error: Intsplits too big! Length of %zu surpasses quantifier "
            "count %zu!\n",
            l,
            quantifiers_size);
    return false;
  }

  int assumption[l];
  memcpy(assumption, quantifiers, l * sizeof(int));

  // Check for validity of intsplit (not over multiple different quantifiers!),
  // convert to absolutes and set the sign to the correct bit for the intsplit.
  size_t assumption_i = 0;
  for(size_t i = 0; i < state_size; ++i) {
    size_t cl = intsplits_current_length(cfg, i);
    for(size_t bit = cl - 1, li = 0; li < cl; ++assumption_i, --bit, ++li) {
      if(li < cl - 1 &&
         (quantifiers[assumption_i] ^ quantifiers[assumption_i + 1]) < 0) {
        fprintf(stderr,
                "Error: Intsplit at index %zu (%d) spanning multiple different "
                "quantifier "
                "blocks! (sign change from quantifier %zu (%d) to %zu (%d)).\n",
                i,
                cfg->intsplits[i],
                assumption_i,
                quantifiers[assumption_i],
                assumption_i + 1,
                quantifiers[assumption_i + 1]);
        return false;
      }

      if(get_bit(state[i], bit) == 0)
        assumption[assumption_i] = -abs(assumption[assumption_i]);
      else
        assumption[assumption_i] = abs(assumption[assumption_i]);
    }
  }

  for(size_t i = 0; i < l; ++i) {
    add_to_assumptions(cfg, assumption[i]);
  }
  add_to_assumptions(cfg, 0);

  return true;
}

static bool
inflate_intsplits(struct config* cfg) {
  assert(quantifiers);
  assert(cfg->intsplits);

  // This instplit-implementation is inspired from Paracooba and tries to
  // reinterpret the first few quantifiers as integers. This is very nice for
  // game-playing and planning.

  // What this means in practice:
  // Let's have alternating prefix: ForAll 1 2, Exists 3 4, ...
  // Intsplit -i 3 -i 3 would generate these assumptions:
  // -1 -2 -3 -4   i.e. 0 0
  // -1 -2 -3 4    i.e. 0 1
  // -1 -2 3 -4    i.e. 0 2
  // -1 2 -3 -4    i.e. 1 0
  // ...

  int intsplits_state[cfg->intsplits_size];
  memset(intsplits_state, 0, sizeof(intsplits_state));
  int last = cfg->intsplits_size - 1;

  while(intsplits_state[0] <= cfg->intsplits[0] - 1) {
    if(!intsplit_state_to_assumption(cfg, intsplits_state, cfg->intsplits_size))
      return false;

    ++intsplits_state[last];
    for(int d = last;
        intsplits_state[d] == cfg->intsplits[d] + (d == 0 ? 1 : 0);
        --d) {
      intsplits_state[d] = 0;
      if(d > 0) {
        ++intsplits_state[d - 1];
      }
    }
  }

  return true;
}

static int*
print_assumption(FILE* f, int* assumption) {
  bool first = true;
  while(*assumption != 0) {
    if(first)
      first = false;
    else
      fprintf(f, " ");

    fprintf(f, "%d", *(assumption++));
  }
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
  cfg.selected_assumption = -1;

  cfg.strictness = NORMAL_PARSING;

  int c;

  if(argc >= 2) {
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
       strcmp(argv[1], "-?") == 0) {
      help();
      exit(EXIT_SUCCESS);
    }

    cfg.input = argv[1];
    ++argv;
    --argc;

    if(!kissat_file_exists(cfg.input)) {
      fprintf(
        stderr,
        "File \"%s\" does not exist! First parameter must be input file.\n",
        cfg.input);
      exit(EXIT_FAILURE);
    }
  }

  while((c = getopt(argc, argv, "dtrgsSHWvph:a:i:I:")) != -1)
    switch(c) {
      case 'a': {
        if(strcmp(optarg, "--") == 0) {
          fprintf(stderr,
                  "Argument to -a was \"--\", seems like you forgot to add an "
                  "assumption!\n");
          exit(EXIT_FAILURE);
        }

        optind--;
        int l;
        for(; optind < argc && !isalpha(argv[optind][0]) &&
              (*argv[optind] != '-' ||
               (argv[optind][1] >= '0' && argv[optind][1] <= '9'));
            optind++) {
          char* value = argv[optind];
          char* endptr = NULL;
          l = strtol(value, &endptr, 10);
          if(value == endptr) {
            fprintf(
              stderr,
              "Invalid number encountered for assumption! Value received: %s\n",
              argv[optind]);
            exit(EXIT_FAILURE);
          }
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
      case 'd':
        setenv("QUAPI_DEBUG", "1", 1);
        break;
      case 't':
        setenv("QUAPI_TRACE", "1", 1);
        break;
      case 'p':
        cfg.print_assumptions = true;
        break;
      case 'g':
        cfg.generate_assumption_list = true;
        break;
      case 'r':
        cfg.strictness = RELAXED_PARSING;
        break;
      case 's':
        cfg.strictness = PEDANTIC_PARSING;
        break;
      case 'S':
        cfg.stringify_result = true;
        break;
      case 'H':
        cfg.print_header = true;
        break;
      case 'W':
        cfg.wrap_assumption = true;
        break;
      case 'i':
        add_intsplit_nesting_level(&cfg, atoi(optarg));
        break;
      case 'I':
        cfg.selected_assumption = atoi(optarg);
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

  if(cfg.assumptions_count == 0 && cfg.intsplits_size == 0) {
    add_to_assumptions(&cfg, 0);
  }

  if(!cfg.generate_assumption_list && optind >= argc) {
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

  // Run through once. Both to check if the file is okay and to only get
  // information at the beginning. We don't want to save the whole formula to
  // RAM in Quapify.

  if(!run_kissat_parser(&cfg))
    return EXIT_FAILURE;

  if(cfg.intsplits_size > 0) {
    if(quantifiers) {
      if(!inflate_intsplits(&cfg)) {
        return EXIT_FAILURE;
      }
    } else {
      fprintf(stderr,
              "Formula \"%s\" does not have quantifiers, but needing "
              "quantifiers for intsplits!\n",
              cfg.input);
      return EXIT_FAILURE;
    }
  }

  if(cfg.generate_assumption_list) {
    print_assumptions(stdout, cfg.assumptions, cfg.assumptions_size);
    return EXIT_SUCCESS;
  }

  if(option_verbose) {
    if(cfg.assumptions_size) {
      ydbg("Assumptions:");
      print_assumptions(stderr, cfg.assumptions, cfg.assumptions_size);
    }

    ydbg("Max Assumption Length: %zu", cfg.assumptions_max_assumption_size);
    ydbg("Input: \"%s\"", cfg.input);
    ydbg("Solver: \"%s\"", cfg.solver);

    if(cfg.solver_argv) {
      for(char** a = cfg.solver_argv; *a != NULL; ++a) {
        ydbg("Solver Arg: \"%s\"", *a);
      }
    }
  }

  solver = quapi_init(cfg.solver,
                      (const char**)cfg.solver_argv,
                      NULL,
                      varcount,
                      clausecount,
                      cfg.assumptions_max_assumption_size,
                      NULL,
                      NULL);

  if(!solver) {
    fprintf(stderr, "Quapi solver could not be initialized!\n");
    return EXIT_FAILURE;
  }

  ydbg("Initialized quapi, parsing formula again.");

  if(!run_kissat_parser(&cfg))
    goto ERROR;

  int* end = cfg.assumptions + cfg.assumptions_size;
  int *ass = cfg.assumptions, *assstart = cfg.assumptions;

  if(cfg.print_header) {
    printf("SolveTime[ns] SolveTime[s] Result Assumption\n");
  }

  int assumption_id = 0;

  while(ass != end) {
    if(*ass != 0) {
      if(cfg.selected_assumption == -1 ||
         cfg.selected_assumption >= 0 &&
           assumption_id == cfg.selected_assumption) {
        if(ABS(*ass) > varcount) {
          fprintf(
            stderr,
            "Cannot assume %d, as it is larger than the variable count %zu!\n",
            *ass,
            varcount);
          goto ERROR;
        }
        if(!quapi_assume(solver, *ass)) {
          fprintf(stderr, "quapi_assume(solver, %d) returned false!\n", *ass);
          goto ERROR;
        }
      }
    } else {
      if(cfg.selected_assumption == -1 ||
         cfg.selected_assumption >= 0 &&
           assumption_id == cfg.selected_assumption) {
        double before_time = tai_time();
        int result = quapi_solve(solver);
        double after_time = tai_time();

        printf("%zu %f ",
               (size_t)((after_time - before_time) * 1000000000),
               after_time - before_time);

        if(cfg.stringify_result) {
          const char* result_str = "UNKNOWN";
          switch(result) {
            case 10:
              result_str = "SAT";
              break;
            case 20:
              result_str = "UNSAT";
              break;
            default:
              result_str = "UNKNOWN";
              break;
          }
          printf("%s ", result_str);
        } else {
          printf("%d ", result);
        }

        if(cfg.wrap_assumption)
          printf("\"");

        printf("%d ", assumption_id);

        if(cfg.print_assumptions) {
          print_assumption(stdout, assstart);
        }

        if(cfg.wrap_assumption)
          printf("\"");

        printf("\n");
      }

      assstart = ass + 1;
      ++assumption_id;
    }
    ++ass;
  }

  free(cfg.assumptions);
  return EXIT_SUCCESS;
ERROR:
  free(cfg.assumptions);
  return EXIT_FAILURE;
}
