#define _GNU_SOURCE // for memmem
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <limits.h>

/* convenient debugging macros */
#define dbgd(x) prt(#x ": %d\n", x),flush()
#define dbgx(x) prt(#x ": %x\n", x),flush()
#define dbgf(x) prt(#x ": %f\n", x),flush()

#define exit2(x) { flush(); exit(x); }

typedef unsigned char u8;

/* span

Think of span as our string type.
A span is two pointers, one to the first char included in the string, and the second to the first char excluded after the string's end.
If these two pointers are equal, the string is empty, but it still points to a location.
So two empty spans are not necessarily the same span, while two empty strings are.
Neither spans nor their contents are immutable.
These two pointers must point into some space that has been allocated somewhere.
Usually the spans are backed by the input buffer or the output buffer or a scratch buffer that we create.
A scratch buffer allocated for a particular phase of processing can be seen as a form of arena allocation.
A common pattern is for(;s.buf<s.end;s.buf++) { ... s.buf[0] ... } or similar.
*/

typedef struct {
  u8 *buf;
  u8 *end;
} span;

#define BUF_SZ (1 << 30)

u8 *input_space; // remains immutable once stdin has been read up to EOF.
u8 *output_space;
u8 *cmp_space;
span out, inp, cmp;
/*
The inp variable is the span which writes into input_space, and then is the immutable copy of stdin for the duration of the process.
The number of bytes of input is len(inp).
*/

int empty(span);
int len(span);

void init_spans(); // init buffers

/*
The output is stored in span out, which points to output_space.
Input processing is generally by reading out of the inp span or subspans of it.
The output spans are mostly written to with prt() and other IO functions.
The cmp_space and cmp span which points to it are used for analysis and model data, both reading and writing.
*/

void prt2cmp();
void prt2std();
void prt(const char *, ...);
void w_char(char);
void wrs(span);
void bksp();
void sp();
void terpri();
void flush();
void flush_err();
void flush_to(char*);
void redir(span);
span reset();
void w_char_esc(char);
void w_char_esc_pad(char);
void w_char_esc_dq(char);
void w_char_esc_sq(char);
void wrs_esc();
void save();
void push(span);
void pop(span*);
void advance1(span*);
void advance(span*,int);
int find_char(span s, char c);
span pop_into_span();
span take_n(int, span*);
span first_n(span, int);
int span_eq(span, span);
int span_cmp(span, span);
span S(char*);
span nullspan();

/* our input statistics on raw bytes */

int counts[256] = {0};

void read_and_count_stdin(); // populate inp and counts[]
int empty(span s) {
  return s.end == s.buf;
}

int len(span s) { return s.end - s.buf; }

u8 in(span s, u8* p) { return s.buf <= p && p < s.end; }

int out_WRITTEN = 0, cmp_WRITTEN = 0;

void init_spans() {
  input_space = malloc(BUF_SZ);
  output_space = malloc(BUF_SZ);
  cmp_space = malloc(BUF_SZ);
  out.buf = output_space;
  out.end = output_space;
  inp.buf = input_space;
  inp.end = input_space + BUF_SZ;
  cmp.buf = cmp_space;
  cmp.end = cmp_space;
}

void bksp() {
  out.end -= 1;
}

void sp() {
  w_char(' ');
}

/* we might have a generic take() which would take from inp */
/* we might have the same kind of redir_i() as we have redir() already, where we redirect input to come from a span and then use standard functions like take() and get rid of these special cases for taking input from streams or spans. */

/* take_n is a mutating function which takes the first n chars of the span into a new span, and also modifies the input span to remove this same prefix.
After a function call such as `span new = take_n(x, s)`, it will be the case that `new` contatenated with `s` is equivalent to `s` before the call.
*/

span take_n(int n, span *io) {
  span ret;
  ret.buf = io->buf;
  ret.end = io->buf + n;
  io->buf += n;
  return ret;
}

/*int span_eq(span a, span b) {
  if (len(a) != len(b)) return 0;
  while (a.buf < a.end) {
    if (*a.buf != *b.buf) return 0;
    a.buf++;
    b.buf++;
  }
  return 1;
}
*/

int span_eq(span s1, span s2) {
  if (len(s1) != len(s2)) return 0;
  for (int i = 0; i < len(s1); ++i) if (s1.buf[i] != s2.buf[i]) return 0;
  return 1;
}

span S(char *s) {
  span ret = {(u8*)s, (u8*)s + strlen(s) };
  return ret;
}

void read_and_count_stdin() {
  int c;
  while ((c = getchar()) != EOF) {
    //if (c == ' ') continue;
    assert(c != 0);
    counts[c]++;
    *inp.buf = c;
    inp.buf++;
    if (len(inp) == BUF_SZ) exit2(1);
  }
  inp.end = inp.buf;
  inp.buf = input_space;
}

span saved_out[16] = {0};
int saved_out_stack = 0;

void redir(span new_out) {
  assert(saved_out_stack < 15);
  saved_out[saved_out_stack++] = out;
  out = new_out;
}

span reset() {
  assert(saved_out_stack);
  span ret = out;
  out = saved_out[--saved_out_stack];
  return ret;
}

// set if debugging some crash
const int ALWAYS_FLUSH = 0;

void swapcmp() { span swap = cmp; cmp = out; out = swap; int swpn = cmp_WRITTEN; cmp_WRITTEN = out_WRITTEN; out_WRITTEN = swpn; }
void prt2cmp() { /*if (out.buf == output_space)*/ swapcmp(); }
void prt2std() { /*if (out.buf == cmp_space)*/ swapcmp(); }

void prt(const char * fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  out.end += vsprintf((char*)out.end, fmt, ap);
  if (out.buf + BUF_SZ < out.end) {
    printf("OUTPUT OVERFLOW (%ld)\n", out.end - output_space);
    exit2(7);
  }
  va_end(ap);
  if (ALWAYS_FLUSH) flush();
}

void terpri() {
  *out.end = '\n';
  out.end++;
  if (ALWAYS_FLUSH) flush();
}

void w_char(char c) {
  *out.end++ = c;
}

void w_char_esc(char c) {
  if (c < 0x20 || c == 127) {
    out.end += sprintf((char*)out.end, "\\%03o", (u8)c);
  } else {
    *out.end++ = c;
  }
}

void w_char_esc_pad(char c) {
  if (c < 0x20 || c == 127) {
    out.end += sprintf((char*)out.end, "\\%03o", (u8)c);
  } else {
    sp();sp();sp();
    *out.end++ = c;
  }
}

void w_char_esc_dq(char c) {
  if (c < 0x20 || c == 127) {
    out.end += sprintf((char*)out.end, "\\%03o", (u8)c);
  } else if (c == '"') {
    *out.end++ = '\\';
    *out.end++ = '"';
  } else if (c == '\\') {
    *out.end++ = '\\';
    *out.end++ = '\\';
  } else {
    *out.end++ = c;
  }
}

void w_char_esc_sq(char c) {
  if (c < 0x20 || c == 127) {
    out.end += sprintf((char*)out.end, "\\%03o", (u8)c);
  } else if (c == '\'') {
    *out.end++ = '\\';
    *out.end++ = '\'';
  } else if (c == '\\') {
    *out.end++ = '\\';
    *out.end++ = '\\';
  } else {
    *out.end++ = c;
  }
}

void wrs(span s) {
  for (u8 *c = s.buf; c < s.end; c++) w_char(*c);
}

void wrs_esc(span s) {
  for (u8 *c = s.buf; c < s.end; c++) w_char_esc(*c);
}

// flush() is used to send our out buffer (written to by prt) to stdout. 
void flush() {
  if (out_WRITTEN < len(out)) {
    printf("%.*s", len(out) - out_WRITTEN, out.buf + out_WRITTEN);
    out_WRITTEN = len(out);
    fflush(stdout);
  }
}

void flush_err() {
  if (out_WRITTEN < len(out)) {
    fprintf(stderr, "%.*s", len(out) - out_WRITTEN, out.buf + out_WRITTEN);
    out_WRITTEN = len(out);
    fflush(stderr);
  }
}

void flush_to(char *fname) {
  int fd = open(fname, O_CREAT | O_WRONLY | O_TRUNC, 0666);
  dprintf(fd, "%*s", len(out) - out_WRITTEN, out.buf + out_WRITTEN);
  //out_WRITTEN = len(out);
  // reset for constant memory usage
  out_WRITTEN = 0;
  out.end = out.buf;
  //fsync(fd);
  close(fd);
}

u8 *save_stack[16] = {0};
int save_count = 0;

void save() {
  push(out);
}

span pop_into_span() {
  span ret;
  ret.buf = save_stack[--save_count];
  ret.end = out.end;
  return ret;
}

void push(span s) {
  save_stack[save_count++] = s.buf;
}

void pop(span *s) {
  s->buf = save_stack[--save_count];
}

void advance1(span *s) {
    if (!empty(*s)) s->buf++;
}

void advance(span *s, int n) {
    if (len(*s) >= n) s->buf += n;
    else s->buf = s->end; // Move to the end if n exceeds span length
}

// new code to copy back
int span_cmp(span s1, span s2) {
  for (;;) {
    if (empty(s1) && !empty(s2)) return 1;
    if (empty(s2) && !empty(s1)) return -1;
    if (empty(s1)) return 0;
    int dif = *(s1.buf++) - *(s2.buf++);
    if (dif) return dif;
  }
}

int contains(span, span);

int contains(span haystack, span needle) {
  /*
  prt("contains() haystack:\n");
  wrs(haystack);terpri();
  prt("needle:\n");
  wrs(needle);terpri();
  */
  if (len(haystack) < len(needle)) {
    return 0; // Needle is longer, so it cannot be contained
  }
  void *result = memmem(haystack.buf, haystack.end - haystack.buf, needle.buf, needle.end - needle.buf);
  return result != NULL ? 1 : 0;
}

span first_n(span s, int n) {
  span ret;
  if (len(s) < n) n = len(s); // Ensure we do not exceed the span's length
  ret.buf = s.buf;
  ret.end = s.buf + n;
  return ret;
}

int find_char(span s, char c) {
  for (int i = 0; i < len(s); ++i) {
    if (s.buf[i] == c) return i;
  }
  return -1; // Character not found
}

span next_line(span*);

/* next_line(span*) shortens the input span and returns the first line as a new span.
The newline is consumed and is not part of either the returned span or the input span after the call.
I.e. the total len of the shortened input and the returned line is one less than the len of the original input.
If there is no newline found, then the entire input is returned.
In this case the input span is mutated such that buf now points to end.
This makes it an empty span and thus a null span in our nomenclature, but it is still an empty span at a particular location.
This convention of empty but localized spans allows us to perform comparisons without needing to handle them differently in the case of an empty span.
*/

span next_line(span *input) {
  if (empty(*input)) return nullspan();
  span line;
  line.buf = input->buf;
  while (input->buf < input->end && *input->buf != '\n') {
    input->buf++;
  }
  line.end = input->buf;
  if (input->buf < input->end) { // If '\n' found, move past it for next call
    input->buf++;
  }
  return line;
}

/*
In consume_prefix(span*,span) we are given a span which is typically something being parsed and another span which is expected to be a prefix of it.
If the prefix is found, we return 1 and modify the span that is being parsed to remove the prefix.
Otherwise we leave that span unmodified and return 0.
Typical use is in an if statement to either identify and consume some prefix and then continue on to handle what follows it, or otherwise to skip the if and continue parsing the unmodified input.
*/

int consume_prefix(span *input, span prefix) {
  if (len(*input) < len(prefix) || !span_eq(first_n(*input, len(prefix)), prefix)) {
    return 0; // Prefix not found or input shorter than prefix
  }
  input->buf += len(prefix); // Remove prefix by advancing the start
  return 1;
}

typedef struct {
  span *s; // array of spans (points into span arena)
  int n;   // length of array
} spans;

#define SPAN_ARENA_STACK 256

span* span_arena;
int span_arenasz;
int span_arena_used;
int span_arena_stack[SPAN_ARENA_STACK];
int span_arena_stack_n;

void span_arena_alloc(int sz) {
  span_arena = malloc(sz * sizeof *span_arena);
  span_arenasz = sz;
  span_arena_used = 0;
  span_arena_stack_n = 0;
}
void span_arena_free() {
  free(span_arena);
}
void span_arena_push() {
  assert(span_arena_stack_n < SPAN_ARENA_STACK);
  span_arena_stack[span_arena_stack_n++] = span_arena_used;
}
void span_arena_pop() {
  assert(0 < span_arena_stack_n);
  span_arena_used = span_arena_stack[--span_arena_stack_n];
}
/* spans_alloc returns a spans which has n equal to the number passed in.
Typically the caller will either fill the number requested exactly, or will shorten n if fewer are used.
*/
spans spans_alloc(int n) {
  assert(span_arena);
  spans ret = {0};
  ret.s = span_arena + span_arena_used;
  ret.n = n;
  span_arena_used += n;
  assert(span_arena_used < span_arenasz);
  return ret;
}

span nullspan() {
  return (span){0, 0};
}

int bool_neq(int, int);

int bool_neq(int a, int b) { return ( a || b ) && !( a && b); }

/*
C string routines are a notorious cause of errors.
We are adding to our spanio library as needed to replace native C string methods with our own safer approach.
We do not use null-terminated strings but instead rely on the explicit end point of our span type.
Here we have spanspan(span,span) which is equivalent to strstr or memmem in the C library but for spans rather than C strings or void pointers respectively.
We implement spanspan with memmem under the hood so we get the same performance.
Like strstr or memmem, the arguments are in haystack, needle order, so remember to call spanspan with the thing you are looking for as the second arg.
We return a span which is either NULL (i.e. nullspan()) or starts with the first location of needle and continues to the end of haystack.
Examples:

spanspan "abc" "b" -> "bc"
spanspan "abc" "x" -> nullspan
*/

span spanspan(span haystack, span needle) {
  // If needle is empty, return the full haystack as strstr does.
  if (empty(needle)) return haystack;

  // If the needle is larger than haystack, it cannot be found.
  if (len(needle) > len(haystack)) return nullspan();

  // Use memmem to find the first occurrence of needle in haystack.
  void *result = memmem(haystack.buf, len(haystack), needle.buf, len(needle));

  // If not found, return nullspan.
  if (!result) return nullspan();

  // Return a span starting from the found location to the end of haystack.
  span found;
  found.buf = result;
  found.end = haystack.end;
  return found;
}

// Checks if a given span is contained in a spans.
// Returns 1 if found, 0 otherwise.
// Actually a more useful function would return an index or -1, so we don't need another function when we care where the thing is.
int is_one_of(span x, spans ys) {
    for (int i = 0; i < ys.n; ++i) {
        if (span_eq(x, ys.s[i])) {
            return 1; // Found
        }
    }
    return 0; // Not found
}

/* END LIBRARY CODE */

typedef struct {
  pid_t pid;     // Process ID of the Stockfish process
  int to_stockfish[2]; // Pipe for sending data to Stockfish
  int from_stockfish[2]; // Pipe for receiving data from Stockfish
  u8* cmp_highwater; // Highwater mark of consumed output from Stockfish in cmp
} StockfishProcess;

void launch_stockfish(StockfishProcess *sp) {
  // Create pipes
  if (pipe(sp->to_stockfish) == -1 || pipe(sp->from_stockfish) == -1) {
    perror("pipe");
    exit2(EXIT_FAILURE);
  }

  // Fork the current process
  sp->pid = fork();
  if (sp->pid == -1) {
    perror("fork");
    exit2(EXIT_FAILURE);
  } else if (sp->pid == 0) {
    // Child process: Set up and execute Stockfish

    // Set up pipes for standard input/output
    dup2(sp->to_stockfish[0], STDIN_FILENO);
    close(sp->to_stockfish[0]);
    close(sp->to_stockfish[1]);

    dup2(sp->from_stockfish[1], STDOUT_FILENO);
    close(sp->from_stockfish[0]);
    close(sp->from_stockfish[1]);

    // Execute Stockfish
    execlp("stockfish", "stockfish", (char *)NULL);
    perror("execlp");
    exit2(EXIT_FAILURE);
  } else {
    // Parent process: Close unused ends of the pipes
    close(sp->to_stockfish[0]);
    close(sp->from_stockfish[1]);
  }
}

#define PRT_STOCKFISH 0

void send_to_stockfish(StockfishProcess *sp, const char *cmd) {
  if (PRT_STOCKFISH) prt("sending to stockfish: %s", cmd);
  write(sp->to_stockfish[1], cmd, strlen(cmd));
}

/*
We read output from stockfish and append it to cmp.

We use a 1k buffer but read again if the buffer was filled by the read() call.
We memcpy to cmp.end so that we append, and then extend cmp.end such that len(cmp) will be greater by the amount of data read.
*/

void read_from_stockfish(StockfishProcess *sp) {
  char buffer[1024];
  ssize_t bytes_read;
  do {
    bytes_read = read(sp->from_stockfish[0], buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
      buffer[bytes_read] = '\0'; // Null-terminate the string
      // Append the read data to cmp
      memcpy(cmp.end, buffer, bytes_read);
      cmp.end += bytes_read; // Update cmp.end to reflect the new data
    }
  } while (bytes_read == sizeof(buffer) - 1); // Continue reading if buffer was filled
  if (PRT_STOCKFISH) {
    prt("read_from_stockfish:\n");
    wrs(cmp);terpri();
  }
}

void set_stockfish_highwater(StockfishProcess *sp) {
  sp->cmp_highwater = cmp.end;
}

span get_stockfish_new_output(StockfishProcess *sp) {
  return (span){sp->cmp_highwater, cmp.end};
}

typedef struct {
    span lan_move; // The move in Long Algebraic Notation (LAN)
    int cp_eval;   // The centipawn evaluation of the move given by Stockfish
} MoveEvaluation;

typedef struct {
  int move_number;
  span san; // Standard Algebraic Notation move text
  span lan; // Long Algebraic Notation used by stockfish
  span annotation; // Move annotations like "?!","!!","?", or "!"
  span comments[10]; // Array of comments
  int num_comments; // Number of comments
  //move_sequence variations[5]; // Array of variations
  int num_variations; // Number of variations
  MoveEvaluation *evals; // Eval of every legal move from this position
  int n_evals; // number of evals; equal to number of legal moves at this point
} move;

typedef struct {
    span *tags;          // Array of spans, each representing a tag
    int tag_count;       // Number of tags in the array
    int max_tags;        // Allocated array size
    move *moves;         // Array of moves
    int move_count;      // Number of moves in the array
} Game;

/*
 * Function: parse_pgn
 * --------------------
 * Parses a Portable Game Notation (PGN) formatted chess game from the input span.
 *
 * The function extracts the sequence of chess moves and relevant metadata from a PGN-encoded chess game.
 * It handles the basic PGN structure, including tags (metadata enclosed in square brackets)
 * and the move text (standard algebraic notation), ignoring annotations and variations for simplicity.
 * The function dynamically allocates memory for an array of spans, each representing a single move,
 * and returns a Game structure containing this array and the move count.
 *
 * Parameters:
 * - inp: A span containing the full PGN text of a chess game.
 *
 * Returns:
 * - A Game struct with two fields: a pointer to an array of move spans and the total move count.
 *   Each move span is a slice of the input span, pointing to the start and end of the move text.
 *
 * Note:
 * The caller is responsible for freeing the dynamically allocated memory for the moves array
 * in the Game struct to avoid memory leaks.
 * Error handling is minimal; malformed PGNs may lead to undefined behavior.
 */

// Global flag for enabling debugging output
int debug_mode = 0;

// Parser functions declarations
void parse_pgn(span *input, Game *game);
void parse_tag_section(span *input, Game *game);
void parse_move_section(span *input, Game *game);
span parse_tag(span *input);
void parse_comment(span *input);
span parse_result(span *input);

void parse_pgn(span *input, Game *game) {
  if (debug_mode) {
    prt("Entering parse_pgn\n");
  }

  parse_tag_section(input, game);
  parse_move_section(input, game);

  if (debug_mode) {
    prt("Leaving parse_pgn\n");
  }
}

void skip_whitespace(span *input) {
  while (input->buf < input->end && isspace(*input->buf)) {
    input->buf++;
  }
}

/* parse_tag is called when there is already an open square bracket at the front of the input.
It parses up to the closing bracket, skips whitespace, and returns the tag as a span.
*/

span parse_tag(span *input) {
  if (debug_mode) prt("enter parse_tag (len %d)\n", len(*input));
  if (input->buf < input->end && *input->buf == '[') {
    u8 *start = input->buf++;
    while (input->buf < input->end && *input->buf != ']') {
      input->buf++;
    }
    if (input->buf < input->end) { // Successfully found closing bracket
      span tag = {start + 1, input->buf}; // *** manually fixed by adding one to exclude opening bracket ***
      input->buf++; // Move past the closing bracket
      if (debug_mode) prt("exit parse_tag success (len %d)\n", len(*input));
      skip_whitespace(input);
      return tag;
    }
  }
  if (debug_mode) prt("exit parse_tag no parse (len %d)\n", len(*input));
  return (span){NULL, NULL}; // Return a null span to indicate failure
}

void parse_tag_section(span *input, Game *game) {
  if (debug_mode) {
    prt("Entering parse_tag_section\n");
  }

  // Initialize the tag count and allocate memory for tags array
  game->tag_count = 0;
  game->tags = (span *)malloc(sizeof(span));
  if (game->tags == NULL) {
    perror("Memory allocation error");
    exit2(EXIT_FAILURE);
  }

  while (input->buf < input->end && *input->buf == '[') {
    span tag = parse_tag(input);
    if (tag.buf != NULL) {
      // Reallocate memory for tags array to accommodate the new tag
      game->tags = (span *)realloc(game->tags, (game->tag_count + 1) * sizeof(span));
      if (game->tags == NULL) {
        perror("Memory allocation error");
        exit2(EXIT_FAILURE);
      }
      // Add the new tag to the tags array
      game->tags[game->tag_count++] = tag;
    }
  }

  if (debug_mode) {
    prt("Leaving parse_tag_section\n");
  }
}

void parse_comment(span *input) {
  if (*input->buf == '{') {
    input->buf++; // Skip the opening brace
    while (input->buf < input->end && *input->buf != '}') {
      input->buf++; // Skip past the content of the comment
    }
    if (input->buf < input->end) {
      input->buf++; // Skip the closing brace
    }
  }
}

/*
 * Relevant EBNF Fragment for a Chess Move in PGN:
 * move ::= move_number move_text { comment }
 * move_number ::= digit {digit} '.'
 * move_text ::= letter { letter | digit | symbol }
 * comment ::= '{' { any_character } '}'
 * 
 * We have a convenience function parse_move_number which returns an int.
 * This function also consumes the dots (one or three) that follow the number.
 * We do not need to know how many there were because we already know who has the move by previous moves.
 * 
 * We will call another function, parse_san, to parse the actual SAN move.
 * The move may be followed by some kind of annotation like "?!" which we will also parse.
 * 
 * We can use w_char to print an individual character.
 *
 * Sample Moves:
 * 1. d4 { [%eval 0.15] [%clk 0:10:00] }
 * 1... d5 { [%eval 0.25] [%clk 0:10:00] }
 * 4. Bg5?! { [%eval -0.55] } { Inaccuracy. Nc3 was best. } { [%clk 0:09:46] }
 * 
 * We handle variations by recursing into a parse_move_sequence function.
 * They look like this:
 * 
 * (4. Nc3 e6 5. Bg5 Be7 6. e3 O-O 7. Bd3 dxc4 8. Bxc4 c5)
 * We consume the open paren and then recurse, and then consume the close paren.
 * 
 * Note that we also must parse O-O and O-O-O.
 */

void print_move(move m) {
  prt("Move Number: %d\n", m.move_number);
  prt("SAN: %.*s\n", m.san.end - m.san.buf, m.san.buf);
  prt("Annotation: %.*s\n", m.annotation.end - m.annotation.buf, m.annotation.buf);

  // Print comments
  prt("Comments:\n");
  for (int i = 0; i < m.num_comments; i++) {
    prt("  %.*s\n", m.comments[i].end - m.comments[i].buf, m.comments[i].buf);
  }

/*
  // Print variations
  prt("Variations:\n");
  for (int i = 0; i < m.num_variations; i++) {
    prt("  Variation %d:\n", i + 1);
    print_move_sequence(m.variations[i]); // Assuming print_move_sequence is defined to handle move_sequence type
  }
*/
}

// Forward declaration of the parse_move_sequence function for recursion
void parse_move_sequence(span *input);
span parse_san(span *input);
int parse_move_number(span*);

move parse_move(span *input) {
  move m = {0};
  //m.num_comments = 0;
  //m.num_variations = 0;

  // Parse move number and consume dots
  if (isdigit(*input->buf)) m.move_number = parse_move_number(input);

  skip_whitespace(input);

  // Parse SAN move
  m.san = parse_san(input);

  // Parse any annotations like "?!","!!","?", or "!"
  span annotation = {input->buf, input->buf};
  if (*input->buf == '!' || *input->buf == '?') {
    annotation.buf = input->buf++;
    while (*input->buf == '!' || *input->buf == '?') {
      input->buf++;
    }
    annotation.end = input->buf;
  }
  m.annotation = annotation;

  skip_whitespace(input);

  // Parse comments
  while (*input->buf == '{') {
    if (m.num_comments >= 10) {
      prt("Array bounds exceeded for comments.\n");
      exit2(1); // Exit to fix the code
    }

    input->buf++; // Skip '{'
    span comment = {input->buf, NULL};
    while (*input->buf != '}') {
      if (input->buf >= input->end) {
        prt("Comment not properly closed.\n");
        exit2(1); // Exit to fix the code
      }
      input->buf++;
    }
    comment.end = input->buf;
    m.comments[m.num_comments++] = comment;
    input->buf++; // Skip '}'
    skip_whitespace(input); // Skip any whitespace after the comment
  }

  // Parse variations
  while (*input->buf == '(') {
    //if (m.num_variations >= 5) {
     // prt("Array bounds exceeded for variations.\n");
      //exit2(1); // Exit to fix the code
    //}

    input->buf++; // Consume the '('
    parse_move_sequence(input); // Parse the variation
    if (*input->buf != ')') {
      prt("Variation not properly closed with ')'.\n");
      exit2(1); // Exit to fix the code
    }
    input->buf++; // Consume the ')'
    m.num_variations++;
    skip_whitespace(input); // Skip any whitespace after the variation
  }

  if (debug_mode) {
    prt("end of parse_move\n");
    dbgd(len(*input));
  }
  return m;
}

int parse_move_number(span *input) {
  int move_number = 0;

  // Convert the sequence of digits to an integer
  while (input->buf < input->end && isdigit(*input->buf)) {
    move_number = move_number * 10 + (*input->buf - '0');
    input->buf++;
  }

  // Expecting at least one dot after the move number
  if (input->buf < input->end && *input->buf == '.') {
    input->buf++; // Skip the first dot

    // Handle ellipsis for black's moves (three dots)
    while (input->buf < input->end && *input->buf == '.') {
      input->buf++;
    }
  } else {
    prt("Expected dot after move number.\n");
    flush();
    exit2(1); // Exit if the format is incorrect
  }

  return move_number;
}


int is_san_char(char c) {
  return isalpha(c) || // Letter for piece or column
    isdigit(c) || // Digit for row
    c == 'x' ||   // Capture indicator
    c == '=' ||   // Promotion indicator
    c == 'O' ||   // Castling
    c == '+' ||   // Check indicator
    c == '#' ||   // Checkmate indicator
    c == '-'      // Part of castling notation
    ;
}

span parse_san(span *input) {
  if (debug_mode) prt("enter parse_san (len %d)\n", len(*input));
  span san_move;
  san_move.buf = input->buf; // Start of the SAN move

  while (input->buf < input->end && (
        isalpha(*input->buf) || // Letter for piece or column
        isdigit(*input->buf) || // Digit for row
        *input->buf == 'x' ||   // Capture indicator
        *input->buf == '=' ||   // Promotion indicator
        *input->buf == 'O' ||   // Castling
        *input->buf == '+' ||   // Check indicator
        *input->buf == '#' ||   // Checkmate indicator
        *input->buf == '-'      // Part of castling notation
        )) {
    input->buf++; // Advance through the SAN move
  }

  san_move.end = input->buf; // End of the SAN move

  // Skip any trailing spaces after the SAN move
  skip_whitespace(input);

  if (debug_mode) {
    prt("end of parse_san\n");
    w_char(*input->buf);terpri();
    dbgd(len(*input));
  }

  return san_move;
}

void parse_move_sequence(span *input) {
  skip_whitespace(input);
  while (input->buf < input->end && *input->buf != ')') {
    move m = parse_move(input); // Recursively parse each move in the sequence
    if (debug_mode) print_move(m);
    skip_whitespace(input);
  }
  if (debug_mode) {
    prt("after parse_move_sequence\n");
    dbgd(len(*input));
  }
}

/*
In parse_result we parse one of the four PGN result strings, and return a span.
A null span indicates to the caller that nothing was parsed.
In debug mode we also print the game result that was parsed.
*/

span parse_result(span *input) {
    static const char *results[] = {"1-0", "0-1", "1/2-1/2", "*"};
    span result = {NULL, NULL};
    for (int i = 0; i < 4; ++i) {
        size_t len = strlen(results[i]);
        if ((input->end - input->buf) >= len && strncmp((char *)input->buf, results[i], len) == 0) {
            result.buf = input->buf;
            result.end = input->buf + len;
            if (debug_mode) {
                prt("Parsed game result: %.*s\n", (int)len, input->buf);
            }
            input->buf += len; // Move the input buffer forward
            break;
        }
    }
    return result;
}

/*
In parse_move_section we parse out the rest of the PGN file after the metadata in the tags at the top.

The only thing we are currently using from this is the move sequence on the main line.

In a loop we first try to parse a result, which is one of a fixed number of short strings.
If parse_result doesn't parse anything we then try to parse a move by calling parse_move, which handles everything from the move number on.
Assumption: everything in the move section will be handled by either parse_move or parse_result.

In debug mode we indicate entering and leaving the function, as with all our parsing functions.
*/

void parse_move_section(span *input, Game *game) {
  if (debug_mode) {
    prt("Entering parse_move_section\n");
  }

  // Dynamically allocate an initial buffer for moves, may need resizing
  int capacity = 10; // Initial capacity for moves
  game->moves = malloc(capacity * sizeof(move));
  game->move_count = 0;

  while (input->buf < input->end) {
    span resultSpan = parse_result(input);
    if (resultSpan.buf == NULL) { // No game result found, proceed to parse move
      if (game->move_count == capacity) {
        // Resize the array if we've reached capacity
        capacity *= 2;
        game->moves = realloc(game->moves, capacity * sizeof(move));
      }
      move mv = parse_move(input); // Parse the next move
      game->moves[game->move_count++] = mv; // Add the move to the Game
    } else {
      // If we've parsed a result, it's the end of the move section.
      break;
    }
  }

  if (debug_mode) {
    prt("Leaving parse_move_section with %d moves parsed.\n", game->move_count);
  }
}

void print_game(Game game) {
  for (size_t i = 0; i < game.move_count; ++i) {
    wrs(game.moves[i].san); // Print the move
    terpri(); // Move to the next line
  }
}

/*
SAN to LAN:

- get a clean output of all the SAN moves on the main line and nothing else from the PGN - done
- get position from stockfish as FEN
- get candidate squares for the starting square from the position 
- get legal moves as LAN from stockfish if needed for disambiguation

*/

void poll_stockfish(span, int, StockfishProcess*);

/*
We read from stockfish in a loop, waiting a few ms each time, until the output contains the string provided.
If that never happens, we will loop forever.
To prevent this, we limit the maximum wait time to the second argument, which is in milliseconds.
If the limit is reached, we print a warning and exit the process.
*/

void poll_stockfish(span target, int max_wait_ms, StockfishProcess *sp) {
  int total_wait = 0;
  int wait_interval_ms = 10; // Interval to wait between polls in milliseconds
  span new_output = (span){sp->cmp_highwater, cmp.end};
  while (!contains(new_output, target)) {
    usleep(wait_interval_ms * 1000); // Sleep for wait_interval_ms milliseconds
    read_from_stockfish(sp); // Read the current output from Stockfish
    new_output.end = cmp.end;
    total_wait += wait_interval_ms;
    if (total_wait > max_wait_ms) {
      prt("Warning: Max wait time of %d ms exceeded while waiting for \"%.*s\".\n", max_wait_ms, len(target), target.buf);
      flush();
      exit(EXIT_FAILURE);
    }
  }
}

spans get_legal_lan_moves(StockfishProcess *sp);

/*
We get the legal moves by sending "go perft 1" to stockfish.
We poll the stockfish response until it contains the string "Nodes searched".

example stockfish session demonstrating getting the 20 legal moves in the starting position:

input to stockfish:
position startpos
go perft 1

output from stockfish:
a2a3: 1
b2b3: 1
c2c3: 1
d2d3: 1
e2e3: 1
f2f3: 1
g2g3: 1
h2h3: 1
a2a4: 1
b2b4: 1
c2c4: 1
d2d4: 1
e2e4: 1
f2f4: 1
g2g4: 1
h2h4: 1
b1a3: 1
b1c3: 1
g1f3: 1
g1h3: 1

Nodes searched: 20

In this function we assume that stockfish has already been given the current position, so we just send the go command.
We determine the number of positions from the output, use spans_alloc() to get a spans of that size, and then put each LAN move as a span into the spans, which we return.
To parse the output of stockfish, we create a new span which is a copy of cmp, but which we will mutate as we parse it.
 *** manually fixed this to use the new get_stockfish_new_output() and set_stockfish_highwater() ***
We use find_char(span, char), which returns an offset, to find the first colon, and take_n() to consume up to that colon.
Then we can advance to the newline and consume that.
Empty lines in the output will be skipped.
Also, the Nodes searched line, which also contains a colon, should be skipped, not added as another "move".
So once we have pulled out a span up to the colon, we check if it contains the string "Nodes searched" and if so we do not add it as a move.
*/

spans get_legal_lan_moves(StockfishProcess *sp) {
  set_stockfish_highwater(sp);

  send_to_stockfish(sp, "go perft 1\n");
  flush(); // Ensure command is sent to Stockfish

  span target = S("Nodes searched");
  poll_stockfish(target, 5000, sp); // Wait for Stockfish to output "Nodes searched", up to 5 seconds

  // Prepare to parse the output
  //span work = cmp; // Make a working copy of cmp to parse
  span work = get_stockfish_new_output(sp);
  int moves_count = 0;
  spans moves = spans_alloc(20); // Allocate space for up to 20 moves initially, assuming standard opening move count

  while (!empty(work)) {
    int newline_pos = find_char(work, '\n');
    if (newline_pos == -1) break; // No more lines to process

    span line = first_n(work, newline_pos);
    int colon_pos = find_char(line, ':');
    if (colon_pos != -1) {
      span move_span = first_n(line, colon_pos);
      if (!span_eq(move_span, target)) { // Ignore "Nodes searched" line
        if (moves_count >= moves.n) {
          // Expand the moves array if necessary
          spans new_moves = spans_alloc(moves.n * 2); // Double the capacity
          memcpy(new_moves.s, moves.s, moves.n * sizeof(span)); // Copy existing moves
          moves = new_moves;
        }
        moves.s[moves_count++] = move_span;
      }
    }
    work.buf += newline_pos + 1; // Advance past the newline
  }

  moves.n = moves_count; // Update the count of moves
  return moves;
}

void populate_lan_moves(Game*, StockfishProcess*);

void send_position(StockfishProcess *sp, Game *game, int upto_move);
span correlate_san_with_lan(span san_move, spans legal_lan_moves);

typedef struct {
  int is_white_move; // which player the move is for *** manually added ***
  char piece_moved; // The type of piece moved (R, N, B, Q, K, a-h for pawn)
  int is_capture; // If the move is a capture *** manually added ***
  char disambiguation[3]; // Disambiguation information, can be a file ('a'-'h'), a rank ('1'-'8'), or both, e.g., "e2"
  char destination_square[3]; // The destination square of the move, e.g., "e4"
  char promotion_piece; // The piece a pawn is promoted to, if applicable ('Q', 'R', 'B', 'N'). 0 if not a promotion.
  char check_indicator; // '+' for check, '#' for checkmate, 0 if neither.
} SanDetails;

/*
A nice pretty-printer for SanDetails.
*** manually fixed a bit ***
*/

void pretty_print_san_details(SanDetails details) {
  prt("Move Details:\n");
  prt("Player: %s\n", details.is_white_move ? "White" : "Black");
  prt("Piece Moved: %c\n", details.piece_moved);
  prt("Capture: %s\n", details.is_capture ? "Yes" : "No");
  prt("Disambiguation: %s\n", details.disambiguation[0] ? details.disambiguation : "None");
  prt("Destination Square: %s\n", details.destination_square);
  prt("Promotion: %c\n", details.promotion_piece ? details.promotion_piece : '0');
  prt("Check/Checkmate: %c\n", details.check_indicator ? details.check_indicator : '0');
}

/*
In populate_lan_moves, we are given a Game which has SAN moves from the PGN, but does not have the LAN moves that we need for stockfish.

To get the LAN moves, for each move in the game, we
- tell stockfish the current position by the send_position() helper function.
- parse the SAN to get:
  - the piece (or pawn) type that was moved, one of R N B Q K or a-h for a pawn move, and
  - the disambiguation information if any, which can be a rank, a file, or neither or both, and
  - the destination square, and
  - the piece promoted to if the move was a pawn promotion, and
  - the check or checkmate indication, which our san parsing function also finds but which we aren't using here.
  - we pass i % 2 == 0 into parse_san_details since it needs to know the move for handling castles
- get the position as FEN from stockfish
- call a helper function which uses the parsed SAN and the FEN info to return the square containing the piece that moved
  - this function can optionally call stockfish to get the legal moves, which is also uses if needed
- then we simply concatenate the algebraic start square and end square, along with the lowercased promotion piece, if there is one, to get the LAN move which is always 4 or 5 chars, 5 being in the case of pawn promotions.
- add this LAN move to the move.lan

Invariant: we always have a LAN move for moves prior to the current move, and we don't have LAN moves for any later ones.
Once we have reached the end of the game then all the LAN moves are populated and we are done.
*/

SanDetails parse_san_details(span, int);
void get_fen_from_stockfish(StockfishProcess*, char*, size_t);
void find_start_square(char *fen, SanDetails san_details, char *start_square, StockfishProcess *sp);
void assign_lan_move(move*, char*);

void populate_lan_moves(Game *game, StockfishProcess *sp) {
  char fen[256]; // Buffer to hold FEN string

  for (int i = 0; i < game->move_count; ++i) {
    // Set position in Stockfish up to the current move
    send_position(sp, game, i);

    // Parse the SAN details for the current move
    SanDetails san_details = parse_san_details(game->moves[i].san, i % 2 == 0);

    // Get the current position as FEN from Stockfish
    get_fen_from_stockfish(sp, fen, sizeof(fen));

    // Find the starting square based on FEN and parsed SAN details
    char start_square[3]; // Buffer to hold the starting square
    find_start_square(fen, san_details, start_square, sp);

    // Construct the LAN move by concatenating the start square, the destination square,
    // and optionally the lowercased promotion piece
    char lan_move[6]; // Buffer to hold the LAN move
    if (san_details.promotion_piece) {
      snprintf(lan_move, sizeof(lan_move), "%s%s%c", start_square, san_details.destination_square, tolower(san_details.promotion_piece));
    } else {
      snprintf(lan_move, sizeof(lan_move), "%s%s", start_square, san_details.destination_square);
    }

    // Assign the constructed LAN move to the current move in the game
    assign_lan_move(&game->moves[i], lan_move);
  }
}

/*
In send_position, we construct a position string which contains "position startpos moves" followed by all the moves up to the argument passed in.
We get the LAN format that stockfish uses and separate them by spaces.
Then we send this string to stockfish, putting it in the position at that point in the game.
*/

void send_position(StockfishProcess *sp, Game *game, int upto_move) {
  // Start with setting the position to the start position
  char position_str[4096] = "position startpos moves";
  int len = strlen(position_str);

  // Append each LAN move up to the specified move number, separated by spaces
  for (int i = 0; i < upto_move && i < game->move_count; ++i) {
    // Assuming LAN moves are stored in game->moves[i].lan
    int move_len = game->moves[i].lan.end - game->moves[i].lan.buf;

    // Check if the move fits into the remaining buffer space, accounting for the space and null terminator
    if (len + move_len + 2 < sizeof(position_str)) {
      position_str[len++] = ' '; // Add space before the move
      strncpy(position_str + len, (char*)game->moves[i].lan.buf, move_len);
      len += move_len;
    } else {
      // Handle error: position string buffer overflow
      prt("Error: position string buffer overflow.\n"); flush();
      exit(EXIT_FAILURE);
    }
  }

  // Null-terminate the position string
  position_str[len] = '\0';

  //prt("sending position string: %s\n", position_str);
  // Send the constructed position string to Stockfish
  send_to_stockfish(sp, position_str);
  send_to_stockfish(sp, "\n"); // Ensure command is properly terminated
}

/*
parse_san_details gets a SAN move and an indication of whether it is white or black to move.
The details are parsed into the SanDetails return type.
Since the check indication is the last thing, we check for it first, and update the length accordingly, then do the same for the promotion if any.
We record the piece name or pawn file into piece_moved.
If the "piece" is "O" then it is castles, which is why we need to know who has the move, to determine the destination square; these are recorded as king moves and the destination square is set to g1 or c1 or g8 or c8 as appropriate.
Then the last two remaining chars that we haven't already handled will always be the destination square.
The char before that will be an "x" if the move was a capture.
Finally any remaining chars not already handled will be disambiguation chars, of which there may be 0 to 2.
*/

SanDetails parse_san_details(span san, int is_white_move) {
  SanDetails details = {0};
  details.is_white_move = is_white_move;

  int length = san.end - san.buf;

  // Handle check or checkmate indicator
  if (san.buf[length - 1] == '+' || san.buf[length - 1] == '#') {
    details.check_indicator = san.buf[length - 1];
    length--; // Adjust length to exclude check/checkmate indicator
  }

  // Handle promotion
  if (length > 2 && san.buf[length - 2] == '=') {
    details.promotion_piece = san.buf[length - 1];
    length -= 2; // Adjust length to exclude promotion information
  }

  // Determine if the move is a capture
  // *** manually fixed *** from:

  /*
  if (san.buf[length - 3] == 'x') {
    details.is_capture = 1;
    length -= 1; // Adjust length to exclude 'x'
  }

  to: */
  if (san.buf[length - 3] == 'x') {
    details.is_capture = 1;
  }
  /* *** end manual fixup *** */

  // Set destination square
  details.destination_square[0] = san.buf[length - 2];
  details.destination_square[1] = san.buf[length - 1];
  details.destination_square[2] = '\0'; // Null-terminate

  // Castling
  if (san.buf[0] == 'O') {
    details.piece_moved = 'K'; // Castling is a king move
    // Determine the destination square based on castling side and who's moving
    strcpy(details.destination_square, is_white_move ? (length == 3 ? "g1" : "c1") : (length == 3 ? "g8" : "c8"));
  } else {
    // For non-castling moves, the first character might indicate the piece moved or be part of a pawn move
    if (isalpha(san.buf[0]) && san.buf[0] != 'x') {
      details.piece_moved = san.buf[0];
    } else {
      // *** manually added ***
      assert(0); // dead code
      // Pawn moves are indicated by file ('a'-'h')
      details.piece_moved = san.buf[length - 4]; // Assuming the move format includes the starting file for pawns
    }

    // Handle disambiguation, if any
    int disambiguationLength = length - 3 - details.is_capture; // Calculate length excluding destination square and capture indicator
    if (disambiguationLength > 0 && details.piece_moved != 'K') { // Exclude castling
      strncpy(details.disambiguation, san.buf + 1, disambiguationLength);
      details.disambiguation[disambiguationLength] = '\0'; // Null-terminate
    }
  }

  return details;
}

/*
The "d" command gets a representation of the current board position from stockfish in several forms like this:

 +---+---+---+---+---+---+---+---+
 | r | n | b | q | k | b | n | r | 8
 +---+---+---+---+---+---+---+---+
 | p | p | p | p | p | p | p | p | 7
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 6
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 5
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 4
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 3
 +---+---+---+---+---+---+---+---+
 | P | P | P | P | P | P | P | P | 2
 +---+---+---+---+---+---+---+---+
 | R | N | B | Q | K | B | N | R | 1
 +---+---+---+---+---+---+---+---+
   a   b   c   d   e   f   g   h

Fen: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
Key: 8F8F01D4562F59FB
Checkers: 

After sending "d\n", get_fen_from_stockfish polls until "Fen" appears, and then handles the output.

We only care about the FEN, so we parse it line by line until we find a line starting with "Fen: ",
strip this prefix, and return the FEN string in the buffer provided by the caller.
 *** manually fixed to start from sp->cmp_highwater instead of cmp.buf ***
*/

void get_fen_from_stockfish(StockfishProcess *sp, char *fen, size_t fen_size) {
  set_stockfish_highwater(sp);

  // Send "d" to Stockfish to display the current board position and various info
  send_to_stockfish(sp, "d\n");

  // Poll Stockfish output until "Fen" is found
  span target = S("Fen");
  poll_stockfish(target, 5000, sp); // Wait up to 5000 ms for "Fen" to appear

  // At this point, cmp contains the output including "Fen"
  // Parse cmp line by line to find the FEN string
  //char *line_start = (char *)cmp.buf;
  char *line_start = (char *)sp->cmp_highwater;
  char *fen_start;
  while ((line_start = strstr(line_start, "\n")) != NULL) {
    // Move past the newline character
    line_start++;

    // Check if the current line starts with "Fen: "
    if (strncmp(line_start, "Fen: ", 5) == 0) {
      // Found the FEN string
      fen_start = line_start + 5; // Skip past "Fen: "
      char *fen_end = strchr(fen_start, '\n');
      if (fen_end != NULL) {
        size_t fen_length = fen_end - fen_start;
        if (fen_length < fen_size) {
          // Copy the FEN string to the buffer provided by the caller
          strncpy(fen, fen_start, fen_length);
          fen[fen_length] = '\0'; // Null-terminate the FEN string
          return; // Successfully found and copied the FEN string
        }
      }
    }
  }

  // If we reach here, either we didn't find "Fen: " or there was a buffer size issue
  fprintf(stderr, "Failed to find or copy the FEN string from Stockfish's output.\n");
  exit(EXIT_FAILURE); // Exit indicating failure to get FEN
}

/*
In find_start_square we get a FEN string, a parsed SanDetails from a SAN move, and a destination buffer which will always be at least 3 chars long.
We also have the StockfishProcess, which we may need to get a list of legal moves if needed to find a unique solution.

First we find all the squares in the position that contain a piece of the type that moved.
In the case of a pawn move, this already includes the file that the pawn is on, which along with the destination square uniquely determines the starting square of the move.

In the case of a piece, we narrow the list of potential starting squares by the SAN disambiguation if any.
However, there may still be more than one piece of the given type in our list.
If this is the case, we then get the legal moves from stockfish; these will be in the LAN format.
If there was no disambiguation in the SAN move, it is because only one of the pieces of the given type can reach the destination square.
In this case we determine the starting square by looking for a legal move which has a starting square from our list, and there will only be one such.
 *** editing because the above was wrong ***
In this case we determine the starting square by looking for a legal move which has a starting square from our list and the destination square matching the SAN.

Examples:
Q8xd7. We find all the queens (of the right color) from the FEN, then filter out any that are not on the 8th rank. This gives a list of candidate starting squares. If there is more than one, that means there are two queens on the 8th rank; we then ask stockfish for the legal moves; only one of the queens will be able to reach the destination square, so we are done.
Be3. In most cases there may be two bishops, and we will find the squares they are not and get the legal moves to see which one can reach the destination.
e4. In this case it is already unambiguous since only one pawn can ever reach any given square without capture.
fxe4. The capture case is also unambiguous since the starting file is given for pawn captures.

First we'll call a helper function that finds the squares that are compatible with the SAN move, i.e. the piece or pawn type and the disambiguation if any.
This will return a spans with the candidate algebraic square names.
If this list has only one element then we are done, and we put it in start_square and return.
Otherwise, the list has more than one element.
We get the legal moves from stockfish, which also gives us a spans.
Then we loop over the candidate squares and, inside that, over the legal moves, and the first match that we find will be the result.
If we haven't found any match, some assumption in our code is incorrect so we report the error and simply crash.
We can also create a helper function to determine whether a candidate square is the start square of a LAN move.
*/

// Declaration for the helper functions assumed by the find_start_square implementation
spans find_candidate_squares(char *fen, SanDetails san_details);
int is_start_square_of_lan_move(span candidate_square, span lan_move);
int is_destination_square_match(span lan_move, SanDetails san_details);

void find_start_square(char *fen, SanDetails san_details, char *start_square, StockfishProcess *sp) {
  //prt("find_start_square: %s\n", fen);
  //pretty_print_san_details(san_details);
  // Find candidate starting squares based on the piece and any disambiguation
  spans candidate_squares = find_candidate_squares(fen, san_details);
  //dbgd(candidate_squares.n);

  if (candidate_squares.n == 1) {
    // If only one candidate, it's our starting square
    strncpy(start_square, (char*)candidate_squares.s[0].buf, 2);
    start_square[2] = '\0'; // Ensure null-termination
  } else {
    // If more than one candidate, get legal moves from Stockfish to resolve ambiguity
    spans legal_moves = get_legal_lan_moves(sp);

    // Iterate over candidate squares and legal moves to find a match
    int found = 0;
    for (int i = 0; i < candidate_squares.n; ++i) {
      //prt("candidate square: ");wrs(candidate_squares.s[i]);terpri();
      for (int j = 0; j < legal_moves.n; ++j) {
        //prt("legal move: ");wrs(legal_moves.s[j]);terpri();
        if (is_start_square_of_lan_move(candidate_squares.s[i], legal_moves.s[j])
          // *** next line added manually ***
          && is_destination_square_match(legal_moves.s[j], san_details)) {
          //prt("Found!\n");
          // Found the starting square
          strncpy(start_square, (char*)candidate_squares.s[i].buf, 2);
          start_square[2] = '\0'; // Ensure null-termination
          found = 1;
          break; // Stop searching once found
        }
      }
      if (found) break; // Stop searching once found
    }

    if (!found) {
      // If no match found, something went wrong
      prt("Error: Failed to find starting square for move.\n");flush();
      exit(EXIT_FAILURE); // Crash the program
    }
  }
}

/*
To find candidate squares we iterate over the FEN string, and we find each square containing the piece and color given by the SAN details.
We have a static string of length 128 that contains all the square names in the same layout as FEN (i.e. a8 through h1 in memory) and we construct spans (each of len() 2) that point into that.
We have a helper function that takes a single char from the FEN, the file, and our SanDetails and returns whether or not the square is a potential match.
(The file is needed only for pawns.)
If there is disambiguation info in the SAN we only return the squares that are consistent with it, otherwise any square that has the right kind of piece; we declare a helper function first that handles this disambiguation info.
For pawn moves, the result should always already be deterministic; we have a file in piece_moved and we remember that "p" or "P" is in the FEN.
FEN uses digits to indicate runs of blank squares, we handle these by incrementing the file we are on.
We use the rank and file to index into the square names correctly.
We use spans_alloc for the return variable.
Note that spans_alloc returns a spans with .n set to the size provided, so we use a separate variable result_count to track the number of results and set .n at the end.
We can allocate space for 10 spans as that is a maximum number of possible pieces of the same type that can be on the board in standard chess.
*/

int square_matches_piece(char fen_char, int file, char piece, int is_white_move);
int matches_disambiguation(const char *square, const char *disambiguation);

spans find_candidate_squares(char *fen, SanDetails san_details) {
  static const char square_names[] = 
    "a8b8c8d8e8f8g8h8"
    "a7b7c7d7e7f7g7h7"
    "a6b6c6d6e6f6g6h6"
    "a5b5c5d5e5f5g5h5"
    "a4b4c4d4e4f4g4h4"
    "a3b3c3d3e3f3g3h3"
    "a2b2c2d2e2f2g2h2"
    "a1b1c1d1e1f1g1h1";

  spans result = spans_alloc(10); // Allocate space for up to 10 candidate squares
  int result_count = 0;

  int square_index = 0; // Index to iterate over square_names
  int rank = 8, file = 0; // Start from the 8th rank and 'a' file

  for (int fen_index = 0; fen[fen_index] != ' '; ++fen_index) {
    char fen_char = fen[fen_index];
    if (fen_char == '/') {
      rank--; // Move to the next rank
      file = 0; // Reset file to 'a'
      continue;
    }

    if (isdigit(fen_char)) {
      file += fen_char - '0'; // Skip empty squares
    } else {
      if (square_matches_piece(fen_char, file, san_details.piece_moved, san_details.is_white_move)) {
        char current_square[3] = {file + 'a', rank + '0', '\0'};

        if (matches_disambiguation(current_square, san_details.disambiguation)) {
          // Construct a span for the current square and add to result
          int name_index = ((8 - rank) * 16) + (file * 2); // Calculate index in square_names
          result.s[result_count] = (span){(u8*)square_names + name_index, (u8*)square_names + name_index + 2};
          result_count++;
        }
      }
      file++; // Move to the next file
    }
  }

  result.n = result_count; // Update the count of result spans
  return result;
}

// Helper function to print spans for debugging
void print_spans(spans s) {
    printf("Candidate Squares:\n");
    for (int i = 0; i < s.n; ++i) {
        printf("%.*s\n", s.s[i].end - s.s[i].buf, s.s[i].buf);
    }
}

/*
In square_matches_piece we get a FEN char, one of KQBNRP upper or lower, a file, and a SAN piece char, one of 'a'-'h' lowercase, or KQBNR, which are upper for both w and b in SAN moves.
We get a file which tells where the FEN char was in the FEN, and an indication of who has the move.
We return true if the FEN char matches the piece type, color, and in the case of a pawn, the file indicated by the SAN piece char.
*/

int square_matches_piece(char fen_char, int file, char piece, int is_white_move) {
  // Check color and piece type
  int is_fen_char_white = isupper(fen_char);
  char fen_piece_type = toupper(fen_char);
//prt("square_matches_piece: %c %d %c %d\n", fen_char, file, piece, is_white_move);

  // Determine if the SAN piece represents a pawn move (indicated by a file 'a'-'h')
  int is_pawn_move = (piece >= 'a' && piece <= 'h');

  // Match color
  if (bool_neq(is_white_move, is_fen_char_white)) {
    return 0; // Color mismatch
  }

  // For pawns: Check if the file matches the SAN piece (indicating the pawn's file)
  if (is_pawn_move) {
    int pawn_file = piece - 'a'; // Convert 'a'-'h' to 0-7 for file comparison
    return (fen_piece_type == 'P' && file == pawn_file);
  }

  // For non-pawn pieces: Check if the piece types match
  // Convert SAN piece to corresponding FEN character
  char san_piece_to_fen;
  switch (piece) {
    case 'K': san_piece_to_fen = 'K'; break;
    case 'Q': san_piece_to_fen = 'Q'; break;
    case 'B': san_piece_to_fen = 'B'; break;
    case 'N': san_piece_to_fen = 'N'; break;
    case 'R': san_piece_to_fen = 'R'; break;
    default: return 0; // Invalid piece character
  }

  //dbgd(fen_piece_type == san_piece_to_fen);flush();
  return (fen_piece_type == san_piece_to_fen);
}


/*
Helper function to check if the current square matches the disambiguation criteria.
The SAN disambiguation is either a rank "a"-"h" or a file "1"-"8" or both like "d4", or nothing.
It's provided as three chars, so the first can be either a rank or file, and the second will always be a rank if anything.
We return false if any disambiguation is present and doesn't match and true otherwise.
*/

int matches_disambiguation(const char *square, const char *disambiguation) {
  // If disambiguation is empty, there's nothing to match against, so it's considered a match
  if (disambiguation[0] == '\0') {
    return 1;
  }

  // Disambiguation can be file, rank, or both
  // Check if the first character of disambiguation matches the file (square[0]) or rank (square[1])
  if (isalpha(disambiguation[0])) { // File disambiguation
    if (square[0] != disambiguation[0]) {
      return 0; // File doesn't match
    }
  }
  if (isdigit(disambiguation[0])) { // Rank disambiguation, or file was a digit, which is an error in this context
    if (square[1] != disambiguation[0]) {
      return 0; // Rank doesn't match
    }
  }

  // If there's a second character and it's a digit, it must be rank disambiguation
  if (disambiguation[1] != '\0' && isdigit(disambiguation[1])) {
    if (square[1] != disambiguation[1]) {
      return 0; // Rank doesn't match
    }
  }

  // Passed all checks, so it's a match
  return 1;
}

/*
Helper function to check if a LAN move matches a square passed in as a (len() = 2) span.
Example:
"a3" "a3a4" -> 1
"a3" "d2d4" -> 0
The spans passed in will always be of the correct length so we do not need to test for that.
*/

int is_start_square_of_lan_move(span candidate_square, span lan_move) {
//prt("is_start_square_of_lan_move\n");
//wrs(candidate_square);terpri();
//wrs(lan_move);terpri();
    // Since spans are guaranteed to be the correct length, we directly compare the first two characters
    if (candidate_square.buf[0] == lan_move.buf[0] && candidate_square.buf[1] == lan_move.buf[1]) {
        return 1; // The start square of the LAN move matches the candidate square
    } else {
        return 0; // No match
    }
}

int is_destination_square_match(span lan_move, SanDetails san_details);

/*
Helper function to determine if a LAN move matches the destination square of a SAN move.
The destination square of a SAN move is always 2 chars and so we can just do a direct comparison of the two chars with the second two chars of the LAN move.
We do not need to check the length of the LAN move passed in, as we know it will be correct.
We simply compare the 3rd and 4th characters of the lan move with the two of the SAN details destination square.
*/

int is_destination_square_match(span lan_move, SanDetails san_details) {
    // Direct comparison of the destination square with the last two characters of the LAN move
    // Note: LAN move format is "e2e4" or "e7e8q" for promotion, so characters at positions 2 and 3 (0-based index) are the destination square
    return lan_move.buf[2] == san_details.destination_square[0] && lan_move.buf[3] == san_details.destination_square[1];
}

/*
We are given a char* for a LAN move and must assign it to move* m as a span.
The char* data is on the stack, so we must copy the char* data somewhere before it goes out of scope.
We use the cmp space for this.
We can use prt2cmp() to redirect all output to the cmp space, then use prt to write the data, with newlines before and after it.
Then we call prt2std() so that we don't change the output mode.
We point the lan span on the move to the data that we have just added, without the newlines.
We know how long the data is so we can just subtract from cmp.end.
LAN moves are always 4 or 5 chars long, so we assert that our span is always the correct length before returning.
Don't forget the len() function exists.
*/

void assign_lan_move(move *m, char *lan_move) {
  // Redirect output to the cmp space
  prt2cmp();

  // Write the LAN move with newlines before and after to ensure it's isolated
  prt("\n%s\n", lan_move);

  // Redirect output back to standard output
  prt2std();

  // Calculate the start of the LAN move in the cmp space (skipping the initial newline)
  u8 *lan_start = cmp.end - strlen(lan_move) - 1; // Subtract the length of the LAN move and the newline after it

  // Point the lan span in the move to the data just added, excluding newlines
  m->lan.buf = lan_start;
  m->lan.end = lan_start + strlen(lan_move);

  // Assert the length is either 4 or 5 chars
  assert(len(m->lan) == 4 || len(m->lan) == 5);
}

void do_analysis(Game*, StockfishProcess*);

/*
To actually do the analysis, we send each position in the game to stockfish.
In each position we then send "go movetime 1000" to analyze for 1 second.
We read all the output from stockfish, which contains info lines that have the values we are interested in.
Here is an example:

info depth 13 seldepth 22 multipv 1 score cp -26 nodes 681621 nps 680260 hashfull 302 tbhits 0 time 1002 pv g8f6 b1c3 e7e6 c1g5 f8e7 e2e3 e8g8 g5h4 f6e4 h4e7 d8e7 d1c2 e4f6 f1d3 d5c4 d3c4
info depth 13 seldepth 13 multipv 2 score cp -38 nodes 681621 nps 680260 hashfull 302 tbhits 0 time 1002 pv e7e6 e2e3 g8f6 b2b3 b8d7 c1b2 b7b6 f1d3 c8b7 e1g1 f8d6 b1d2 e8g8
info depth 13 seldepth 17 multipv 3 score cp -38 nodes 681621 nps 680260 hashfull 302 tbhits 0 time 1002 pv a7a6 c4c5 g8f6 b1c3 g7g6 c1f4 f6h5 f4e5 f8g7 e5g7 h5g7 h2h3 b8d7 e2e3 e8g8 f1d3
[... more lines ...]

The multipv is used to order the lines from best to worst, but since we are going to process all of them we don't care about this.
The only information we need is the first move after "pv", which is the move that we will use for our arrow, and the number after "cp" which is the eval in centipawns.
The lines may be repeated for the same moves, but we can handle this by parsing all of them and updating the cp eval for each one so we will always have the latest result that stockfish gives.

So in this function we just iterate over all the moves in the game, and call a helper function that does the analysis.
As we have a place on the move struct to store the evals, here we just call send_position to update the stockfish process with the current position.
We then call analyze_move to get the evals, and we pass a pointer to the move into this function so that it can store them.
*/

void analyze_move(StockfishProcess *sp, move *m);
void analyze_move_2(StockfishProcess *sp, move *m);

void do_analysis(Game *game, StockfishProcess *sp) {
  for (int i = 0; i < game->move_count; ++i) {
    // Set the position in Stockfish up to the current move
    send_position(sp, game, i);

    // Analyze the current move and store the evaluations
    analyze_move_2(sp, &game->moves[i]);
  }
}

/*
In analyze_move, stockfish already has the position, so we just need to send the "go movetime 1000" command to let it evaluate all the legal moves for 1 second.
Before we call send_to_stockfish, we first must call set_stockfish_highwater so that we can tell later where the output from this particular command started.
After the send_to_stockfish call we then sleep for the same number of milliseconds that we put in the movetime.
Then we send "stop" to stockfish, just in case it is still producing output.
We wait for 250ms after sending stop, then we call read_from_stockfish to get its output.
Then we pass the move into a further helper function which will parse the lines in cmp.
We call new_output to get the output after the previous highwater mark which was generated by our command, which we also pass into the helper function.
It is not necessary to send flush() to send commands to stockfish; in fact this has nothing to do with stockfish but actually flushes our output space to stdout, so stop calling flush after send_to_stockfish.
*/

void parse_stockfish_output(span output, move *m);
void parse_stockfish_output_2(span output, move *m, spans legal_moves);

void analyze_move(StockfishProcess *sp, move *m) {
  // Set highwater mark for Stockfish output to identify new output generated by this command
  set_stockfish_highwater(sp);

  // Send command to Stockfish to evaluate the position for 1 second
  send_to_stockfish(sp, "go movetime 1000\n");

  // Sleep for 1 second to allow Stockfish to evaluate
  usleep(1000 * 1000); // Sleep for 1000 milliseconds

  // Send "stop" to Stockfish to halt evaluation, in case it's still running
  send_to_stockfish(sp, "stop\n");

  usleep(250 * 1000);

  // Read output from Stockfish
  read_from_stockfish(sp);

  // Get the new output generated by our "go movetime 1000" command
  span output = get_stockfish_new_output(sp);

  // Parse the Stockfish output to extract move evaluations and update the move structure
  parse_stockfish_output(output, m);
}

// Global variable for Stockfish analysis time in milliseconds
int analysis_time_ms = 1000; // Default value

void analyze_move_2(StockfishProcess *sp, move *m) {
  // Set highwater mark for Stockfish output to identify new output generated by this command
  set_stockfish_highwater(sp);

  // Prepare the command string with the global variable for analysis time
  char command[256];
  snprintf(command, sizeof(command), "go movetime %d\n", analysis_time_ms);

  // Send command to Stockfish to evaluate the position for the specified analysis time
  send_to_stockfish(sp, command);

  // Sleep for the specified analysis time to allow Stockfish to evaluate
  usleep(analysis_time_ms * 1000); // Convert milliseconds to microseconds

  // Send "stop" to Stockfish to halt evaluation, in case it's still running
  send_to_stockfish(sp, "stop\n");

  // Increase the wait time after sending stop to ensure all output is captured
  //usleep(250 * 1000); // Wait for an additional 250 milliseconds

  // Read output from Stockfish
  read_from_stockfish(sp);

  // Get the new output generated by our command
  span output = get_stockfish_new_output(sp);

  // Parse the Stockfish output to extract move evaluations and update the move structure
  spans legal_moves = get_legal_lan_moves(sp); /* *** manual fixup *** */
  parse_stockfish_output_2(output, m, legal_moves);
}

/*
In parse_stockfish_output, we are given a span containing the "info" lines like those shown above, e.g.:

info depth 13 seldepth 22 multipv 1 score cp -26 nodes 681621 nps 680260 hashfull 302 tbhits 0 time 1002 pv g8f6 b1c3 e7e6 c1g5 f8e7 e2e3 e8g8 g5h4 f6e4 h4e7 d8e7 d1c2 e4f6 f1d3 d5c4 d3c4

Here is another sample line that indicates a forced checkmate line, in this case, mate in one for the other player if c2a4 is played by this player.

info depth 232 seldepth 3 multipv 3 score mate -1 nodes 754894 nps 5353858 tbhits 0 time 141 pv c2a4 a8a4
info depth 1 seldepth 2 multipv 2 score mate 6 nodes 1258 nps 629000 tbhits 0 time 2 pv c7b7 b1a1

From the info line we need only the cp eval (e.g. -26 above) or mate and the first LAN move after the pv, which identifies the first move in the line.
Since we are evaluating every legal move, there will be one of these for each legal move in the position.
We extract the LAN move into a span, after checking whether it is four or five chars long.
We optimistically assume that we can locate the LAN move by searching for " pv ".

Sometimes we get a cp eval and sometimes we will get a forced checkmate eval like "score mate -7".
To handle both cases we can look for " score ", then check whether it is followed by "cp " or "mate ", and then handle the number after that.
We do not distinguish in our later analysis between mate and extreme centipawn values so we will just replace any mate with a large eval like +10000 or -10000.
This way we can continue to treat the cp eval as an integer downstream of this parsing function.

The input may contain other lines that do not start with "info", which we must ignore.
We can make one pass over all the lines.

First we parse the line and get the first move, in the example above this is g8f6.
We then look in the evals already stored on the move, and see if this is already present.
If it is, we update the cp eval, since the later lines from stockfish override the earlier ones.
If it is not, then we must add it, and also increment the number of move evals.

Before we start, we can allocate the move eval structs, probably 128 is a safe number as we're unlikely to find more legal moves than that in any game.
However, if we do, we should detect it, report it (using prt followed by flush) and then crash.

We declare helper functions for any part of the process that seems potentially involved.
We will have a helper function update_or_add_eval taking a move*, a span for the lan move, and the cp_eval as an int.
*/

void update_or_add_eval(move *m, span lan_move, int cp_eval);

void parse_stockfish_output(span output, move *m) {
  char *line = output.buf;
  char *end = output.end;

  // Prepare for parsing
  m->evals = (MoveEvaluation *)malloc(128 * sizeof(MoveEvaluation));
  if (!m->evals) {
    printf("Memory allocation failed\n");
    exit(EXIT_FAILURE); // Fail loudly on allocation failure
  }
  m->n_evals = 0;

  while (line < end) {
    char *line_end = strchr(line, '\n');
    if (!line_end) break; /* *** manual fixup *** */
    //if (!line_end) line_end = end;

    if (strncmp(line, "info", 4) == 0) {
      char *score_ptr = strstr(line, " score ");
      int cp_eval = 0;
      if (score_ptr && score_ptr < line_end) {
        /* *** score_type is a manual fixup *** */
        char *score_type = strstr(score_ptr, "cp ");
        if (score_type && score_type < line_end) {
          cp_eval = atoi(score_ptr + strlen(" score cp "));
        } else if ((score_type = strstr(score_ptr, "mate ")) && score_type < line_end) {
          // Treat checkmate as large positive or negative value
          int mate_in = atoi(score_ptr + strlen(" score mate "));
          cp_eval = mate_in > 0 ? 10000 : -10000; // Simplified representation for mate
        } else {
          prt("can't parse score: %.*s\n", line_end - line, line);
          continue;
          //flush();exit(1);
        }
      }

      // Locate the first LAN move after "pv"
      char *pv_ptr = strstr(line, " pv ");
      if (pv_ptr) {
        pv_ptr += 4; // Skip " pv "
        int move_length = (*(pv_ptr + 4) == ' ' || *(pv_ptr + 4) == '\n') ? 4 : 5;
        span lan_move = {pv_ptr, pv_ptr + move_length};

        // Update or add eval for the move
        update_or_add_eval(m, lan_move, cp_eval);
      }
    }

    // Move to the start of the next line
    line = line_end + 1;
  }

  // Check for excessive legal moves
  if (m->n_evals > 128) {
    printf("Error: More than 128 legal moves found, exceeding allocation.\n");
    exit(EXIT_FAILURE); // Fail loudly if unexpectedly high number of moves
  }
}

/*
Above was our first implementation of parse_stockfish_output. It eventually worked but required some manual fixups.

Let us rewrite this code using span methods instead of char* C-style strings.
In particular, this will solve the issue that we had in the original code of incorrectly searching (via strstr) past the end of the line that we had found, since strstr obviously looks until the next null byte, which does not exist in our input.

Here we rewrite parse_stockfish_output function as parse_stockfish_output_2 using spans throughout.
Additionally, we had a race condition in the above code where we might be getting stockfish output from the previous position, with moves for the other player.
To handle this, we first call get_legal_lan_moves to get all the legal moves from stockfish in the current position.
Then after find_pv_move when we have the lan move that we're about to add to the evals, before actually adding it we call another helper function to tell us if this span is one of the spans in the legal_moves.
If it isn't, we simply skip it, and this solves the issue with incorrectly adding arrows from the previous half-move.
*/

// Declaration of additional helper functions that might be needed
int parse_cp_eval(span line);
span find_pv_move(span line);

// Assume declaration of get_legal_lan_moves and is_legal_move helper functions
int is_legal_move(span lan_move, spans legal_moves); // Checks if a LAN move is in the legal_moves list

void parse_stockfish_output_2(span output, move *m, spans legal_moves) {

  // Prepare for parsing
  m->evals = (MoveEvaluation *)malloc(128 * sizeof(MoveEvaluation));
  if (!m->evals) {
    prt("Memory allocation failed\n");
    flush();
    exit(EXIT_FAILURE); // Fail loudly on allocation failure
  }
  m->n_evals = 0;

  while (!empty(output)) {
    span line = next_line(&output); // Extract the next line as a span

    if (consume_prefix(&line, S("info"))) {
      int cp_eval = parse_cp_eval(line); // Parse the cp or mate score
      span lan_move = find_pv_move(line); // Find the first LAN move after "pv"

      // Check if the LAN move is legal before updating or adding eval
      if (!empty(lan_move) && is_legal_move(lan_move, legal_moves)) {
        // Update or add eval for the move
        update_or_add_eval(m, lan_move, cp_eval);
      }
    }
  }

  // Check for excessive legal moves
  if (m->n_evals > 128) {
    prt("Error: More than 128 legal moves found, exceeding allocation.\n");
    flush();
    exit(EXIT_FAILURE); // Fail loudly if unexpectedly high number of moves
  }
}

// Wrapper to check if a LAN move is legal.
// Uses is_one_of to search through the legal_moves provided.
int is_legal_move(span lan_move, spans legal_moves) {
    return is_one_of(lan_move, legal_moves);
}

/*
In parse_cp_eval we get a line and parse either a "score cp <int>" or "score mate <int>" out of it.
We convert the forced mate to either + or - 10000 so that they can be treated as cp evals downstream of this function.
We use spanspan to find the location of " score " and indicate failure if it isn't found.
We return the maximally negative int in case the line doesn't contain " score " or can't be parsed for some other reason.
We get back from spanspan the string starting at that point " score " so we skip past that and then we look after this point for "cp " or "move " with consume_prefix() and then handle the int that follows.

Example input lines:

info depth 13 seldepth 22 multipv 1 score cp -26 nodes 681621 nps 680260 hashfull 302 tbhits 0 time 1002 pv g8f6 b1c3 e7e6 c1g5 f8e7 e2e3 e8g8 g5h4 f6e4 h4e7 d8e7 d1c2 e4f6 f1d3 d5c4 d3c4
info depth 232 seldepth 3 multipv 3 score mate -1 nodes 754894 nps 5353858 tbhits 0 time 141 pv c2a4 a8a4
info depth 1 seldepth 2 multipv 2 score mate 6 nodes 1258 nps 629000 tbhits 0 time 2 pv c7b7 b1a1

Correct outputs:

-26
-10000
10000
*/

int parse_cp_eval(span line) {
  span score_span = S(" score ");
  span cp_span = S("cp ");
  span mate_span = S("mate ");
  int fail_val = INT_MIN; // Return maximally negative int on failure

  // Find " score " in the line
  span score_section = spanspan(line, score_span);
  if (empty(score_section)) return fail_val; // " score " not found

  // Move past " score "
  consume_prefix(&score_section, score_span);

  // Check for "cp " or "mate "
  if (consume_prefix(&score_section, cp_span)) {
    // Parse cp value
    return atoi((char *)score_section.buf);
  } else if (consume_prefix(&score_section, mate_span)) {
    // Parse mate value and convert to large cp value
    int mate_in = atoi((char *)score_section.buf);
    return mate_in > 0 ? 10000 : -10000;
  }

  // Parsing failed
  return fail_val;
}

/*
In find_pv_move we search for and move past " pv ".
Then we handle a LAN move which will either be 4 or 5 chars and is followed by a space or possibly a newline.
*/

span find_pv_move(span line) {
  span pv_span = S(" pv ");
  span move;

  // Search for " pv " in the line
  span pv_section = spanspan(line, pv_span);
  if (empty(pv_section)) return nullspan(); // " pv " not found

  // Move past " pv "
  consume_prefix(&pv_section, pv_span);

  // Handle LAN move: it will either be 4 or 5 characters long
  move.buf = pv_section.buf; // Start of the LAN move
  move.end = move.buf + 4; // Assume 4 characters initially

  // Check if the move is actually 5 characters long (4 chars + ' ' or '\n')
  if (*(move.end) != ' ' && *(move.end) != '\n' && (move.end + 1) < pv_section.end) {
    move.end += 1; // Include the fifth character
  }

  return move;
}

/*
Here we get a move and a LAN move as a span, along with the most recent eval from stockfish for that move.

We can do a linear scan over the move evaluations here as N is small.
We simply check if the move is already in the list, and if it is, we update the cp eval.
If not we add it and update the number of evals on the move.
*/

void update_or_add_eval(move *m, span lan_move, int cp_eval) {
  // Linear scan over existing move evaluations
  for (int i = 0; i < m->n_evals; ++i) {
    // Check if the current evaluation matches the LAN move
    if (span_eq(m->evals[i].lan_move, lan_move)) {
      // Update cp eval and return
      m->evals[i].cp_eval = cp_eval;
      return;
    }
  }

  // If the move is not found in the existing evaluations, add a new evaluation
  if (m->n_evals < 128) { // Ensure we don't exceed the allocated space
    m->evals[m->n_evals].lan_move = lan_move;
    m->evals[m->n_evals].cp_eval = cp_eval;
    m->n_evals++; // Increment the count of evaluations
  } else {
    // Handle the unlikely case where there are more evaluations than expected
    prt("Error: Exceeded the maximum number of move evaluations (128).\n");
    flush();
    exit(EXIT_FAILURE);
  }
}

void print_all_move_evals(Game *game) {
  for (int i = 0; i < game->move_count; ++i) {
    move current_move = game->moves[i];
    prt("Move %d: %.*s\n", i + 1, current_move.lan.end - current_move.lan.buf, current_move.lan.buf);
    prt("Legal Moves from this position:\n");

    for (int j = 0; j < current_move.n_evals; ++j) {
      MoveEvaluation eval = current_move.evals[j];
      prt("  LAN Move: %.*s, CP Eval: %d\n", eval.lan_move.end - eval.lan_move.buf, eval.lan_move.buf, eval.cp_eval);
    }
  }
}

void produce_output(Game *game);

/*
To produce the PGN output, we already have a game object which contains, for each position reached in the game, the list of each legal move along with our eval from stockfish.
First we iterate over the tags on the game object and reproduce them one per line, each enclosed in square brackets.
Then we output a blank line (with terpri()) to indicate the tag section is ended.

Next we iterate over the moves, and output the move number starting with "1." and so on, followed by the SAN move which we already have, then a comment which we generate, which will contain arrows.
An example PGN comment containing arrows is: { [%cal Gb8d7,Ga5b4,Gf6e4,Gf6h5,Gf6d7] }
We generate the arrows by taking our move evals and interpreting them according to the following rules:

To determine whether the move is for white or black, we must use the loop iteration variable, as we do not store this information on the move struct explicitly.
To get the move number we can also simply integer-divide the iteration counter by 2 and add one.

We find the highest cp_eval for any move in the position to determine if the position is winning, drawn, or losing.
Then we draw arrows which are green for every legal move that maintains the win, if we consider it a win, or maintains the draw if we consider it a draw.
If the position is losing, we will not draw any arrows, because all moves are equivalent in BPC terms in that case.
We will draw red arrows for every move that changes from a win to a draw or a win to a loss.

We will convert between cp_eval numbers and categorical win, loss, or draw values according to thresholds, currently:

in range [-150, 150]: drawn
greater than 150: winning
less than -150: losing

Note that the cp_eval numbers are always in terms relative to the player who's turn it is, so we don't have to consider who has the move in interpreting these 
numbers.

Since our output just goes to stdout, we simply use prt() to generate our output PGN as described above.
As usual, when we get invalid input or cannot proceed, we simply print a message (using prt() followed by flush()) and exit the process.

After each move (or half move) we just print a space, to keep the PGN output compact, as newlines are not required.
We do include one newline at the end, though, so that our output ends with a newline as a text file always should.
*/

typedef enum { WINNING, DRAWN, LOSING } position_evaluation;
position_evaluation evaluate_position(int cp_eval);
void print_move_arrows(move *m);

void produce_output(Game *game) {
  // Iterate over the tags and reproduce them
  for (int i = 0; i < game->tag_count; ++i) {
    prt("[%.*s]\n", game->tags[i].end - game->tags[i].buf, game->tags[i].buf);
  }
  terpri();

  // Iterate over the moves and output them
  for (int i = 0; i < game->move_count; ++i) {
    move *current_move = &game->moves[i];
    int move_number = (i / 2) + 1;
    if (i % 2 == 0) { // White's move
      prt("%d. ", move_number);
    } else { // Black's move, no move number needed, just a space
      prt(" ");
    }

    // Output the SAN move
    prt("%.*s ", current_move->san.end - current_move->san.buf, current_move->san.buf);

    // Generate and print arrows based on move evaluations
    print_move_arrows(current_move);

    // Print a space after each move or half-move
    prt(" ");
  }

  terpri();
  flush(); // Ensure all output is printed
}

void produce_output_2(Game *game);

void produce_output_2(Game *game) {
  // Iterate over the tags and reproduce them
  for (int i = 0; i < game->tag_count; ++i) {
    prt("[%.*s]\n", game->tags[i].end - game->tags[i].buf, game->tags[i].buf);
  }
  terpri();

  // Iterate over the moves and output them
  for (int i = 0; i < game->move_count; ++i) {
    move *current_move = &game->moves[i];
    int move_number = (i / 2) + 1;

    // Generate and print arrows based on move evaluations
    print_move_arrows(current_move);

    if (i % 2 == 0) { // White's move
      prt("%d. ", move_number);
    } else { // Black's move, no move number needed, just a space
      prt(" ");
    }

    // Output the SAN move
    prt("%.*s ", current_move->san.end - current_move->san.buf, current_move->san.buf);

    // Print a space after each move or half-move
    // actually don't need this I think -- Ed.
    //prt(" ");
  }

  terpri();
  flush(); // Ensure all output is printed
}

position_evaluation evaluate_position(int cp_eval) {
  if (cp_eval > 150) return WINNING;
  else if (cp_eval < -150) return LOSING;
  else return DRAWN;
}

/*
In print_move_arrows we are given a move and we must determine first what we consider the BPC (big-picture color) of the position to be.
To do this we iterate over all the move evals in the position and find the highest cp_eval of any move.
We call evaluate_position with this max value and store the result.

If the position is considered losing (i.e. the best move is classified as LOSING by our evaluation function) then we don't print any arrows, and just return.
This is to avoid printing green arrows for each move in a losing position.

Then we iterate over each of the legal moves, i.e. each of the move_evals, and for each one we again get a categorical evaluation by calling evaluate_position.
If the categorical eval is equal to the best possible one that we got for the highest-eval move, we consider this a non-error move and draw a green arrow.
Otherwise we consider the move an error and draw a red arrow.

To "draw" the arrows, we output our PGN comment containing square brackets with the "%cal" tag.
We must double the percent sign in the call to prt, as it uses the same format string syntax as printf.
For each arrow, we have either "R" or "G" followed by the LAN move, which is already given on the move_eval.
Concatenating the letter for the color with the LAN move consisting of the start and destination squares yields a valid arrow in the form that "%cal" uses.
The arrows themselves are then separated by commas inside the whole "%cal" thing.

An example PGN comment containing arrows is: { [%cal Gb8d7,Ga5b4,Gf6e4,Gf6h5,Gf6d7,Rg6g7] }
*/

void print_move_arrows(move *m) {
  // Determine the best possible categorization (BPC) of the position
  int max_cp_eval = -10000; // Start with a very low value
  for (int i = 0; i < m->n_evals; ++i) {
    if (m->evals[i].cp_eval > max_cp_eval) {
      max_cp_eval = m->evals[i].cp_eval;
    }
  }
  position_evaluation bpc = evaluate_position(max_cp_eval);

  if (bpc == LOSING) return; /* *** manual fixup *** */

  // Start printing the comment containing arrows
  prt("{ [%%cal ");

  for (int i = 0; i < m->n_evals; ++i) {
    position_evaluation move_eval = evaluate_position(m->evals[i].cp_eval);

    // Determine arrow color based on comparison with BPC
    char arrow_color = (move_eval == bpc) ? 'G' : 'R'; // Green for maintaining BPC, Red for error

    // "Draw" the arrow for the move
    prt("%c%.*s", arrow_color, m->evals[i].lan_move.end - m->evals[i].lan_move.buf, m->evals[i].lan_move.buf);

    // Separate arrows with commas, but not after the last arrow
    if (i < m->n_evals - 1) {
      prt(",");
    }
  }

  // Close the comment
  prt("] }");
}

/*
In print_positions(Game*) we print out each half-move as a number followed by one or three dots, a space, a SAN move, a FEN string, and a newline.
This is mainly used to fetch FEN strings for any given position in a game for further use with Stockfish.

We assume that populate_lan_moves has been called first, but since the FEN strings aren't stored above, we call get_fen_from_stockfish() for each position.
*/

void print_positions(Game *game, StockfishProcess *sp);

void print_positions(Game *game, StockfishProcess *sp) {
  char fen[256]; // Buffer to hold FEN string
  
  // Iterate over each move in the game
  for (int i = 0; i < game->move_count; ++i) {
    // Set the position in Stockfish up to this move
    send_position(sp, game, i + 1);

    // Get the current position as FEN from Stockfish
    get_fen_from_stockfish(sp, fen, sizeof(fen));

    // Print move number and dots
    if (i % 2 == 0) { // White's move
      prt("%d. ", (i / 2) + 1);
    } else { // Black's move
      prt("%d... ", (i / 2) + 1);
    }

    // Print SAN move and FEN string
    prt("%.*s %s\n", game->moves[i].san.end - game->moves[i].san.buf, game->moves[i].san.buf, fen);
  }
}

#define MAX_SPANS (1 << 20)

int main(int argc, char *argv[]) {
  // This could come from parsing a command line flag like --print-fen, but just set manually for now.
  int just_print_fen = 0;

  init_spans(); // Initialize your spans and buffers
  span_arena_alloc(MAX_SPANS);

  /*
  We have a global variable analysis_time_ms, and we want to be able to set this from the command line. Write a few lines here to handle argc and argv and update this variable if a corresponding flag is provided, otherwise we will leave it set to the default (which was already initialized above).

  We also have just_print_fen which is a debugging feature, but we can support this with a command line flag as well, if the flag is present we can set this to 1 and we'll get the debugging output with the FEN position for each move.

  We also have --help which prints a short usage summary, using prt(), flush(), and exit(0).

  For the command-line flags we use "--analysis-time", "--just-print-fen", and of course "--help".
  */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--analysis-time") == 0 && i + 1 < argc) {
      analysis_time_ms = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--just-print-fen") == 0) {
      just_print_fen = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      prt("Usage: %s [OPTIONS]\n", argv[0]);
      prt("--analysis-time <milliseconds>     Set analysis time in milliseconds.\n");
      prt("--just-print-fen                   Print FEN positions for debugging.\n");
      prt("--help                             Display this help message.\n");
      flush();
      exit(0);
    }
  }

  read_and_count_stdin(); // Read the PGN data into the inp span

  Game game = {0};

  debug_mode = 0; // Set to 0 to disable debugging output
  parse_pgn(&inp, &game);

  // Print the moves from the parsed game
  //print_game(game);

  // Rest of the main function, including Stockfish process handling
  StockfishProcess sp;
  launch_stockfish(&sp); // Launch and communicate with Stockfish
  // Send command to Stockfish, etc.

  // Send command to Stockfish
  send_to_stockfish(&sp, "uci\n");
  send_to_stockfish(&sp, "setoption name MultiPV value 500\n");
  send_to_stockfish(&sp, "ucinewgame\n");
  
  populate_lan_moves(&game, &sp);

  if (just_print_fen) {
    // just print the FEN strings and moves for easier debugging via manual Stockfish input
    print_positions(&game, &sp);
  } else {
    // do the normal analysis

    //dbgd(game.move_count);
    //for (int i=0;i < game.move_count;i++) {
      //wrs(game.moves[i].lan);terpri();
    //}

    // Now we actually do the analysis, for each position reached.
    do_analysis(&game, &sp);

    //print_all_move_evals(&game);

    produce_output_2(&game);
  }

  // Cleanup for Stockfish process
  close(sp.to_stockfish[1]);
  close(sp.from_stockfish[0]);
  waitpid(sp.pid, NULL, 0); // Wait for Stockfish to exit

  flush(); // Ensure all output is written
  span_arena_free();
  return 0;
}

