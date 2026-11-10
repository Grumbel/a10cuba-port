/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* SDL3 front end for the i386 PE runner. It starts the runner (through
 * qemu-i386 unless --no-qemu is given), shows the 8-bit frames the runner
 * writes to a pipe, and sends keyboard/mouse state back on a second pipe. */

#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum { INPUT_KEY = 1, INPUT_MOVE = 2, INPUT_BUTTON = 3, MAX_FRAME = 4096 };

static int read_full(int fd, void *buffer, size_t size)
{
    unsigned char *p = buffer;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return 0;
        }
        p += n;
        size -= (size_t)n;
    }
    return 1;
}

/* Translate an SDL keycode to a Windows virtual-key code (0 if unmapped). */
static unsigned virtual_key(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z') return (unsigned)(key - 'a' + 'A');
    if (key >= '0' && key <= '9') return (unsigned)key;
    if (key >= SDLK_F1 && key <= SDLK_F12) return 0x70u + (unsigned)(key - SDLK_F1);
    switch (key) {
    case SDLK_BACKSPACE: return 0x08;  case SDLK_TAB: return 0x09;
    case SDLK_RETURN: return 0x0d;     case SDLK_ESCAPE: return 0x1b;
    case SDLK_SPACE: return 0x20;      case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;   case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;       case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;         case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;       case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;     case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_MINUS: return 0xbd;      case SDLK_EQUALS: return 0xbb;
    case SDLK_COMMA: return 0xbc;      case SDLK_PERIOD: return 0xbe;
    case SDLK_SLASH: return 0xbf;      case SDLK_SEMICOLON: return 0xba;
    case SDLK_LEFTBRACKET: return 0xdb; case SDLK_RIGHTBRACKET: return 0xdd;
    case SDLK_BACKSLASH: return 0xdc;  case SDLK_APOSTROPHE: return 0xde;
    case SDLK_GRAVE: return 0xc0;
    case SDLK_KP_0: return 0x60;       case SDLK_KP_1: return 0x61;
    case SDLK_KP_2: return 0x62;       case SDLK_KP_3: return 0x63;
    case SDLK_KP_4: return 0x64;       case SDLK_KP_5: return 0x65;
    case SDLK_KP_6: return 0x66;       case SDLK_KP_7: return 0x67;
    case SDLK_KP_8: return 0x68;       case SDLK_KP_9: return 0x69;
    case SDLK_KP_PLUS: return 0x6b;    case SDLK_KP_MINUS: return 0x6d;
    default: return 0;
    }
}

static void send_input(int fd, uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t rec[4] = {type, a, b, 0};
    if (write(fd, rec, sizeof rec) < 0 && errno != EAGAIN) {
        /* The runner has gone away; the frame loop notices at EOF. */
    }
}

static int usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--runner PATH] [--game-dir DIR] [--no-qemu]\n"
            "          [--max-frames N] [--snapshot OUT.bmp] [--scale N]\n"
            "Runs the original A-10 Cuba! binaries from DIR (default\n"
            "./data/disc/A-10Cuba) in the i386 runner and shows them in SDL.\n",
            program);
    return 2;
}

int main(int argc, char **argv)
{
    const char *runner = getenv("A10_PE32_NATIVE_BIN");
    const char *game_dir = getenv("A10_CUBA_DATA_DIR");
    const char *snapshot = NULL;
    int use_qemu = 1, max_frames = 0, scale = 2, frames = 0, status = 1;
    int frame_pipe[2], input_pipe[2];
    pid_t child;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint8_t *pixels = NULL;
    uint32_t *rgba = NULL;
    int width = 0, height = 0;

    runner = runner ? runner : "./build/a10-pe32-native";
    game_dir = game_dir ? game_dir : "./data/disc/A-10Cuba";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--runner") && i + 1 < argc) runner = argv[++i];
        else if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) game_dir = argv[++i];
        else if (!strcmp(argv[i], "--no-qemu")) use_qemu = 0;
        else if (!strcmp(argv[i], "--max-frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else return usage(argv[0]);
    }
    if (scale < 1) scale = 1;

    if (pipe(frame_pipe) < 0 || pipe2(input_pipe, O_NONBLOCK) < 0) {
        perror("pipe");
        return 1;
    }
    /* Keep our ends clear of fds 3 and 4, which the child receives. */
    for (int i = 0; i < 2; i++) {
        int moved = fcntl(frame_pipe[i], F_DUPFD, 10);
        close(frame_pipe[i]);
        frame_pipe[i] = moved;
        moved = fcntl(input_pipe[i], F_DUPFD, 10);
        close(input_pipe[i]);
        input_pipe[i] = moved;
    }
    signal(SIGPIPE, SIG_IGN);
    child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        char exe[4096], dll[4096], fd_out[16], fd_in[16];
        /* The runner side of the input pipe must stay blocking-agnostic. */
        dup2(frame_pipe[1], 3);
        dup2(input_pipe[0], 4);
        close(frame_pipe[0]); close(frame_pipe[1]);
        close(input_pipe[0]); close(input_pipe[1]);
        snprintf(exe, sizeof exe, "%s/A10Cuba.exe", game_dir);
        snprintf(dll, sizeof dll, "%s/A10Sim.dll", game_dir);
        snprintf(fd_out, sizeof fd_out, "3");
        snprintf(fd_in, sizeof fd_in, "4");
        if (use_qemu) {
            execlp("qemu-i386", "qemu-i386", runner, exe, dll, "--run-entry",
                   "--auto-accept", "--frame-fd", fd_out, "--input-fd", fd_in,
                   (char *)NULL);
        } else {
            execl(runner, runner, exe, dll, "--run-entry", "--auto-accept",
                  "--frame-fd", fd_out, "--input-fd", fd_in, (char *)NULL);
        }
        perror("exec runner");
        _exit(127);
    }
    close(frame_pipe[1]);
    close(input_pipe[0]);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        goto done;
    }
    for (;;) {
        uint32_t header[3];
        unsigned char palette[1024];
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                status = 0;
                goto done;
            }
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                unsigned vk = virtual_key(event.key.key);
                if (vk) {
                    send_input(input_pipe[1], INPUT_KEY, vk, event.type == SDL_EVENT_KEY_DOWN);
                }
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && renderer) {
                float x = event.motion.x, y = event.motion.y;
                SDL_RenderCoordinatesFromWindow(renderer, x, y, &x, &y);
                send_input(input_pipe[1], INPUT_MOVE, (uint32_t)(x < 0 ? 0 : x),
                           (uint32_t)(y < 0 ? 0 : y));
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                unsigned vk = event.button.button == SDL_BUTTON_LEFT ? 1
                            : event.button.button == SDL_BUTTON_RIGHT ? 2
                            : event.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                if (vk) {
                    send_input(input_pipe[1], INPUT_BUTTON, vk,
                               event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                }
            }
        }
        if (!read_full(frame_pipe[0], header, sizeof header)) {
            status = frames ? 0 : 1;
            break;
        }
        if (header[0] != 0xa10f0001u || header[1] == 0 || header[2] == 0 ||
            header[1] > MAX_FRAME || header[2] > MAX_FRAME) {
            fprintf(stderr, "sdl_host: bad frame header\n");
            break;
        }
        if ((int)header[1] != width || (int)header[2] != height) {
            width = (int)header[1];
            height = (int)header[2];
            free(pixels); free(rgba);
            pixels = malloc((size_t)width * height);
            rgba = malloc((size_t)width * height * 4);
            if (texture) SDL_DestroyTexture(texture);
            texture = NULL;
            if (!window) {
                window = SDL_CreateWindow("A-10 Cuba!", width * scale, height * scale,
                                          SDL_WINDOW_RESIZABLE);
                renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
                if (!renderer) {
                    fprintf(stderr, "SDL window/renderer: %s\n", SDL_GetError());
                    break;
                }
            }
            SDL_SetRenderLogicalPresentation(renderer, width, height,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, width, height);
            if (!texture || !pixels || !rgba) break;
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        }
        if (!read_full(frame_pipe[0], palette, sizeof palette) ||
            !read_full(frame_pipe[0], pixels, (size_t)width * height)) {
            break;
        }
        for (size_t i = 0; i < (size_t)width * height; i++) {
            const unsigned char *c = palette + pixels[i] * 4;
            rgba[i] = 0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
        }
        SDL_UpdateTexture(texture, NULL, rgba, width * 4);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        frames++;
        if (max_frames && frames >= max_frames) {
            if (snapshot) {
                SDL_Surface *s = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888,
                                                       rgba, width * 4);
                if (s) { SDL_SaveBMP(s, snapshot); SDL_DestroySurface(s); }
            }
            status = 0;
            break;
        }
    }
done:
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    free(pixels); free(rgba);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
