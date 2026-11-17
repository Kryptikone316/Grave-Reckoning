#include "music.h"
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (music_init(argc > 1 ? argv[1] : "Music") != 0) { fprintf(stderr, "init failed\n"); return 1; }
    fprintf(stderr, "== group 0 for 1.2 s\n"); usleep(1200000);
    fprintf(stderr, "== ask for group 3\n"); music_set_group(3); usleep(2000000);
    fprintf(stderr, "== ask for group 3 again (no change expected)\n"); music_set_group(3); usleep(1000000);
    fprintf(stderr, "== ask for group 6\n"); music_set_group(6); usleep(2000000);
    fprintf(stderr, "== ask for group 9 (invalid -> menu)\n"); music_set_group(9); usleep(2000000);
    music_deinit();
    return 0;
}
