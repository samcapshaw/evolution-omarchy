/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sender avatars for the mail list: a site icon fetched once per sender
 * domain, or a colored initial as the placeholder.
 */

#if !defined (__E_UTIL_H_INSIDE__) && !defined (LIBEUTIL_COMPILATION)
#error "Only <e-util/e-util.h> should be included directly."
#endif

#ifndef E_OMA_AVATAR_H
#define E_OMA_AVATAR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void		e_oma_avatar_draw		(cairo_t *cr,
						 gdouble x,
						 gdouble y,
						 gdouble size,
						 const gchar *email,
						 const gchar *name,
						 GtkWidget *redraw_widget);

G_END_DECLS

#endif /* E_OMA_AVATAR_H */
