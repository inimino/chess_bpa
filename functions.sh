spanio_prompt() {
  ( cat prompts/spanio_pre
    awk '/END LIBRARY CODE/ {exit} {print}' bpa.c
    cat prompts/spanio_post
  ) | xclip -i -selection clipboard
}

code_prompt() {
  ( echo "Here's our code so far. Reply with \"OK\"."
    echo '```'
    awk '/END LIBRARY CODE/,0' bpa.c | tail -n +2
    echo '```'
  ) | xclip -i -selection clipboard
}

build() {
  gcc -o bpa bpa.c
}
