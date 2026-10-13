// Test builds (make AUTOPLAY=1): the game plays itself through the title, a ride, the initials and the settings, and
// saves a screenshot of each to <save dir>/shots/ as .ppm, then quits. test/wii-shots.sh runs it in Dolphin.
static int apFrame, apShot, apPhase; static double apEnd = -1;
static void apSave(const char *name) {
  static unsigned char rgb[640 * 576 * 3]; int w, h; gfx_shot(rgb, &w, &h);
  char p[256]; snprintf(p, sizeof p, "%s/shots", saveDir); mkdirs(p);
  snprintf(p, sizeof p, "%s/shots/%02d-%s.ppm", saveDir, apShot++, name);
  FILE *f = fopen(p, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(rgb, 1, w * h * 3, f); fclose(f);
}
u32 autoplay(u32 h, double *sy) {
  int f = apFrame;
  if (apPhase == 0) {                                              // the title, then RIDE
    if (f == 100) return KEY_A;
    return 0;
  }
  if (apPhase == 1 && st == ST_PLAY) {                             // ride: keep to the middle and near the start line, then let go
    roadAt(g.cam + g.bx); *sy = clampd((RC - g.y) / 12, -1, 1);
    if (f < apEnd) return g.bx < START_BX + 20 ? KEY_GO : 0;
    return 0;
  }
  if (apPhase == 2) {                                              // initials: C, A, T
    static const u32 seq[] = { KEY_DOWN, KEY_DOWN, KEY_A, KEY_A, KEY_UP, KEY_A };
    int k = (f - 60) / 12; if (f >= 60 && (f - 60) % 12 == 0 && k < 6) return seq[k];
    return 0;
  }
  if (apPhase == 4 && f > 20 && f < 140) return KEY_SELECT;        // hold - on the title: settings
  if (apPhase == 5 && f == 40) return KEY_DOWN;
  return 0;
}
static void autoplay_frame(void) {
  apFrame++;
  switch (apPhase) {
    case 0: if (apFrame == 90) apSave("title"); if (screen == SC_PLAY) { apPhase = 1; apFrame = 0; apEnd = 60 * 12; } break;
    case 1:
      if (apFrame == 60 * 4) apSave("riding");
      if (apFrame == 60 * 9) apSave("riding-later");
      if (screen == SC_ENTRY) { apPhase = 2; apFrame = 0; }
      else if (screen == SC_OVER) { apPhase = 3; apFrame = 0; }
      break;
    case 2: if (apFrame == 40) apSave("initials"); if (apFrame == 100) apSave("initials-typed"); if (screen == SC_OVER) { apPhase = 3; apFrame = 0; } break;
    case 3: if (apFrame == 60) { apSave("game-over"); toTitle(); apPhase = 4; apFrame = 0; } break;
    case 4: if (apFrame == 10) apSave("title-board"); if (screen == SC_SETTINGS) { apPhase = 5; apFrame = 0; } break;
    case 5: if (apFrame == 60) { apSave("settings"); save(); SYS_Report("AUTOPLAY DONE sound buffers %d, gaps %d\n", audio_played(), audio_gaps()); for (;;) VIDEO_WaitVSync(); } break;
  }
}
