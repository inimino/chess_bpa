# Chess Big-Picture Analysis
# Project goals

This tool lets us analyze chess games in terms of sets of moves that are equivalent, which is a complementary perspective to that of Stockfish or another engine.
Instead of focusing on the best move or the one right move in a position, we want to think in objective terms about all the moves that are equally good.
For example, if the game is a draw, then from a logical perspective, any move that maintains the draw is equally good.
Of course, one move might be an immediate draw while another move could require a long and very difficult line to reach a draw.
So from a human perspective, not all drawn moves (or all winning moves) are equal, which is why we tend to focus on "best" moves.
However, looking at moves in sets can inspire us to think about moves that we might not otherwise and also lets us see everything that is going on in a position, presenting more information than simply the "best" move according to an engine.

YouTube playlist about this tool [here](https://www.youtube.com/playlist?list=PLjma_kMa78BOuUN5gsud0k0igNy8uih_m).

To use the tool you provide a PGN file on stdin and it produces an annotated file on stdout, which you can view using standard chess tools (Lichess.org is recommended).

All the code is written by GPT4 based on comments written by the human author.
For more on this development style, see [cmpr](https://github.com/inimino/cmpr).

# Usage

There are no dependencies and all the code is in one file.
Compile with `gcc -o bpa bpa.c`.
You will need stockfish installed and in your PATH, i.e. you should be able to run "stockfish" at the terminal.
This is how bpa will launch stockfish, so if you can run it then the tool will also be able to run it.

Perform analysis like `bpa < input_game.pgn > output_annotated.pgn`.
The tool is a filter that takes PGN input on stdin and responds on stdout.
The tool will analyze only the main line of the game (not variations) and add its annotations as arrows.
You can then open the resulting PGN on lichess or some other chess software and you will see the arrows.

You can use `--analysis-time <ms>` to change the default (from 1000 = 1s).
This is the amount of time we let stockfish consider each position, so for example in a game with 43 moves and the default setting, the analysis will run for about 86 seconds (since there's one position for each player per move).

# TODO

- support PGN files with multiple games (currently only one game per PGN is supported)
  - more robust PGN parsing, suitable for use as a database tool
  - query by Elo range, player name, opening, etc
- code cleanup adapting to use with cmpr (continuous as code is touched for bugfixes or features)
- adaptive time for Stockfish eval - aim for fixed error rate, not fixed time per position - target total analysis time per game, or accuracy, rather than fixed limit
- optional depth-1 analysis, e.g. add a number to the arrows (unclear how to present this info)
- roundtrip other comments and variations
- add option to analyze variations as well

# Useful development resources

- ./sample.pgn
- ./sample[12].png
- /usr/share/doc/stockfish/engine-interface.txt.gz
- https://github.com/official-stockfish/Stockfish/wiki/UCI-&-Commands#standard-commands
- https://backscattering.de/chess/uci/
- https://www.chessprogramming.org/Portable_Game_Notation
- https://www.thechessdrum.net/PGN_Reference.txt
- https://github.com/fsmosca/PGN-Standard/blob/master/PGN-Standard.txt
