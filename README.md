# Chess Big-Picture Analysis
# Project goals

The tool should help us explore big picture analysis ideas and test whether they are useful on real games.
Interface will be via PGN files, and we'll use lichess (or any other standard tool) to view the annotations (see sample1.png and 2).

# Requirements

- We want ChatGPT to write all the code.
- Because this is hypothesis testing, we want rapid, rough-and-ready development.

- Input is a PGN file containing a single game as in ./sample.pgn
  - We assume there are no arrows or annotations already in the game file.
- Output should be the same file with added annotations of some kind that show the non-mistakes in each position in the game.
  - Non-mistakes are defined as moves that do not change the big-picture color (defined elsewhere).
  - An example PGN comment containing arrows is: { [%cal Gb8d7,Ga5b4,Gf6e4,Gf6h5,Gf6d7] }
  - We could also annotate only the mistakes with red arrows if desired; in some endgame positions for example we expect there would be fewer mistakes.
  - We can try R G B Y for the colors supported by Lichess at least.

- We will use Stockfish to estimate the BPC for each move in each position in the game, then encode this visually using arrows.
- We will have to decide how to handle borderline cases, by setting some appropriate thresholds, e.g. [-1.5,+1.5] -> drawn.

- We handle the PGN format directly and talk to stockfish via its stdin/stdout text protocol

- To get an eval from stockfish for every move in every position in a game:
  - We set the multipv option to the max value of 500.
  - First we will read the PGN, and for every move generate a "position" string as per UCI.
  - Then we will feed these to stockfish, send "go" for each one, and wait while parsing the info lines which give us the eval for each.
  - When the eval reaches some threshold of stability or a time limit or something (TBD) we will stop and estimate the BPC of each move.
  - Then we will generate the arrow annotations, recreate the PGN format as our output.
  - We may put the numeric eval data in somewhere e.g. to check how the thresholding is working.

- We will first try writing everything in C with no dependencies except our standard framework (spanio, etc).

# Resources

./sample.pgn
./sample[12].png
/usr/share/doc/stockfish/engine-interface.txt.gz
https://github.com/official-stockfish/Stockfish/wiki/UCI-&-Commands#standard-commands
https://backscattering.de/chess/uci/
https://www.chessprogramming.org/Portable_Game_Notation
https://github.com/fsmosca/PGN-Standard/blob/master/PGN-Standard.txt

# Components / Phases

- set up the communication with stockfish ("hello world" stage)
- read the PGN and generate FEN or "position" strings or similar, send everything to stockfish
  - just parse and print them out in SAN format - done
  - add a board representation - provisionally not actually necessary
  - convert to LAN form and send them to stockfish
- parse the stockfish info lines and get the cp eval for each move
- write a function to turn the cp evals into BPC estimates (simple thresholding at first)
- output the modified PGN
