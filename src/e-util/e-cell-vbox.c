/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Authors:
 *		Chris Toshok <toshok@ximian.com>
 *		Chris Lahey  <clahey@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "evolution-config.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include <gtk/gtk.h>

#include "gal-a11y-e-cell-registry.h"
#include "gal-a11y-e-cell-vbox.h"

#include "e-cell-vbox.h"
#include "e-table-item.h"

G_DEFINE_TYPE (ECellVbox, e_cell_vbox, E_TYPE_CELL)

#define INDENT_AMOUNT 16

/* Mail status is painted outside the existing text cells.  It has no
 * cell view or input handler, so the native tree/selection path is unchanged. */
static gint
mail_gutter_width (ECellView *view)
{
	return g_object_get_data (G_OBJECT (view->ecell), "oma-unread-gutter-col") ? 32 : 0;
}

static void
mail_gutter_draw (ECellView *view, cairo_t *cr, gint row, gint x, gint y, gint height)
{
	gint unread_col = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view->ecell), "oma-unread-gutter-col")) - 1;
	gint flag_col = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (view->ecell), "oma-flag-gutter-col")) - 1;
	gdouble cy = y + height / 2.0;
	cairo_save (cr);
	cairo_set_source_rgb (cr, 122.0 / 255, 162.0 / 255, 247.0 / 255);
	if (GPOINTER_TO_INT (e_table_model_value_at (view->e_table_model, unread_col, row))) {
		cairo_arc (cr, x + 8, cy, 3.5, 0, 2 * G_PI);
		cairo_fill (cr);
	}
	if (GPOINTER_TO_INT (e_table_model_value_at (view->e_table_model, flag_col, row))) {
		cairo_set_line_width (cr, 1.5);
		cairo_move_to (cr, x + 20, cy + 6);
		cairo_line_to (cr, x + 20, cy - 6);
		cairo_line_to (cr, x + 28, cy - 6);
		cairo_line_to (cr, x + 26, cy - 3);
		cairo_line_to (cr, x + 28, cy);
		cairo_line_to (cr, x + 20, cy);
		cairo_stroke (cr);
	}
	cairo_restore (cr);
}

/*
 * ECell::new_view method
 */
static ECellView *
ecv_new_view (ECell *ecell,
              ETableModel *table_model,
              gpointer e_table_item_view)
{
	ECellVbox *ecv = E_CELL_VBOX (ecell);
	ECellVboxView *vbox_view = g_new0 (ECellVboxView, 1);
	gint i;

	vbox_view->cell_view.ecell = ecell;
	vbox_view->cell_view.e_table_model = table_model;
	vbox_view->cell_view.e_table_item_view = e_table_item_view;
	vbox_view->cell_view.kill_view_cb = NULL;
	vbox_view->cell_view.kill_view_cb_data = NULL;

	/* create our subcell view */
	vbox_view->subcell_view_count = ecv->subcell_count;
	vbox_view->subcell_views = g_new (ECellView *, vbox_view->subcell_view_count);
	vbox_view->model_cols = g_new (int, vbox_view->subcell_view_count);

	for (i = 0; i < vbox_view->subcell_view_count; i++) {
		vbox_view->subcell_views[i] = e_cell_new_view (ecv->subcells[i], table_model, e_table_item_view /* XXX */);
		vbox_view->model_cols[i] = ecv->model_cols[i];
	}

	return (ECellView *) vbox_view;
}

/*
 * ECell::kill_view method
 */
static void
ecv_kill_view (ECellView *ecv)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecv;
	gint i;

	if (vbox_view->cell_view.kill_view_cb)
		(vbox_view->cell_view.kill_view_cb)(ecv, vbox_view->cell_view.kill_view_cb_data);

	if (vbox_view->cell_view.kill_view_cb_data)
	    g_list_free (vbox_view->cell_view.kill_view_cb_data);

	/* kill our subcell view */
	for (i = 0; i < vbox_view->subcell_view_count; i++)
		e_cell_kill_view (vbox_view->subcell_views[i]);

	g_free (vbox_view->model_cols);
	g_free (vbox_view->subcell_views);
	g_free (vbox_view);
}

/*
 * ECell::realize method
 */
static void
ecv_realize (ECellView *ecell_view)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecell_view;
	gint i;

	/* realize our subcell view */
	for (i = 0; i < vbox_view->subcell_view_count; i++)
		e_cell_realize (vbox_view->subcell_views[i]);

	if (E_CELL_CLASS (e_cell_vbox_parent_class)->realize)
		(* E_CELL_CLASS (e_cell_vbox_parent_class)->realize) (ecell_view);
}

/*
 * ECell::unrealize method
 */
static void
ecv_unrealize (ECellView *ecv)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecv;
	gint i;

	/* unrealize our subcell view. */
	for (i = 0; i < vbox_view->subcell_view_count; i++)
		e_cell_unrealize (vbox_view->subcell_views[i]);

	if (E_CELL_CLASS (e_cell_vbox_parent_class)->unrealize)
		(* E_CELL_CLASS (e_cell_vbox_parent_class)->unrealize) (ecv);
}

/*
 * ECell::draw method
 */
static void
ecv_draw (ECellView *ecell_view,
          cairo_t *cr,
          gint model_col,
          gint view_col,
          gint row,
          ECellFlags flags,
          gint x1,
          gint y1,
          gint x2,
          gint y2)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecell_view;

	gint subcell_offset = 0;
	gint i;
	gint gutter = mail_gutter_width (ecell_view);

	if (gutter && vbox_view->subcell_view_count)
		mail_gutter_draw (ecell_view, cr, row, x1, y1,
			e_cell_height (vbox_view->subcell_views[0], vbox_view->model_cols[0], view_col, row));

	for (i = 0; i < vbox_view->subcell_view_count; i++) {
		/* Now cause our subcells to draw their contents,
		 * shifted by subcell_offset pixels */
		gint height;

		height = e_cell_height (
			vbox_view->subcell_views[i],
			vbox_view->model_cols[i], view_col, row);
		e_cell_draw (
			vbox_view->subcell_views[i], cr,
			vbox_view->model_cols[i], view_col, row, flags,
			x1 + gutter, y1 + subcell_offset, x2,
			y1 + subcell_offset + height);

		subcell_offset += e_cell_height (
			vbox_view->subcell_views[i],
			vbox_view->model_cols[i], view_col, row);
	}
}

/*
 * ECell::event method
 */
static gint
ecv_event (ECellView *ecell_view,
           GdkEvent *event,
           gint model_col,
           gint view_col,
           gint row,
           ECellFlags flags,
           ECellActions *actions)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecell_view;
	gint y = 0;
	gint gutter = mail_gutter_width (ecell_view);
	GdkEvent *adjusted = NULL;
	gint result = 0;
	gint i;
	gint subcell_offset = 0;

	switch (event->type) {
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS:
		y = event->button.y;
		break;
	case GDK_MOTION_NOTIFY:
		y = event->motion.y;
		break;
	default:
		/* nada */
		break;
	}

	if (gutter && (event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE ||
	    event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS || event->type == GDK_MOTION_NOTIFY)) {
		gdouble x = event->type == GDK_MOTION_NOTIFY ? event->motion.x : event->button.x;
		if (x < gutter)
			return FALSE;
		adjusted = gdk_event_copy (event);
		if (event->type == GDK_MOTION_NOTIFY)
			adjusted->motion.x -= gutter;
		else
			adjusted->button.x -= gutter;
		event = adjusted;
	}

	for (i = 0; i < vbox_view->subcell_view_count; i++) {
		gint height = e_cell_height (vbox_view->subcell_views[i], vbox_view->model_cols[i], view_col, row);
		if (y < subcell_offset + height) {
			result = e_cell_event (vbox_view->subcell_views[i], event, vbox_view->model_cols[i], view_col, row, flags, actions);
			break;
		}
		subcell_offset += height;
	}
	if (adjusted)
		gdk_event_free (adjusted);
	return result;
}

/*
 * ECell::height method
 */
static gint
ecv_height (ECellView *ecell_view,
            gint model_col,
            gint view_col,
            gint row)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecell_view;
	gint height = 0;
	gint i;

	for (i = 0; i < vbox_view->subcell_view_count; i++) {
		height += e_cell_height (vbox_view->subcell_views[i], vbox_view->model_cols[i], view_col, row);
	}
	return height;
}

/*
 * ECell::max_width method
 */
static gint
ecv_max_width (ECellView *ecell_view,
               gint model_col,
               gint view_col)
{
	ECellVboxView *vbox_view = (ECellVboxView *) ecell_view;
	gint max_width = 0;
	gint i;

	for (i = 0; i < vbox_view->subcell_view_count; i++) {
		gint width = e_cell_max_width (vbox_view->subcell_views[i], vbox_view->model_cols[i], view_col);
		max_width = MAX (width, max_width);
	}

	return max_width + mail_gutter_width (ecell_view);
}

/*
 * GObject::dispose method
 */
static void
ecv_dispose (GObject *object)
{
	ECellVbox *ecv = E_CELL_VBOX (object);
	gint i;

	/* destroy our subcell */
	for (i = 0; i < ecv->subcell_count; i++)
		if (ecv->subcells[i])
			g_object_unref (ecv->subcells[i]);
	g_free (ecv->subcells);
	ecv->subcells = NULL;
	ecv->subcell_count = 0;

	G_OBJECT_CLASS (e_cell_vbox_parent_class)->dispose (object);
}

static void
ecv_finalize (GObject *object)
{
	ECellVbox *ecv = E_CELL_VBOX (object);

	g_free (ecv->model_cols);

	G_OBJECT_CLASS (e_cell_vbox_parent_class)->finalize (object);
}

static void
e_cell_vbox_class_init (ECellVboxClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	ECellClass *ecc = E_CELL_CLASS (class);

	object_class->dispose = ecv_dispose;
	object_class->finalize = ecv_finalize;

	ecc->new_view = ecv_new_view;
	ecc->kill_view = ecv_kill_view;
	ecc->realize = ecv_realize;
	ecc->unrealize = ecv_unrealize;
	ecc->draw = ecv_draw;
	ecc->event = ecv_event;
	ecc->height = ecv_height;
	ecc->max_width = ecv_max_width;

	gal_a11y_e_cell_registry_add_cell_type (NULL, E_TYPE_CELL_VBOX, gal_a11y_e_cell_vbox_new);
}

static void
e_cell_vbox_init (ECellVbox *ecv)
{
	ecv->subcells = NULL;
	ecv->subcell_count = 0;
}

/**
 * e_cell_vbox_new:
 *
 * Creates a new ECell renderer that can be used to render multiple
 * child cells.
 *
 * Return value: an ECell object that can be used to render multiple
 * child cells.
 **/
ECell *
e_cell_vbox_new (void)
{
	return g_object_new (E_TYPE_CELL_VBOX, NULL);
}

void
e_cell_vbox_append (ECellVbox *vbox,
                    ECell *subcell,
                    gint model_col)
{
	vbox->subcell_count++;

	vbox->subcells = g_renew (ECell *, vbox->subcells,   vbox->subcell_count);
	vbox->model_cols = g_renew (int,     vbox->model_cols, vbox->subcell_count);

	vbox->subcells[vbox->subcell_count - 1]   = subcell;
	vbox->model_cols[vbox->subcell_count - 1] = model_col;

	if (subcell)
		g_object_ref_sink (subcell);
}
