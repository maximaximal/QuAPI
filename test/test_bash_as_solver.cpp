#include "catch.hpp"
#include "util.hpp"

#include <quapi/quapi.h>

#include <filesystem>
#include <fstream>
#include <sstream>

/* This test is very interesting, as it allows better debugging into the
 * printing process.
 */
TEST_CASE("bash read line as solver", "[.]") {
  const char* argv[] = {
    "bash",
    "-c",
    "while IFS='$\n' read -r line; do echo \"$line\" >&2; done",
    NULL
  };

  QuAPISolver s(quapi_init("bash", argv, NULL, 2, 2, 1, NULL, NULL));
  REQUIRE(s.get());

  quapi_quantify(s.get(), 1);
  quapi_quantify(s.get(), -2);

  quapi_add(s.get(), 1);
  quapi_add(s.get(), 0);

  quapi_add(s.get(), 1);
  quapi_add(s.get(), 2);
  quapi_add(s.get(), 0);

  quapi_assume(s.get(), 1);

  quapi_solve(s.get());
}

#define FILEPATH "_bash_as_solver_with_file_redirect.txt"

TEST_CASE("bash as solver with file redirect") {
  const auto& f = GENERATE(FillerAndExpected(
                             "1",
                             [](quapi_solver* s) {
                               quapi_quantify(s, 1);
                               quapi_quantify(s, -2);

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 0);

                               quapi_assume(s, -1);
                             },
                             2,
                             1,
                             1,
                             R""""(p cnf 2 2
e 1 0
a 2 0
1 2 0
-1 0
)""""),
                           FillerAndExpected(
                             "More alternations > 100",
                             [](quapi_solver* s) {
                               for(int i = 1; i <= 100; ++i) {
                                 quapi_quantify(s, i);
                               }
                               for(int i = 101; i <= 200; ++i) {
                                 quapi_quantify(s, -i);
                               }
                               for(int i = 201; i <= 300; ++i) {
                                 quapi_quantify(s, i);
                               }

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 0);

                               quapi_assume(s, -1);
                             },
                             300,
                             1,
                             1,
                             R""""(p cnf 300 2
e 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 99 100 0
a 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143 144 145 146 147 148 149 150 151 152 153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 171 172 173 174 175 176 177 178 179 180 181 182 183 184 185 186 187 188 189 190 191 192 193 194 195 196 197 198 199 200 0
e 201 202 203 204 205 206 207 208 209 210 211 212 213 214 215 216 217 218 219 220 221 222 223 224 225 226 227 228 229 230 231 232 233 234 235 236 237 238 239 240 241 242 243 244 245 246 247 248 249 250 251 252 253 254 255 256 257 258 259 260 261 262 263 264 265 266 267 268 269 270 271 272 273 274 275 276 277 278 279 280 281 282 283 284 285 286 287 288 289 290 291 292 293 294 295 296 297 298 299 300 0
1 2 0
-1 0
)""""),
                           FillerAndExpected(
                             "2",
                             [](quapi_solver* s) {
                               quapi_quantify(s, 1);
                               quapi_quantify(s, 2);

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 3);
                               quapi_add(s, 0);

                               quapi_assume(s, 1);
                             },
                             3,
                             1,
                             1,
                             R""""(p cnf 3 2
e 1 2 0
1 2 3 0
1 0
)""""),
                           FillerAndExpected(
                             "3",
                             [](quapi_solver* s) {
                               quapi_quantify(s, 1);
                               quapi_quantify(s, -2);

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 0);

                               quapi_assume(s, -1);
                               quapi_assume(s, 2);
                             },
                             2,
                             1,
                             2,
                             R""""(p cnf 2 3
e 1 2 0
1 2 0
-1 0
2 0
)""""),
                           FillerAndExpected(
                             "One Too Few Assumptions",
                             [](quapi_solver* s) {
                               quapi_quantify(s, 1);
                               quapi_quantify(s, 2);

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 3);
                               quapi_add(s, 0);

                               quapi_assume(s, 1);
                             },
                             3,
                             1,
                             2,
                             R""""(p cnf 3 3
e 1 2 0
1 2 3 0
1 0
1 -1 0
)""""),
                           FillerAndExpected(
                             "More Too Few Assumptions",
                             [](quapi_solver* s) {
                               quapi_quantify(s, 1);
                               quapi_quantify(s, 2);

                               quapi_add(s, 1);
                               quapi_add(s, 2);
                               quapi_add(s, 3);
                               quapi_add(s, 0);

                               quapi_assume(s, 1);
                             },
                             3,
                             1,
                             3,
                             R""""(p cnf 3 4
e 1 2 0
1 2 3 0
1 0
1 -1 0
1 -1 0
)""""),
                           FillerAndExpected(
                             "Zero Literals With Assumptions",
                             [](quapi_solver* s) {},
                             0,
                             0,
                             1,
                             R""""(p cnf 0 1
0
)""""));

  CAPTURE(f.name);

  using namespace std::filesystem;

  if(file_exists(FILEPATH)) {
    remove(FILEPATH);
  }

  const char* argv[] = { "bash",
                         "-c",
                         "while read line; do echo \"$line\" >> " FILEPATH
                         "; done < \"${1:-/dev/stdin}\"",
                         NULL };

  QuAPISolver s(quapi_init(
    "bash", argv, NULL, f.literals, f.clauses, f.prefixdepth, NULL, NULL));
  REQUIRE(s.get());

  f.filler(s.get());

  quapi_solve(s.get());

  std::ifstream t(FILEPATH);
  std::stringstream buffer;
  buffer << t.rdbuf();
  REQUIRE(buffer.str() == f.expected);

  remove(FILEPATH);
}
