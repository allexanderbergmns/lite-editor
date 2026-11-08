#include <SDL2/SDL.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "rencache.h"
#ifdef _WIN32
  #include <windows.h>
  #include <shlobj.h>
#endif

extern SDL_Window *window;

static const char* button_name(int button) {
  switch (button) {
    case 1  : return "left";
    case 2  : return "middle";
    case 3  : return "right";
    default : return "?";
  }
}

static char* key_name(char *dst, int sym) {
  strcpy(dst, SDL_GetKeyName(sym));
  char *p = dst;
  while (*p) {
    *p = tolower(*p);
    p++;
  }
  return dst;
}

static int f_poll_event(lua_State *L) {
  char buf[16];
  int mx, my, wx, wy;
  SDL_Event e;

top:
  if ( !SDL_PollEvent(&e) ) {
    return 0;
  }

  switch (e.type) {
    case SDL_QUIT:
      lua_pushstring(L, "quit");
      return 1;

    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
        lua_pushstring(L, "resized");
        lua_pushnumber(L, e.window.data1);
        lua_pushnumber(L, e.window.data2);
        return 3;
      } else if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
        rencache_invalidate();
        lua_pushstring(L, "exposed");
        return 1;
      }
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        SDL_FlushEvent(SDL_KEYDOWN);
      }
      goto top;

    case SDL_DROPFILE:
      SDL_GetGlobalMouseState(&mx, &my);
      SDL_GetWindowPosition(window, &wx, &wy);
      lua_pushstring(L, "filedropped");
      lua_pushstring(L, e.drop.file);
      lua_pushnumber(L, mx - wx);
      lua_pushnumber(L, my - wy);
      SDL_free(e.drop.file);
      return 4;

    case SDL_KEYDOWN:
      lua_pushstring(L, "keypressed");
      lua_pushstring(L, key_name(buf, e.key.keysym.sym));
      return 2;

    case SDL_KEYUP:
      lua_pushstring(L, "keyreleased");
      lua_pushstring(L, key_name(buf, e.key.keysym.sym));
      return 2;

    case SDL_TEXTINPUT:
      lua_pushstring(L, "textinput");
      lua_pushstring(L, e.text.text);
      return 2;

    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == 1) { SDL_CaptureMouse(1); }
      lua_pushstring(L, "mousepressed");
      lua_pushstring(L, button_name(e.button.button));
      lua_pushnumber(L, e.button.x);
      lua_pushnumber(L, e.button.y);
      lua_pushnumber(L, e.button.clicks);
      return 5;

    case SDL_MOUSEBUTTONUP:
      if (e.button.button == 1) { SDL_CaptureMouse(0); }
      lua_pushstring(L, "mousereleased");
      lua_pushstring(L, button_name(e.button.button));
      lua_pushnumber(L, e.button.x);
      lua_pushnumber(L, e.button.y);
      return 4;

    case SDL_MOUSEMOTION:
      lua_pushstring(L, "mousemoved");
      lua_pushnumber(L, e.motion.x);
      lua_pushnumber(L, e.motion.y);
      lua_pushnumber(L, e.motion.xrel);
      lua_pushnumber(L, e.motion.yrel);
      return 5;

    case SDL_MOUSEWHEEL:
      lua_pushstring(L, "mousewheel");
      lua_pushnumber(L, e.wheel.y);
      return 2;

    default:
      goto top;
  }

  return 0;
}

static int f_wait_event(lua_State *L) {
  double n = luaL_checknumber(L, 1);
  lua_pushboolean(L, SDL_WaitEventTimeout(NULL, n * 1000));
  return 1;
}

static SDL_Cursor* cursor_cache[SDL_SYSTEM_CURSOR_HAND + 1];

static const char *cursor_opts[] = {
  "arrow",
  "ibeam",
  "sizeh",
  "sizev",
  "hand",
  NULL
};

static const int cursor_enums[] = {
  SDL_SYSTEM_CURSOR_ARROW,
  SDL_SYSTEM_CURSOR_IBEAM,
  SDL_SYSTEM_CURSOR_SIZEWE,
  SDL_SYSTEM_CURSOR_SIZENS,
  SDL_SYSTEM_CURSOR_HAND
};

static int f_set_cursor(lua_State *L) {
  int opt = luaL_checkoption(L, 1, "arrow", cursor_opts);
  int n = cursor_enums[opt];
  SDL_Cursor *cursor = cursor_cache[n];
  if (!cursor) {
    cursor = SDL_CreateSystemCursor(n);
    cursor_cache[n] = cursor;
  }
  SDL_SetCursor(cursor);
  return 0;
}

static int f_set_window_title(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
  SDL_SetWindowTitle(window, title);
  return 0;
}

static const char *window_opts[] = { "normal", "maximized", "fullscreen", 0 };
enum { WIN_NORMAL, WIN_MAXIMIZED, WIN_FULLSCREEN };

static int f_set_window_mode(lua_State *L) {
  int n = luaL_checkoption(L, 1, "normal", window_opts);
  SDL_SetWindowFullscreen(window,
    n == WIN_FULLSCREEN ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  if (n == WIN_NORMAL) { SDL_RestoreWindow(window); }
  if (n == WIN_MAXIMIZED) { SDL_MaximizeWindow(window); }
  return 0;
}

static int f_window_has_focus(lua_State *L) {
  unsigned flags = SDL_GetWindowFlags(window);
  lua_pushboolean(L, flags & SDL_WINDOW_INPUT_FOCUS);
  return 1;
}

static int f_show_confirm_dialog(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
  const char *msg = luaL_checkstring(L, 2);

#if _WIN32
  int id = MessageBox(0, msg, title, MB_YESNO | MB_ICONWARNING);
  lua_pushboolean(L, id == IDYES);

#else
  SDL_MessageBoxButtonData buttons[] = {
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
  };
  SDL_MessageBoxData data = {
    .title = title,
    .message = msg,
    .numbuttons = 2,
    .buttons = buttons,
  };
  int buttonid;
  SDL_ShowMessageBox(&data, &buttonid);
  lua_pushboolean(L, buttonid == 1);
#endif
  return 1;
}

static int f_chdir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  int err = chdir(path);
  if (err) { luaL_error(L, "chdir() failed"); }
  return 0;
}

static int f_list_dir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);

  DIR *dir = opendir(path);
  if (!dir) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_newtable(L);
  int i = 1;
  struct dirent *entry;
  while ( (entry = readdir(dir)) ) {
    if (strcmp(entry->d_name, "." ) == 0) { continue; }
    if (strcmp(entry->d_name, "..") == 0) { continue; }
    lua_pushstring(L, entry->d_name);
    lua_rawseti(L, -2, i);
    i++;
  }

  closedir(dir);
  return 1;
}

static int f_absolute_path(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);

#ifdef _WIN32
  char path_buf[MAX_PATH];
  if (!_fullpath(path_buf, path, MAX_PATH)) {
    return 0;
  }
  lua_pushstring(L, path_buf);
#else
  char *res = realpath(path, NULL);
  if (!res) { return 0; }
  lua_pushstring(L, res);
  free(res);
#endif
  return 1;
}

static int f_get_file_info(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);

  struct stat s;
  int err = stat(path, &s);
  if (err < 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_newtable(L);
  lua_pushnumber(L, s.st_mtime);
  lua_setfield(L, -2, "modified");

  lua_pushnumber(L, s.st_size);
  lua_setfield(L, -2, "size");

  if (S_ISREG(s.st_mode)) {
    lua_pushstring(L, "file");
  } else if (S_ISDIR(s.st_mode)) {
    lua_pushstring(L, "dir");
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, -2, "type");

  return 1;
}

static int f_get_clipboard(lua_State *L) {
  char *text = SDL_GetClipboardText();
  if (!text) { return 0; }
  lua_pushstring(L, text);
  SDL_free(text);
  return 1;
}

static int f_set_clipboard(lua_State *L) {
  const char *text = luaL_checkstring(L, 1);
  SDL_SetClipboardText(text);
  return 0;
}

static int f_get_time(lua_State *L) {
  double n = SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency();
  lua_pushnumber(L, n);
  return 1;
}

static int f_sleep(lua_State *L) {
  double n = luaL_checknumber(L, 1);
  SDL_Delay(n * 1000);
  return 0;
}

static int f_exec(lua_State *L) {
  size_t len;
  const char *cmd = luaL_checklstring(L, 1, &len);
  char *buf = malloc(len + 32);
  if (!buf) { luaL_error(L, "buffer allocation failed"); }
#if _WIN32
  sprintf(buf, "cmd /c \"%s\"", cmd);
  WinExec(buf, SW_HIDE);
#else
  sprintf(buf, "%s &", cmd);
  int res = system(buf);
  (void) res;
#endif
  free(buf);
  return 0;
}

static int f_fuzzy_match(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  const char *ptn = luaL_checkstring(L, 2);
  int score = 0;
  int run = 0;

  while (*str && *ptn) {
    while (*str == ' ') { str++; }
    while (*ptn == ' ') { ptn++; }
    if (tolower(*str) == tolower(*ptn)) {
      score += run * 10 - (*str != *ptn);
      run++;
      ptn++;
    } else {
      score -= 10;
      run = 0;
    }
    str++;
  }
  if (*ptn) { return 0; }

  lua_pushnumber(L, score - (int) strlen(str));
  return 1;
}


static int f_show_open_folder_dialog(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
#if _WIN32
  BROWSEINFO bi = {0};
  bi.lpszTitle = title;
  bi.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
  LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
  if (!pidl) { return 0; }
  char path[MAX_PATH];
  if (SHGetPathFromIDList(pidl, path)) {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }
  CoTaskMemFree(pidl);
#else
  SDL_MessageBoxButtonData buttons[] = {
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open" },
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
  };
  SDL_MessageBoxData data = {
    .title = title,
    .message = "Please use the system file manager to choose a folder.",
    .numbuttons = 2,
    .buttons = buttons,
  };
  int buttonid;
  SDL_ShowMessageBox(&data, &buttonid);
  lua_pushnil(L);
  (void) buttonid;
#endif
  return 1;
}


static int f_show_open_folder_dialog_native(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);

#if _WIN32
  BROWSEINFO bi = {0};
  bi.lpszTitle = title;
  bi.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
  LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
  if (!pidl) { return 0; }
  char path[MAX_PATH];
  if (SHGetPathFromIDList(pidl, path)) {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }
  CoTaskMemFree(pidl);

#elif __APPLE__
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSSavePanel *panel = [NSSavePanel savePanel];
  [panel setTitle:[NSString stringWithUTF8String:title]];
  [panel setCanCreateDirectories:YES];
  [panel setDirectoryURL:[NSURL fileURLWithPath:NSHomeDirectory() isDirectory:YES]];
  [panel setPrompt:@"Choose"];
  [panel setAllowedFileTypes:@[@""]];
  if ([panel runModal] == NSModalResponseOK) {
    NSString *selected = [[panel URL] path];
    if (selected) {
      const char *utf8 = [selected UTF8String];
      lua_pushstring(L, utf8);
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }
  [pool drain];

#elif __linux__
  /*
   ** On Linux we do not initialise GTK in this process, so we cannot host the
   ** folder chooser here. Instead we spawn a tiny GTK4 helper process that
   ** shows a native folder chooser and writes the chosen path to stdout.
   */
  char tmp_path[64];
  int tmp_fd = -1;

  {
    const char *template = "/tmp/lite_folder_XXXXXX";
    tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) { goto linux_native_fail; }
    close(tmp_fd);
    tmp_fd = -1;
  }

  {
    char exe_buf[4096];
    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (n <= 0) { unlink(tmp_path); goto linux_native_fail; }
    exe_buf[n] = '\0';

    char argv_buf[8192];
    const char *home_dir = getenv("HOME");
    const char *start_dir = home_dir ? home_dir : "/home";
    int argv_len = snprintf(argv_buf, sizeof(argv_buf),
        "%s --lite-folder-chooser %s %s %s", exe_buf, tmp_path,
        title ? title : "Choose Folder", start_dir);
    if (argv_len < 0 || (size_t)argv_len >= sizeof(argv_buf)) {
      unlink(tmp_path);
      goto linux_native_fail;
    }

    pid_t pid = fork();
    if (pid < 0) {
      unlink(tmp_path);
      goto linux_native_fail;
    }
    if (pid == 0) {
      execl("/bin/sh", "sh", "-c", argv_buf, (char *)NULL);
      _exit(127);
    }

    int status = 0;
    pid_t ret = waitpid(pid, &status, 0);
    if (ret != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      unlink(tmp_path);
      goto linux_native_fail;
    }
  }

  {
    FILE *f = fopen(tmp_path, "r");
    if (!f) { unlink(tmp_path); goto linux_native_fail; }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) {
      fclose(f);
      unlink(tmp_path);
      goto linux_native_fail;
    }
    fclose(f);
    unlink(tmp_path);

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      buf[--len] = '\0';
    }
    if (len == 0) { goto linux_native_fail; }

    char *real = realpath(buf, NULL);
    if (real) {
      lua_pushstring(L, real);
      free(real);
    } else {
      lua_pushstring(L, buf);
    }
    return 1;
  }

linux_native_fail:
  lua_pushnil(L);
  return 1;

#else
  SDL_MessageBoxButtonData buttons[] = {
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open" },
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
  };
  SDL_MessageBoxData data = {
    .title = title,
    .message = "Please use the system file manager to choose a folder.",
    .numbuttons = 2,
    .buttons = buttons,
  };
  int buttonid;
  SDL_ShowMessageBox(&data, &buttonid);
  lua_pushnil(L);
  (void) buttonid;
#endif
  return 1;
}

static const luaL_Reg lib[] = {
  { "poll_event",              f_poll_event              },
  { "wait_event",              f_wait_event              },
  { "set_cursor",              f_set_cursor              },
  { "set_window_title",        f_set_window_title        },
  { "set_window_mode",         f_set_window_mode         },
  { "window_has_focus",        f_window_has_focus        },
  { "show_confirm_dialog",     f_show_confirm_dialog     },
  { "show_open_folder_dialog",          f_show_open_folder_dialog          },
  { "show_open_folder_dialog_native",   f_show_open_folder_dialog_native   },
  { "chdir",                            f_chdir                            },
  { "list_dir",                f_list_dir                },
  { "absolute_path",           f_absolute_path           },
  { "get_file_info",           f_get_file_info           },
  { "get_clipboard",           f_get_clipboard           },
  { "set_clipboard",           f_set_clipboard           },
  { "get_time",                f_get_time                },
  { "sleep",                   f_sleep                   },
  { "exec",                    f_exec                    },
  { "fuzzy_match",             f_fuzzy_match             },
  { NULL, NULL }
};

int luaopen_system(lua_State *L) {
  luaL_newlib(L, lib);
  return 1;
}
