/* debugger.c: the GTK+ debugger
   Copyright (c) 2002 Philip Kendall

   $Id$

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

   Author contact information:

   E-mail: pak21-fuse@srcf.ucam.org
   Postal address: 15 Crescent Road, Wokingham, Berks, RG40 2DB, England

*/

#include <config.h>

#ifdef UI_GTK		/* Use this file iff we're using GTK+ */

#include <stdio.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "debugger/debugger.h"
#include "fuse.h"
#include "gtkui.h"
#include "spectrum.h"
#include "ui/ui.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

static int create_dialog( void );
static int activate_debugger( void );
static int deactivate_debugger( void );

static void evaluate_command( GtkWidget *widget, gpointer user_data );
static void gtkui_debugger_done_step( GtkWidget *widget, gpointer user_data );
static void gtkui_debugger_done_continue( GtkWidget *widget,
					  gpointer user_data );
static void gtkui_debugger_break( GtkWidget *widget, gpointer user_data );
static void gtkui_debugger_done_close( GtkWidget *widget, gpointer user_data );

static GtkWidget *dialog,		/* The debugger dialog box */
  *continue_button, *break_button,	/* Two of its buttons */
  *registers[17],			/* The register display */
  *breakpoints,				/* The breakpoint display */
  *disassembly;				/* The disassembly */

/* The top line of the current disassembly */
static WORD disassembly_top;

/* Have we created the above yet? */
static int dialog_created = 0;

/* Is the debugger window active (as opposed to the debugger itself)? */
static int debugger_active;

int
ui_debugger_activate( void )
{
  fuse_emulation_pause();

  /* Create the dialog box if it doesn't already exist */
  if( !dialog_created ) if( create_dialog() ) return 1;

  gtk_widget_show_all( dialog );

  gtk_widget_set_sensitive( continue_button, 1 );
  gtk_widget_set_sensitive( break_button, 0 );
  if( !debugger_active ) activate_debugger();

  return 0;
}

int
ui_debugger_deactivate( int interruptable )
{
  if( debugger_active ) deactivate_debugger();

  gtk_widget_set_sensitive( continue_button, !interruptable );
  gtk_widget_set_sensitive( break_button,     interruptable );

  return 0;
}

static int
create_dialog( void )
{
  size_t i;
  GtkWidget *hbox, *vbox;
  GtkWidget *table;
  GtkWidget *entry, *eval_button;
  GtkWidget *step_button, *close_button;
  GtkAccelGroup *accel_group;
  GtkStyle *style;

  gchar *breakpoint_titles[] = { "ID", "Type", "Value", "Ignore", "Life" },
    *disassembly_titles[] = { "Address", "Instruction" };

  /* Try and get a monospaced font */
  style = gtk_style_new();
  gdk_font_unref( style->font );

  style->font = gdk_font_load( "-*-courier-medium-r-*-*-12-*-*-*-*-*-*-*" );
  if( !style->font ) {
    ui_error( UI_ERROR_ERROR, "couldn't find a monospaced font" );
    return 1;
  }

  dialog = gtk_dialog_new();
  gtk_window_set_title( GTK_WINDOW( dialog ), "Fuse - Debugger" );

  /* A couple of boxes to contain the things we want to display */
  hbox = gtk_hbox_new( FALSE, 5 );
  gtk_box_pack_start_defaults( GTK_BOX( GTK_DIALOG( dialog )->vbox ), hbox );

  vbox = gtk_vbox_new( FALSE, 5 );
  gtk_box_pack_start_defaults( GTK_BOX( hbox ), vbox );

  /* 'table' contains the register display */
  table = gtk_table_new( 9, 2, FALSE );
  gtk_box_pack_start_defaults( GTK_BOX( vbox ), table );

  for( i = 0; i < 15; i++ ) {

    registers[i] = gtk_label_new( "" );
    gtk_widget_set_style( registers[i], style );
    gtk_table_attach_defaults( GTK_TABLE( table ), registers[i],
			       i%2, i%2+1, i/2, i/2+1 );

  }

  /* Interrupt mode */
  registers[15] = gtk_label_new( "" );
  gtk_widget_set_style( registers[15], style );
  gtk_table_attach_defaults( GTK_TABLE( table ), registers[15], 1, 2, 7, 8 );

  /* Flags */
  registers[16] = gtk_label_new( "" );
  gtk_widget_set_style( registers[16], style );
  gtk_table_attach_defaults( GTK_TABLE( table ), registers[16], 0, 1, 8, 9 );

  /* The breakpoint CList */
  breakpoints = gtk_clist_new_with_titles( 5, breakpoint_titles );
  gtk_clist_column_titles_passive( GTK_CLIST( breakpoints ) );
  gtk_box_pack_start_defaults( GTK_BOX( vbox ), breakpoints );

  /* Create the disassembly CList itself */
  disassembly = gtk_clist_new_with_titles( 2, disassembly_titles );
  gtk_widget_set_style( disassembly, style );
  gtk_clist_column_titles_passive( GTK_CLIST( disassembly ) );
  gtk_box_pack_start_defaults( GTK_BOX( hbox ), disassembly );

  /* Another hbox to hold the command entry widget and the 'evaluate'
     button */
  hbox = gtk_hbox_new( FALSE, 5 );
  gtk_box_pack_start_defaults( GTK_BOX( GTK_DIALOG( dialog )->vbox ), hbox );

  /* The command entry widget */
  entry = gtk_entry_new();
  gtk_box_pack_start_defaults( GTK_BOX( hbox ), entry );

  /* The 'command evaluate' button */
  eval_button = gtk_button_new_with_label( "Evaluate" );
  gtk_signal_connect_object( GTK_OBJECT( eval_button ), "clicked",
			     GTK_SIGNAL_FUNC( evaluate_command ),
			     GTK_OBJECT( entry ) );
  gtk_box_pack_start_defaults( GTK_BOX( hbox ), eval_button );

  /* The action buttons for the dialog box */

  step_button = gtk_button_new_with_label( "Single Step" );
  gtk_signal_connect( GTK_OBJECT( step_button ), "clicked",
		      GTK_SIGNAL_FUNC( gtkui_debugger_done_step ), NULL );
  gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->action_area ),
		     step_button );

  continue_button = gtk_button_new_with_label( "Continue" );
  gtk_signal_connect( GTK_OBJECT( continue_button ), "clicked",
		      GTK_SIGNAL_FUNC( gtkui_debugger_done_continue ), NULL );
  gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->action_area ),
		     continue_button );

  break_button = gtk_button_new_with_label( "Break" );
  gtk_signal_connect( GTK_OBJECT( break_button ), "clicked",
		      GTK_SIGNAL_FUNC( gtkui_debugger_break ), NULL );
  gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->action_area ),
		     break_button );

  close_button = gtk_button_new_with_label( "Close" );
  gtk_signal_connect_object( GTK_OBJECT( close_button ), "clicked",
			     GTK_SIGNAL_FUNC( gtkui_debugger_done_close ),
			     GTK_OBJECT( dialog ) );
  gtk_container_add( GTK_CONTAINER( GTK_DIALOG( dialog )->action_area ),
		     close_button );

  /* Deleting the window is the same as pressing 'Close' */
  gtk_signal_connect( GTK_OBJECT( dialog ), "delete_event",
		      GTK_SIGNAL_FUNC( gtkui_debugger_done_close ),
		      (gpointer) NULL );

  /* Keyboard shortcuts */
  accel_group = gtk_accel_group_new();
  gtk_window_add_accel_group( GTK_WINDOW( dialog ), accel_group );

  /* Return is equivalent to clicking on 'evaluate' */
  gtk_widget_add_accelerator( eval_button, "clicked", accel_group,
			      GDK_Return, 0, 0 );

  /* Esc is equivalent to clicking on 'close' */
  gtk_widget_add_accelerator( close_button, "clicked", accel_group,
                              GDK_Escape, 0, 0 );

  dialog_created = 1;

  return 0;
}

static int
activate_debugger( void )
{
  debugger_active = 1;

  disassembly_top = PC;
  ui_debugger_update();

  gtk_main();
  return 0;
}

/* Update the debugger's display */
int
ui_debugger_update( void )
{
  size_t i;
  char buffer[1024];
  gchar *breakpoint_text[5] = { &buffer[  0], &buffer[ 40], &buffer[80],
			        &buffer[120], &buffer[160]               },
    *disassembly_text[2] = { &buffer[0], &buffer[40] };
  WORD address;
  char *format_16_bit, *format_8_bit;
  GSList *ptr;

  const char *register_name[] = { "PC", "SP",
				  "AF", "AF'",
				  "BC", "BC'",
				  "DE", "DE'",
				  "HL", "HL'",
				  "IX", "IY",
                                };

  WORD *value_ptr[] = { &PC, &SP,  &AF, &AF_,
			&BC, &BC_, &DE, &DE_,
			&HL, &HL_, &IX, &IY,
                      };

  if( debugger_output_base == 10 ) {
    format_16_bit = format_8_bit = "%5d";
  } else {
    format_16_bit = "0x%04X"; format_8_bit = "0x  %02X";
  }

  for( i = 0; i < 12; i++ ) {
    snprintf( buffer, 5, "%3s ", register_name[i] );
    snprintf( &buffer[4], 76, format_16_bit, *value_ptr[i] );
    gtk_label_set_text( GTK_LABEL( registers[i] ), buffer );
  }

  strcpy( buffer, "  I " ); snprintf( &buffer[4], 76, format_8_bit, I );
  gtk_label_set_text( GTK_LABEL( registers[12] ), buffer );
  strcpy( buffer, "  R " );
  snprintf( &buffer[4], 80, format_8_bit, ( R & 0x7f ) | ( R7 & 0x80 ) );
  gtk_label_set_text( GTK_LABEL( registers[13] ), buffer );

  snprintf( buffer, 80, "T-states %5d", tstates );
  gtk_label_set_text( GTK_LABEL( registers[14] ), buffer );
  snprintf( buffer, 80, "IM %d", IM );
  gtk_label_set_text( GTK_LABEL( registers[15] ), buffer );

  strcpy( buffer, "SZ5H3PNC\n" );
  for( i = 0; i < 8; i++ ) buffer[i+9] = ( F & ( 0x80 >> i ) ) ? '1' : '0';
  buffer[17] = '\0';
  gtk_label_set_text( GTK_LABEL( registers[16] ), buffer );

  /* Create the breakpoint list */
  gtk_clist_freeze( GTK_CLIST( breakpoints ) );
  gtk_clist_clear( GTK_CLIST( breakpoints ) );

  for( ptr = debugger_breakpoints; ptr; ptr = ptr->next ) {

    debugger_breakpoint *bp = ptr->data;

    snprintf( breakpoint_text[0], 40, "%lu", (unsigned long)bp->id );
    snprintf( breakpoint_text[1], 40, "%d", bp->type );
    snprintf( breakpoint_text[2], 40, format_16_bit, bp->value );
    snprintf( breakpoint_text[3], 40, "%lu", (unsigned long)bp->ignore );
    snprintf( breakpoint_text[4], 40, "%d", bp->life );

    gtk_clist_append( GTK_CLIST( breakpoints ), breakpoint_text );
  }

  gtk_clist_thaw( GTK_CLIST( breakpoints ) );

  /* Put some disassembly in */
  gtk_clist_freeze( GTK_CLIST( disassembly ) );
  gtk_clist_clear( GTK_CLIST( disassembly ) );

  for( i = 0, address = disassembly_top; i < 20; i++ ) {

    size_t length;

    snprintf( disassembly_text[0], 40, format_16_bit, address );
    debugger_disassemble( disassembly_text[1], 40, &length, address );
    address += length;

    gtk_clist_append( GTK_CLIST( disassembly ), disassembly_text );
  }
  gtk_clist_thaw( GTK_CLIST( disassembly ) );

  return 0;
}

static int
deactivate_debugger( void )
{
  gtk_main_quit();
  debugger_active = 0;
  fuse_emulation_unpause();
  return 0;
}

/* Set the disassembly to start at 'address' */
int
ui_debugger_disassemble( WORD address )
{
  disassembly_top = address;
  return 0;
}

/* Evaluate the command currently in the entry box */
static void
evaluate_command( GtkWidget *widget, gpointer user_data GCC_UNUSED )
{
  debugger_command_evaluate( gtk_entry_get_text( GTK_ENTRY( widget ) ) );
}

static void
gtkui_debugger_done_step( GtkWidget *widget GCC_UNUSED,
			  gpointer user_data GCC_UNUSED )
{
  debugger_step();
}

static void
gtkui_debugger_done_continue( GtkWidget *widget GCC_UNUSED,
			      gpointer user_data GCC_UNUSED )
{
  debugger_run();
}

static void
gtkui_debugger_break( GtkWidget *widget GCC_UNUSED,
		      gpointer user_data GCC_UNUSED )
{
  debugger_mode = DEBUGGER_MODE_HALTED;
  gtk_widget_set_sensitive( continue_button, 1 );
  gtk_widget_set_sensitive( break_button, 0 );
}

static void
gtkui_debugger_done_close( GtkWidget *widget, gpointer user_data GCC_UNUSED )
{
  gtk_widget_hide_all( widget );
  gtkui_debugger_done_continue( NULL, NULL );
}

#endif			/* #ifdef UI_GTK */
