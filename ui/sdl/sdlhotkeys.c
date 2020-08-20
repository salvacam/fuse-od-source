/* sdlhotkeys.c: Hotkeys processing
   Copyright (c) 2020 Pedro Luis Rodríguez González

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: pl.rguez@gmail.com

*/

#include <config.h>

#include <SDL.h>
#include "settings.h"
#include "ui/ui.h"
#include "ui/hotkeys.h"

#ifdef GCWZERO
static int combo_done;
static SDL_Event *combo_keys[10];
static int last_combo_key = 0;
static int push_combo_event( Uint8* flags );
static int filter_combo_done( const SDL_Event *event );
static int is_combo_possible( const SDL_Event *event );

int is_combo_possible( const SDL_Event *event )
{
  /* There are not combos in widgets or keyboard */
#ifdef VKEYBOARD
  if ( ui_widget_level >= 0 || vkeyboard_enabled ) return 0;
#else
  if ( ui_widget_level >= 0 ) return 0;
#endif

 /* Mot filter if R1 and R1 are mapped to joysticks */
  if ( ( event->key.keysym.sym == SDLK_TAB &&
         ( ( settings_current.joystick_1_output && settings_current.joystick_1_fire_5 ) ||
           ( settings_current.joystick_gcw0_output && settings_current.joystick_gcw0_l1 ) ) ) ||
       ( event->key.keysym.sym == SDLK_BACKSPACE &&
         ( ( settings_current.joystick_1_output && settings_current.joystick_1_fire_6 ) ||
           ( settings_current.joystick_gcw0_output && settings_current.joystick_gcw0_r1 ) ) ) )
    return 0;

  return 1;
}

int
filter_combo_done( const SDL_Event *event )
{
  int i, filter_released_key = 0;

  /* Not filter if not combo or not release key*/
  if ( !combo_done || event->type != SDL_KEYUP )
    return filter_released_key;

  /* Search for released key and check to filter*/
  for(i=0;i<last_combo_key;i++)
    if (combo_keys[i] && combo_keys[i]->key.keysym.sym == event->key.keysym.sym) {
      free(combo_keys[i]);
      combo_keys[i] = NULL;
      filter_released_key = 1;
    }

  /* Any combo pending */
  for(i=0;i<last_combo_key;i++)
    if (combo_keys[i] && filter_released_key)
      return filter_released_key;

  /* All combo keys released */
  combo_done = 0;
  last_combo_key = 0;
  return filter_released_key;
}

int
push_combo_event( Uint8* flags )
{
  SDLKey combo_key = 0;
  SDL_Event *combo_event;
  int toggle_triple_buffer = 0;

  /* Search valid combos */
  switch (*flags) {
  case 0xA1: combo_key = SDLK_F4; break;/* L1 + Select + X General opetions */
  case 0xA2: combo_key = SDLK_F9; break;/* L1 + Select + Y Machine Select */
  case 0x61: combo_key = SDLK_F7; break;/* L1 + Start + X Tape Open */
  case 0x62: combo_key = SDLK_F8; break;/* L1 + Start + Y Tape play */
  case 0x91: combo_key = SDLK_F12; break;/*R1 + Select + X Joystick */
  case 0x30: toggle_triple_buffer = 1; break;/* L1 + R1 Toggle triple buffer */
  case 0x21: combo_key = SDLK_F3; break;/* L1 + X Open files */
  case 0x22: combo_key = SDLK_F2; break;/* L1 + Y  Save files*/
  case 0x11: combo_key = SDLK_F10; break;/* R1 + X Exit*/
  case 0x12: combo_key = SDLK_F5; break;/* R1 + Y Reset */
  default: break;
  }

  /* Push combo event */
  if (combo_key) {
    combo_event = malloc(sizeof(SDL_Event));
    combo_event->type = SDL_KEYDOWN;
    combo_event->key.type = SDL_KEYDOWN;
    combo_event->key.state = SDL_PRESSED;
    combo_event->key.keysym.sym = combo_key;
    SDL_PushEvent(combo_event);
    *flags = 0x00;
    combo_done = 1;
    return 1;
  } else if ( toggle_triple_buffer ) {
    settings_current.triple_buffer = !settings_current.triple_buffer;
    combo_done = 1;
    *flags = 0x00;
    return 1;
  } else
    return 0;
}

/*

 This is called for filter Events. The current use is for start combos keys
 before the events will be passed to the ui_event function

 Return values are:
    0 will drop event
    1 will push event

 L1 or R1 start a combo if they are not mapped to any Joystick.
 When a combo is started if any key not used in combos is pressed or released
 then the event is pushed.
 Auto-repeat keys in combos are droped.
 if a combo is done then the release of keys in combo are dropped at init
 Current keys used in combos: L1, R1, Select, Start, X, Y.

 Combos currently are mapped to Fx functions used in Fuse:
   L1 + R1          Toggle triple buffer
   L1 + X           Open file (F3)
   L1 + Y           Save file (F2)
   R1 + X           Exit fuse (F10)
   R1 + Y           Reset machine (F5)
   R1 + Select + X  Joystick
   L1 + Select + X  General options (F4)
   L1 + Select + Y  Machine select (F9)
   L1 + Start + X   Tape open (F7)
   L1 + Start + Y   Tape play (F8)
*/
int
filter_combo_events( const SDL_Event *event )
{
  static Uint8  flags  = 0;
  int i, not_in_combo  = 0;

  /* Filter release of combo keys */
  if ( filter_combo_done(event) ) return 0;

  if ( !is_combo_possible(event) ) return 1;

  switch (event->type) {
  case SDL_KEYDOWN:
    switch (event->key.keysym.sym) {
    case SDLK_ESCAPE:    /* Select */
      if (flags) {
        if (flags & 0x80)
          return 0; /* Filter Repeat key */
        else
          flags |= 0x80;
      } else
        return 1;
      break;

    case SDLK_RETURN:    /* Start */
      if (flags) {
        if (flags & 0x40)
          return 0; /* Filter Repeat key */
        else
          flags |= 0x40;
      } else
        return 1;
      break;

    case SDLK_TAB:        /* L1 */
      if (flags & 0x20)
        return 0; /* Filter Repeat key */
      else
        flags |= 0x20;
      break;

    case SDLK_BACKSPACE:  /* R1 */
      if (flags & 0x10)
        return 0; /* Filter Repeat key */
      else
        flags |= 0x10;
      break;

    case SDLK_SPACE:     /* X  */
      if (flags) {
        if (flags & 0x01)
          return 0; /* Filter Repeat key */
        else
          flags |= 0x01;
      } else
        return 1;
      break;

    case SDLK_LSHIFT:      /* Y  */
      if (flags) {
        if (flags & 0x02)
          return 0; /* Filter Repeat key */
        else
          flags |= 0x02;
      } else
        return 1;
      break;

    /* Key not in combo */
    default:
      not_in_combo = 1;
      break;
    }

    break;

  /* Any release key break the combo */
  case SDL_KEYUP:
    switch (event->key.keysym.sym) {
    case SDLK_ESCAPE:     /* Select */
      if (flags & 0x80)
        flags &= ~0x80;
      else
        return 1;
      break;

    case SDLK_RETURN:    /* Start */
      if (flags & 0x40)
        flags &= ~0x40;
      else
        return 1;
      break;

    case SDLK_TAB:        /* L1 */
      flags &= ~0x20;
      break;

    case SDLK_BACKSPACE:  /* R1 */
      flags &= ~0x10;
      break;

    case SDLK_SPACE:      /* X  */
      if (flags & 0x01)
        flags &= ~0x01;
      else
        return 1;
      break;

    case SDLK_LSHIFT:    /* Y  */
      if (flags & 0x02)
        flags &= ~0x02;
      else
        return 1;
      break;

    /* Key not in combo */
    default:
      not_in_combo = 1;
      break;
    }

    /* All Combo keys released, clean all saved events */
    if (!not_in_combo && !flags) {
      for(i=0;i<last_combo_key;i++)
        if (combo_keys[i]) {
          free(combo_keys[i]);
          combo_keys[i] = NULL;
        }
      last_combo_key = 0;
    }

    break;

  default:
    not_in_combo = 1;
    break;
  }

  if (!not_in_combo) {
    /* Save actual event, excluding releases */
    if (event->type == SDL_KEYDOWN) {
      combo_keys[last_combo_key] = malloc( sizeof(SDL_Event) );
      memcpy( combo_keys[last_combo_key], event, sizeof(SDL_Event) );
      last_combo_key++;
    }

    /* Push combo event */
    push_combo_event( &flags );

    /* Drop current Event */
    return 0;

  } else
    /* Push current Event */
    return 1;
}
#endif /* GCWZERO */
