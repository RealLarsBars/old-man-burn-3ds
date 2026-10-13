#!/usr/bin/env python3
"""Turns the web game's achievements.json (copied here from old-man-burn/web/) into source/achdata.c.

There are no coins or leaderboards offline, so the achievements that count them get an offline goal instead
(OFFLINE below). Their names and rewards stay the same as on Discord."""
import json, os, sys

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'achievements.json')
d = json.load(open(src))

def rides(n): return ('rides_500', n, 'Finish %s ride%s of 500 m or more.' % (format(n, ','), '' if n == 1 else 's'))
OFFLINE = {   # stat -> goal -> (new stat, new goal, new description)
  'coins': {n: rides(n) for n in (1, 3, 5, 7, 10, 20, 25, 35, 50, 100)},
  'wins': {1: ('weeks_5k', 1, 'Score 5,000 on a weekly map.'), 3: ('weeks_5k', 3, 'Score 5,000 on 3 different weekly maps.'),
           5: ('weeks_5k', 5, 'Score 5,000 on 5 different weekly maps.')},
  'coins_won': {25: ('pbs', 25, 'Beat your own best on a map 25 times.'), 100: ('pbs', 60, 'Beat your own best on a map 60 times.'),
                1000: ('pbs', 150, 'Beat your own best on a map 150 times.')},
  'daily_wins': {1: ('daily_2k_days', 1, 'Score 2,000 in the Daily Dash.'), 3: ('daily_2k_days', 3, 'Score 2,000 in the Daily Dash on 3 different days.'),
                 10: ('daily_2k_days', 10, 'Score 2,000 in the Daily Dash on 10 different days.')},
  'daily_coins_won': {30: ('daily_1k', 30, 'Score 1,000 in the Daily Dash 30 times.')},
  'podiums': {1: ('old_3k', 1, 'Score 3,000 on an old map.')},
  'halloween_podiums': {1: ('halloween_3k', 1, 'Score 3,000 on a Halloween map.')},
  'christmas_podiums': {1: ('christmas_3k', 1, 'Score 3,000 on a Christmas map.')},
}

def c(s): return json.dumps(s, ensure_ascii=False)
slots = [s['id'] for s in d['slots']]
achs = []
for a in d['achievements']:
  stat, goal, desc = a['stat'], a['goal'], a['desc']
  if stat in OFFLINE:
    if goal not in OFFLINE[stat]: sys.exit('no offline goal for %s (%s %d)' % (a['id'], stat, goal))
    stat, goal, desc = OFFLINE[stat][goal]
  achs.append((a, stat, goal, desc))
stats = []
for _, s, _, _ in achs:
  if s not in stats: stats.append(s)

o = ['// Made by tools/achgen.py from tools/achievements.json. Don\'t edit by hand.', '#include "ach.h"', '']
o.append('const char *const SLOT_IDS[NSLOT] = { %s };' % ', '.join(c(s) for s in slots))
o.append('const char *const SLOT_NAMES[NSLOT] = { %s };' % ', '.join(c(s['name']) for s in d['slots']))
o.append('const char *const STAT_IDS[NSTAT] = { %s };' % ', '.join(c(s) for s in stats))
o.append('const AchItem ITEMS[NITEMS] = {')
for i in d['items']: o.append('  { %d, %s, %s },' % (slots.index(i['slot']), c(i['id']), c(i['name'])))
o.append('};')
o.append('const Ach ACHS[NACH] = {')
for a, s, goal, desc in achs:
  rw = [a['reward']] + (a.get('also') or [])
  rs = ', '.join('{ %d, %s }' % (slots.index(r['slot']), c(r['item'])) for r in rw)
  o.append('  { %s, %s, %s, %d, %d, %d, { %s } },' % (c(a['id']), c(a['name']), c(desc), stats.index(s), goal, len(rw), rs))
o.append('};')
open(os.path.join(here, '..', 'source', 'achdata.c'), 'w').write('\n'.join(o) + '\n')

h = ['// Made by tools/achgen.py. Don\'t edit by hand.', '#pragma once',
     'enum { %s, NSTAT };' % ', '.join('ST_' + s.upper() for s in stats),
     '#define NITEMS %d' % len(d['items']), '#define NACH %d' % len(achs), '#define MAX_REWARDS %d' % max(1 + len(a.get('also') or []) for a in d['achievements'])]
open(os.path.join(here, '..', 'source', 'achdata.h'), 'w').write('\n'.join(h) + '\n')
print('%d items, %d achievements, %d stats' % (len(d['items']), len(achs), len(stats)))
