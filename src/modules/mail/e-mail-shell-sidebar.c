/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-config.h"

#include "e-mail-shell-content.h"
#include "e-mail-shell-sidebar.h"

#include "mail/e-mail-backend.h"
#include "mail/e-mail-reader.h"
#include "mail/e-mail-sidebar.h"
#include "mail/em-folder-utils.h"
#include "mail/em-folder-tree-model.h"
#include "libemail-engine/libemail-engine.h"

struct _EMailShellSidebarPrivate {
	GtkWidget *folder_tree;

	/* Folded sidebar: a column of mailbox shortcuts. */
	GtkWidget *rail;
	guint rail_idle_id;
	guint rail_unread_idle_id;
};

/* —— Folded mailbox rail ——
 * When the sidebar is folded to a strip, the full folder tree is swapped
 * for one button per unified mailbox and per account inbox, so the folded
 * state still answers "which mail do I want to check?". */

static void
oma_rail_button_clicked_cb (GtkButton *button,
                            EMailShellSidebar *self)
{
	const gchar *uri = g_object_get_data (G_OBJECT (button), "oma-uri");

	if (uri && self->priv->folder_tree)
		em_folder_tree_set_selected (EM_FOLDER_TREE (self->priv->folder_tree), uri, FALSE);
}

static void
oma_rail_sync_selection (EMailShellSidebar *self)
{
	GList *children, *link;
	gchar *selected;

	if (!self->priv->rail || !self->priv->folder_tree)
		return;

	selected = em_folder_tree_get_selected_uri (EM_FOLDER_TREE (self->priv->folder_tree));
	children = gtk_container_get_children (GTK_CONTAINER (self->priv->rail));
	for (link = children; link; link = g_list_next (link)) {
		const gchar *uri = g_object_get_data (G_OBJECT (link->data), "oma-uri");
		GtkStyleContext *context = gtk_widget_get_style_context (link->data);

		if (uri && selected && g_str_equal (uri, selected))
			gtk_style_context_add_class (context, "selected");
		else
			gtk_style_context_remove_class (context, "selected");
	}
	g_list_free (children);
	g_free (selected);
}

static void
oma_rail_add (EMailShellSidebar *self,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              CamelStore *store,
              const gchar *full_name,
              const gchar *icon_name,
              const gchar *initial,
              const gchar *tooltip,
              guint unread)
{
	GtkWidget *button, *child;
	gchar *text;

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
	if (icon_name) {
		child = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	} else {
		child = gtk_label_new (initial);
		gtk_style_context_add_class (gtk_widget_get_style_context (child), "oma-rail-initial");
	}
	gtk_container_add (GTK_CONTAINER (button), child);
	if (unread > 0)
		gtk_style_context_add_class (gtk_widget_get_style_context (button), "unread");

	text = unread > 0 ? g_strdup_printf ("%s — %u unread", tooltip, unread) : g_strdup (tooltip);
	gtk_widget_set_tooltip_text (button, text);
	g_free (text);

	g_object_set_data_full (G_OBJECT (button), "oma-uri",
		e_mail_folder_uri_build (store, full_name), g_free);
	{
		GtkTreePath *path = gtk_tree_model_get_path (model, iter);
		g_object_set_data_full (G_OBJECT (button), "oma-row",
			gtk_tree_row_reference_new (model, path), (GDestroyNotify) gtk_tree_row_reference_free);
		g_object_set_data_full (G_OBJECT (button), "oma-label", g_strdup (tooltip), g_free);
		gtk_tree_path_free (path);
	}
	g_signal_connect (button, "clicked", G_CALLBACK (oma_rail_button_clicked_cb), self);
	gtk_box_pack_start (GTK_BOX (self->priv->rail), button, FALSE, FALSE, 0);
	gtk_widget_show_all (button);
}

static gboolean
oma_rail_rebuild_idle_cb (gpointer user_data)
{
	EMailShellSidebar *self = user_data;
	GtkTreeModel *model;
	GtkTreeIter store_iter;
	gboolean valid, first = TRUE;

	self->priv->rail_idle_id = 0;
	if (!self->priv->rail || !self->priv->folder_tree)
		return G_SOURCE_REMOVE;

	gtk_container_foreach (GTK_CONTAINER (self->priv->rail), (GtkCallback) gtk_widget_destroy, NULL);
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (self->priv->folder_tree));

	for (valid = model && gtk_tree_model_get_iter_first (model, &store_iter); valid;
	     valid = gtk_tree_model_iter_next (model, &store_iter)) {
		CamelStore *store = NULL;
		gchar *store_name = NULL;
		GtkTreeIter iter;
		gboolean child_valid, added = FALSE;
		const gchar *uid;

		gtk_tree_model_get (model, &store_iter,
			COL_OBJECT_CAMEL_STORE, &store,
			COL_STRING_DISPLAY_NAME, &store_name, -1);
		uid = store ? camel_service_get_uid (CAMEL_SERVICE (store)) : NULL;

		if (!store || g_strcmp0 (uid, "local") == 0) {
			g_clear_object (&store);
			g_free (store_name);
			continue;
		}

		for (child_valid = gtk_tree_model_iter_children (model, &iter, &store_iter); child_valid;
		     child_valid = gtk_tree_model_iter_next (model, &iter)) {
			gchar *name = NULL, *full_name = NULL;
			guint32 flags = 0;
			guint unread = 0;

			gtk_tree_model_get (model, &iter,
				COL_STRING_DISPLAY_NAME, &name,
				COL_STRING_FULL_NAME, &full_name,
				COL_UINT_FLAGS, &flags,
				COL_UINT_UNREAD, &unread, -1);

			if (full_name && g_strcmp0 (uid, "vfolder") == 0) {
				const gchar *icon = "folder-saved-search-symbolic";

				if (g_strcmp0 (name, "All Inboxes") == 0)
					icon = "mail-inbox-symbolic";
				else if (g_strcmp0 (name, "All Sent") == 0)
					icon = "mail-send-symbolic";
				else if (g_strcmp0 (name, "Flagged") == 0)
					icon = "oma-mail-flag-symbolic";
				if (!added && !first)
					gtk_box_pack_start (GTK_BOX (self->priv->rail), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
				oma_rail_add (self, model, &iter, store, full_name, icon, NULL, name, unread);
				added = TRUE;
			} else if (full_name && (flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_INBOX) {
				/* An account's inbox shows as the account's initial. */
				gchar initial[8] = { 0 };
				gchar *tooltip = g_strdup_printf ("%s — %s", name, store_name ? store_name : "");
				gunichar ch = store_name && *store_name ? g_unichar_toupper (g_utf8_get_char (store_name)) : '?';

				g_unichar_to_utf8 (ch, initial);
				if (!added && !first)
					gtk_box_pack_start (GTK_BOX (self->priv->rail), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
				oma_rail_add (self, model, &iter, store, full_name, NULL, initial, tooltip, unread);
				g_free (tooltip);
				added = TRUE;
			}

			g_free (name);
			g_free (full_name);
		}

		first = first && !added;
		g_clear_object (&store);
		g_free (store_name);
	}

	gtk_widget_show_all (self->priv->rail);
	oma_rail_sync_selection (self);

	return G_SOURCE_REMOVE;
}

/* Unread changes only restyle existing buttons. */
static gboolean
oma_rail_refresh_unread_idle_cb (gpointer user_data)
{
	EMailShellSidebar *self = user_data;
	GList *children, *link;

	self->priv->rail_unread_idle_id = 0;
	if (!self->priv->rail)
		return G_SOURCE_REMOVE;

	children = gtk_container_get_children (GTK_CONTAINER (self->priv->rail));
	for (link = children; link; link = g_list_next (link)) {
		GtkTreeRowReference *row = g_object_get_data (G_OBJECT (link->data), "oma-row");
		const gchar *label = g_object_get_data (G_OBJECT (link->data), "oma-label");
		GtkStyleContext *context = gtk_widget_get_style_context (link->data);
		GtkTreePath *path = row ? gtk_tree_row_reference_get_path (row) : NULL;
		GtkTreeModel *model = row ? gtk_tree_row_reference_get_model (row) : NULL;
		GtkTreeIter iter;
		guint unread = 0;

		if (path && gtk_tree_model_get_iter (model, &iter, path)) {
			gchar *text;

			gtk_tree_model_get (model, &iter, COL_UINT_UNREAD, &unread, -1);
			if (unread > 0)
				gtk_style_context_add_class (context, "unread");
			else
				gtk_style_context_remove_class (context, "unread");
			text = unread > 0 ? g_strdup_printf ("%s — %u unread", label, unread) : g_strdup (label);
			gtk_widget_set_tooltip_text (link->data, text);
			g_free (text);
		}
		gtk_tree_path_free (path);
	}
	g_list_free (children);

	return G_SOURCE_REMOVE;
}

static void
oma_rail_queue_refresh_unread (EMailShellSidebar *self)
{
	if (!self->priv->rail_unread_idle_id)
		self->priv->rail_unread_idle_id = g_idle_add (oma_rail_refresh_unread_idle_cb, self);
}

static void
oma_rail_queue_rebuild (EMailShellSidebar *self)
{
	if (!self->priv->rail_idle_id)
		self->priv->rail_idle_id = g_idle_add (oma_rail_rebuild_idle_cb, self);
}

enum {
	PROP_0,
	PROP_FOLDER_TREE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EMailShellSidebar, e_mail_shell_sidebar, E_TYPE_SHELL_SIDEBAR, 0,
	G_ADD_PRIVATE_DYNAMIC (EMailShellSidebar))

static void
mail_shell_sidebar_selection_changed_cb (EShellSidebar *shell_sidebar,
                                         GtkTreeSelection *selection)
{
	EShellView *shell_view;
	EShellViewClass *shell_view_class;
	GtkTreeModel *model;
	GtkTreeIter iter;
	const gchar *icon_name = NULL;
	GtkWidget *widget;
	GIcon *custom_icon = NULL;
	gchar *set_icon_name = NULL;
	gchar *display_name = NULL;
	gboolean is_folder = FALSE;
	guint flags = 0;

	shell_view = e_shell_sidebar_get_shell_view (shell_sidebar);
	shell_view_class = E_SHELL_VIEW_GET_CLASS (shell_view);

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (
			model, &iter,
			COL_STRING_DISPLAY_NAME, &display_name,
			COL_BOOL_IS_FOLDER, &is_folder,
			COL_UINT_FLAGS, &flags,
			COL_STRING_ICON_NAME, &set_icon_name,
			COL_GICON_CUSTOM_ICON, &custom_icon,
			-1);
	}

	if (is_folder) {
		if (!custom_icon) {
			if (set_icon_name && *set_icon_name)
				icon_name = set_icon_name;
			else
				icon_name = em_folder_utils_get_icon_name (flags);
		}
	} else {
		g_free (display_name);

		icon_name = shell_view_class->icon_name;
		display_name = g_strdup (shell_view_class->label);
	}

	widget = e_shell_sidebar_get_image_widget (shell_sidebar);

	if (custom_icon) {
		gtk_image_set_from_gicon (GTK_IMAGE (widget), custom_icon, GTK_ICON_SIZE_MENU);
	} else if (gtk_image_get_storage_type (GTK_IMAGE (widget)) == GTK_IMAGE_GICON &&
		   g_strcmp0 (icon_name, e_shell_sidebar_get_icon_name (shell_sidebar)) == 0) {
		/* When the custom icon is unset and the icon name matches, emit the signal here,
		   because it won't update the image icon in the binding handler otherwise. */
		g_object_notify (G_OBJECT (shell_sidebar), "icon-name");
	} else {
		e_shell_sidebar_set_icon_name (shell_sidebar, icon_name);
	}

	e_shell_sidebar_set_primary_text (shell_sidebar, display_name);

	g_clear_object (&custom_icon);
	g_free (set_icon_name);
	g_free (display_name);
}

static void
mail_shell_sidebar_model_row_changed_cb (GtkTreeModel *model,
					 GtkTreePath *path,
					 GtkTreeIter *iter,
					 gpointer user_data)
{
	EMailShellSidebar *mail_shell_sidebar = user_data;
	GtkTreeSelection *selection;

	g_return_if_fail (E_IS_MAIL_SHELL_SIDEBAR (mail_shell_sidebar));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (mail_shell_sidebar->priv->folder_tree));

	if (gtk_tree_selection_iter_is_selected (selection, iter))
		mail_shell_sidebar_selection_changed_cb (E_SHELL_SIDEBAR (mail_shell_sidebar), selection);
}

static gboolean
mail_shell_sidebar_tree_view_key_press_cb (GtkWidget *widget,
					   GdkEventKey *key_event,
					   gpointer user_data)
{
	EMailShellSidebar *mail_shell_sidebar = user_data;

	g_return_val_if_fail (E_IS_MAIL_SHELL_SIDEBAR (mail_shell_sidebar), FALSE);

	if ((key_event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0 &&
	    (key_event->keyval == GDK_KEY_Return || key_event->keyval == GDK_KEY_KP_Enter) &&
	    gtk_widget_has_focus (widget)) {
		EShellView *shell_view;
		EShellContent *shell_content;
		EMailView *mail_view;
		GtkWidget *message_list;

		shell_view = e_shell_sidebar_get_shell_view (E_SHELL_SIDEBAR (mail_shell_sidebar));
		shell_content = e_shell_view_get_shell_content (shell_view);
		mail_view = e_mail_shell_content_get_mail_view (E_MAIL_SHELL_CONTENT (shell_content));
		message_list = e_mail_reader_get_message_list (E_MAIL_READER (mail_view));

		gtk_widget_grab_focus (message_list);
	}

	return FALSE;
}

static void
mail_shell_sidebar_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_FOLDER_TREE:
			g_value_set_object (
				value, e_mail_shell_sidebar_get_folder_tree (
				E_MAIL_SHELL_SIDEBAR (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_shell_sidebar_dispose (GObject *object)
{
	EMailShellSidebar *self = E_MAIL_SHELL_SIDEBAR (object);

	if (self->priv->rail_idle_id) {
		g_source_remove (self->priv->rail_idle_id);
		self->priv->rail_idle_id = 0;
	}
	if (self->priv->rail_unread_idle_id) {
		g_source_remove (self->priv->rail_unread_idle_id);
		self->priv->rail_unread_idle_id = 0;
	}
	self->priv->rail = NULL;

	if (self->priv->folder_tree != NULL) {
		GtkTreeModel *model;

		model = gtk_tree_view_get_model (GTK_TREE_VIEW (self->priv->folder_tree));

		if (model) {
			g_signal_handlers_disconnect_by_func (model,
				mail_shell_sidebar_model_row_changed_cb, object);
			g_signal_handlers_disconnect_by_func (model,
				oma_rail_queue_rebuild, object);
			g_signal_handlers_disconnect_by_func (model,
				oma_rail_queue_refresh_unread, object);
		}
		g_signal_handlers_disconnect_by_func (self->priv->folder_tree,
			oma_rail_sync_selection, object);

		g_clear_object (&self->priv->folder_tree);
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_shell_sidebar_parent_class)->dispose (object);
}

static void
mail_shell_sidebar_constructed (GObject *object)
{
	EMailShellSidebar *mail_shell_sidebar;
	EShellBackend *shell_backend;
	EShellSidebar *shell_sidebar;
	EShellView *shell_view;
	EMailBackend *backend;
	EMailSession *session;
	EAlertSink *alert_sink;
	GtkTreeSelection *selection;
	GtkTreeView *tree_view;
	GtkWidget *container;
	GtkWidget *widget;
	GSettings *settings;

	/* Chain up to parent's constructed method. */
	G_OBJECT_CLASS (e_mail_shell_sidebar_parent_class)->constructed (object);

	shell_sidebar = E_SHELL_SIDEBAR (object);
	shell_view = e_shell_sidebar_get_shell_view (shell_sidebar);
	shell_backend = e_shell_view_get_shell_backend (shell_view);

	backend = E_MAIL_BACKEND (shell_backend);
	session = e_mail_backend_get_session (backend);

	alert_sink = E_ALERT_SINK (shell_sidebar);

	mail_shell_sidebar = E_MAIL_SHELL_SIDEBAR (object);

	/* Build sidebar widgets. */

	container = GTK_WIDGET (object);

	/* Full folder tree and the folded rail share the sidebar; the shell
	 * view shows one or the other (see shell_view_compact_sidebar_cb). */
	widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (container), widget);
	gtk_widget_show (widget);
	container = widget;

	widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_name (widget, "omamail-rail");
	gtk_widget_set_no_show_all (widget, TRUE);
	gtk_box_pack_end (GTK_BOX (container), widget, TRUE, TRUE, 0);
	mail_shell_sidebar->priv->rail = widget;
	g_object_set_data (object, "oma-compact-rail", widget);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (
		GTK_SCROLLED_WINDOW (widget),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (container), widget, TRUE, TRUE, 0);
	gtk_widget_show (widget);
	g_object_set_data (object, "oma-full-content", widget);

	container = widget;

	widget = e_mail_sidebar_new (session, alert_sink);
	gtk_container_add (GTK_CONTAINER (container), widget);
	mail_shell_sidebar->priv->folder_tree = g_object_ref (widget);
	gtk_widget_show (widget);

	e_binding_bind_property (
		shell_view, "state-key-file",
		widget, "key-file",
		G_BINDING_SYNC_CREATE);

	settings = e_util_ref_settings ("org.gnome.evolution.mail");

	g_settings_bind (
		settings, "side-bar-search",
		widget, "enable-search",
		G_SETTINGS_BIND_GET);

	g_object_unref (settings);

	g_signal_connect_swapped (
		widget, "key-file-changed",
		G_CALLBACK (e_shell_view_set_state_dirty), shell_view);

	tree_view = GTK_TREE_VIEW (mail_shell_sidebar->priv->folder_tree);
	selection = gtk_tree_view_get_selection (tree_view);

	g_signal_connect_swapped (
		selection, "changed",
		G_CALLBACK (mail_shell_sidebar_selection_changed_cb),
		shell_sidebar);

	g_signal_connect (gtk_tree_view_get_model (tree_view), "row-changed",
		G_CALLBACK (mail_shell_sidebar_model_row_changed_cb), shell_sidebar);

	g_signal_connect (tree_view, "key-press-event",
		G_CALLBACK (mail_shell_sidebar_tree_view_key_press_cb), shell_sidebar);

	g_signal_connect_swapped (gtk_tree_view_get_model (tree_view), "row-inserted",
		G_CALLBACK (oma_rail_queue_rebuild), object);
	g_signal_connect_swapped (gtk_tree_view_get_model (tree_view), "row-deleted",
		G_CALLBACK (oma_rail_queue_rebuild), object);
	g_signal_connect_swapped (gtk_tree_view_get_model (tree_view), "row-changed",
		G_CALLBACK (oma_rail_queue_refresh_unread), object);
	g_signal_connect_swapped (tree_view, "folder-selected",
		G_CALLBACK (oma_rail_sync_selection), object);
	oma_rail_queue_rebuild (mail_shell_sidebar);
}

static gint
guess_screen_width (EMailShellSidebar *sidebar)
{
	GtkWidget *widget = GTK_WIDGET (sidebar);
	GdkDisplay *display;
	GtkWidget *toplevel;
	GdkRectangle rect;

	display = gtk_widget_get_display (widget);
	toplevel = gtk_widget_get_toplevel (widget);
	if (toplevel && gtk_widget_get_realized (toplevel)) {
		GdkMonitor *monitor;

		monitor = gdk_display_get_monitor_at_window (
			display, gtk_widget_get_window (toplevel));
		gdk_monitor_get_workarea (monitor, &rect);

		return rect.width;
	}

	return 1024;
}

static void
mail_shell_sidebar_get_preferred_height (GtkWidget *widget,
                                         gint *minimum_height,
                                         gint *natural_height)
{
	GTK_WIDGET_CLASS (e_mail_shell_sidebar_parent_class)->
		get_preferred_height (widget, minimum_height, natural_height);
}

static void
mail_shell_sidebar_get_preferred_width (GtkWidget *widget,
                                        gint *minimum_width,
                                        gint *natural_width)
{
	/* We override the normal size-request handler so that we can
	 * spit out a treeview with a suitable width.  We measure the
	 * length of a typical string and use that as the requisition's
	 * width.
	 *
	 * EMFolderTreeClass, our parent class, is based on GtkTreeView,
	 * which doesn't really have a good way of figuring out a minimum
	 * width for the tree.  This is really GTK+'s fault at large, as
	 * it only has "minimum size / allocated size", instead of
	 * "minimum size / preferred size / allocated size".  Hopefully
	 * the extended-layout branch of GTK+ will get merged soon and
	 * then we can remove this crap.
	 */

	EMailShellSidebar *sidebar;
	PangoLayout *layout;
	PangoRectangle ink_rect;
	GtkBorder padding;
	GtkStyleContext *style_context;
	gint border;
	gint sidebar_width;
	gint screen_width;

	sidebar = E_MAIL_SHELL_SIDEBAR (widget);

	GTK_WIDGET_CLASS (e_mail_shell_sidebar_parent_class)->
		get_preferred_width (widget, minimum_width, natural_width);

	/* The folded rail is an icon strip; don't widen it to fit a name. */
	if (sidebar->priv->rail && gtk_widget_get_visible (sidebar->priv->rail))
		return;

	/* This string is a mockup only; it doesn't need to be translated */
	layout = gtk_widget_create_pango_layout (
		widget, "Account Name");
	pango_layout_get_pixel_extents (layout, &ink_rect, NULL);
	g_object_unref (layout);

	style_context = gtk_widget_get_style_context (widget);
	gtk_style_context_get_padding (style_context, gtk_style_context_get_state (style_context), &padding);

	screen_width = guess_screen_width (sidebar);

	/* Thickness of frame shadow plus some slack for padding. */
	border = 2 * padding.left + 4;
	sidebar_width = ink_rect.width + border;
	sidebar_width = MIN (sidebar_width, screen_width / 4);
	*minimum_width = *natural_width = MAX (*natural_width, sidebar_width);
}

static guint32
mail_shell_sidebar_check_state (EShellSidebar *shell_sidebar)
{
	EMailShellSidebar *self;
	EMailSidebar *sidebar;

	self = E_MAIL_SHELL_SIDEBAR (shell_sidebar);
	sidebar = E_MAIL_SIDEBAR (self->priv->folder_tree);

	return e_mail_sidebar_check_state (sidebar);
}

static void
e_mail_shell_sidebar_class_init (EMailShellSidebarClass *class)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;
	EShellSidebarClass *shell_sidebar_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = mail_shell_sidebar_get_property;
	object_class->dispose = mail_shell_sidebar_dispose;
	object_class->constructed = mail_shell_sidebar_constructed;

	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->get_preferred_width = mail_shell_sidebar_get_preferred_width;
	widget_class->get_preferred_height = mail_shell_sidebar_get_preferred_height;

	shell_sidebar_class = E_SHELL_SIDEBAR_CLASS (class);
	shell_sidebar_class->check_state = mail_shell_sidebar_check_state;

	properties[PROP_FOLDER_TREE] =
		g_param_spec_object (
			"folder-tree", NULL, NULL,
			EM_TYPE_FOLDER_TREE,
			G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
e_mail_shell_sidebar_class_finalize (EMailShellSidebarClass *class)
{
}

static void
e_mail_shell_sidebar_init (EMailShellSidebar *mail_shell_sidebar)
{
	mail_shell_sidebar->priv = e_mail_shell_sidebar_get_instance_private (mail_shell_sidebar);

	/* Postpone widget construction until we have a shell view. */
}

void
e_mail_shell_sidebar_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_mail_shell_sidebar_register_type (type_module);
}

GtkWidget *
e_mail_shell_sidebar_new (EShellView *shell_view)
{
	g_return_val_if_fail (E_IS_SHELL_VIEW (shell_view), NULL);

	return g_object_new (
		E_TYPE_MAIL_SHELL_SIDEBAR,
		"shell-view", shell_view, NULL);
}

EMFolderTree *
e_mail_shell_sidebar_get_folder_tree (EMailShellSidebar *mail_shell_sidebar)
{
	g_return_val_if_fail (
		E_IS_MAIL_SHELL_SIDEBAR (mail_shell_sidebar), NULL);

	return EM_FOLDER_TREE (mail_shell_sidebar->priv->folder_tree);
}
