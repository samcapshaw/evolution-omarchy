/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-config.h"

#include "e-online-button.h"

#include <glib/gi18n.h>

#include "e-misc-utils.h"

#define ONLINE_TOOLTIP \
	_("Evolution is currently online.  Click this button to work offline.")

#define OFFLINE_TOOLTIP \
	_("Evolution is currently offline.  Click this button to work online.")

#define NETWORK_UNAVAILABLE_TOOLTIP \
	_("Evolution is currently offline because the network is unavailable.")

struct _EOnlineButtonPrivate {
	GtkWidget *image;
	gboolean online;
};

enum {
	PROP_0,
	PROP_ONLINE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (EOnlineButton, e_online_button, GTK_TYPE_BUTTON)

static void
online_button_update_tooltip (EOnlineButton *button)
{
	const gchar *tooltip;

	if (e_online_button_get_online (button))
		tooltip = ONLINE_TOOLTIP;
	else if (gtk_widget_get_sensitive (GTK_WIDGET (button)))
		tooltip = OFFLINE_TOOLTIP;
	else
		tooltip = NETWORK_UNAVAILABLE_TOOLTIP;

	gtk_widget_set_tooltip_text (GTK_WIDGET (button), tooltip);
}

static void
online_button_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ONLINE:
			e_online_button_set_online (
				E_ONLINE_BUTTON (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
online_button_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ONLINE:
			g_value_set_boolean (
				value, e_online_button_get_online (
				E_ONLINE_BUTTON (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
online_button_dispose (GObject *object)
{
	EOnlineButton *self = E_ONLINE_BUTTON (object);

	g_clear_object (&self->priv->image);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_online_button_parent_class)->dispose (object);
}

static void
e_online_button_class_init (EOnlineButtonClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = online_button_set_property;
	object_class->get_property = online_button_get_property;
	object_class->dispose = online_button_dispose;

	/**
	 * EOnlineButton:online
	 *
	 * The button state is online
	 **/
	properties[PROP_ONLINE] =
		g_param_spec_boolean (
			"online", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
e_online_button_init (EOnlineButton *button)
{
	GtkWidget *widget;

	button->priv = e_online_button_get_instance_private (button);

	gtk_widget_set_can_focus (GTK_WIDGET (button), FALSE);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);

	/* Starts offline (priv->online is FALSE); set_online() swaps the glyph. */
	widget = gtk_image_new_from_icon_name ("network-offline-symbolic", GTK_ICON_SIZE_MENU);
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (button)), "offline");
	gtk_container_add (GTK_CONTAINER (button), widget);
	button->priv->image = g_object_ref (widget);
	gtk_widget_show (widget);

	e_signal_connect_notify (
		button, "notify::online",
		G_CALLBACK (online_button_update_tooltip), NULL);

	e_signal_connect_notify (
		button, "notify::sensitive",
		G_CALLBACK (online_button_update_tooltip), NULL);
}

GtkWidget *
e_online_button_new (void)
{
	return g_object_new (E_TYPE_ONLINE_BUTTON, NULL);
}

gboolean
e_online_button_get_online (EOnlineButton *button)
{
	g_return_val_if_fail (E_IS_ONLINE_BUTTON (button), FALSE);

	return button->priv->online;
}

void
e_online_button_set_online (EOnlineButton *button,
                            gboolean online)
{
	GtkStyleContext *context;

	g_return_if_fail (E_IS_ONLINE_BUTTON (button));

	if (button->priv->online == online)
		return;

	button->priv->online = online;

	/* Monochrome glyph that follows the theme; offline gets a warning tint. */
	gtk_image_set_from_icon_name (GTK_IMAGE (button->priv->image),
		online ? "network-transmit-receive-symbolic" : "network-offline-symbolic",
		GTK_ICON_SIZE_MENU);
	context = gtk_widget_get_style_context (GTK_WIDGET (button));
	if (online)
		gtk_style_context_remove_class (context, "offline");
	else
		gtk_style_context_add_class (context, "offline");

	g_object_notify_by_pspec (G_OBJECT (button), properties[PROP_ONLINE]);
}
