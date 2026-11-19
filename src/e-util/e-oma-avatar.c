/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sender avatars for the mail list.
 *
 * Every sender gets a 32px circle: the sender domain's own site icon when
 * one can be found, otherwise a colored initial. Icons are fetched straight
 * from the sender's domain (never through a third-party icon service), once
 * per domain, and cached on disk. Personal mail providers (gmail.com, ...)
 * are skipped, since their logo would stand in for every person there.
 */

#include "evolution-config.h"

#include <string.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <libedataserver/libedataserver.h>

#include "e-oma-avatar.h"

#define OMA_AVATAR_MAX_ACTIVE 4
#define OMA_AVATAR_MAX_BYTES (2 * 1024 * 1024)
#define OMA_AVATAR_NONE_TTL (7 * 24 * 60 * 60)

typedef enum {
	STEP_TOUCH_ICON,	/* https://domain/apple-touch-icon.png */
	STEP_HOME_PAGE,		/* <link rel="...icon"> on https://domain/ */
	STEP_LINKED_ICON,	/* the icon that link points to */
	STEP_FAVICON,		/* https://domain/favicon.ico */
	STEP_DONE
} FetchStep;

typedef enum {
	STATE_PENDING,
	STATE_READY,
	STATE_NONE
} AvatarState;

typedef struct {
	gchar *domain;
	AvatarState state;
	FetchStep step;
	gchar *linked_icon; /* resolved from the home page */
	GdkPixbuf *pixbuf;
	gboolean full_bleed; /* large touch icons fill the circle */
} AvatarEntry;

static GHashTable *entries;	/* domain ~> AvatarEntry */
static GHashTable *widgets;	/* GtkWidget * set to redraw on load */
static GQueue pending = G_QUEUE_INIT;
static guint active;
static SoupSession *session;

static const gchar *personal_domains[] = {
	"gmail.com", "googlemail.com", "outlook.com", "hotmail.com", "live.com",
	"msn.com", "yahoo.com", "ymail.com", "icloud.com", "me.com", "mac.com",
	"aol.com", "proton.me", "protonmail.com", "pm.me", "gmx.com", "gmx.de",
	"mail.com", "yandex.com", "yandex.ru", "zoho.com", "fastmail.com",
	"hey.com", "tutanota.com", "qq.com", "163.com"
};

static void fetch_next_step (AvatarEntry *entry);

static gchar *
cache_path (const gchar *domain,
            const gchar *suffix)
{
	gchar *name = g_strconcat (domain, suffix, NULL);
	gchar *path = g_build_filename (e_get_user_cache_dir (), "oma-avatars", name, NULL);

	g_free (name);
	return path;
}

/* notifications.github.com -> github.com; shop.example.co.uk -> example.co.uk */
static gchar *
registrable_domain (const gchar *host)
{
	gchar **labels = g_strsplit (host, ".", -1);
	guint n = g_strv_length (labels), keep = 2;
	gchar *result;

	if (n >= 3 && strlen (labels[n - 1]) == 2 &&
	    g_strv_contains ((const gchar * const[]) { "co", "com", "org", "net", "ac", "gov", "edu", NULL }, labels[n - 2]))
		keep = 3;

	result = n <= keep ? g_strdup (host) : g_strjoinv (".", labels + (n - keep));
	g_strfreev (labels);
	return result;
}

static gchar *
domain_for_email (const gchar *email)
{
	const gchar *at;
	gchar *host, *domain;
	guint ii;

	if (!email || !(at = strrchr (email, '@')) || !at[1])
		return NULL;

	host = g_ascii_strdown (at + 1, -1);
	g_strstrip (host);
	g_strdelimit (host, ">", '\0');

	if (!*host || !strchr (host, '.')) {
		g_free (host);
		return NULL;
	}

	domain = registrable_domain (host);
	g_free (host);

	for (ii = 0; ii < G_N_ELEMENTS (personal_domains); ii++) {
		if (g_str_equal (domain, personal_domains[ii])) {
			g_free (domain);
			return NULL;
		}
	}

	return domain;
}

static void
redraw_widgets (void)
{
	GHashTableIter iter;
	gpointer key;

	if (!widgets)
		return;

	g_hash_table_iter_init (&iter, widgets);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		gtk_widget_queue_draw (key);
}

static void
widget_destroyed_cb (GtkWidget *widget,
                     gpointer user_data)
{
	if (widgets)
		g_hash_table_remove (widgets, widget);
}

static void
remember_widget (GtkWidget *widget)
{
	if (!widget)
		return;

	if (!widgets)
		widgets = g_hash_table_new (g_direct_hash, g_direct_equal);

	if (!g_hash_table_contains (widgets, widget)) {
		g_hash_table_add (widgets, widget);
		g_signal_connect (widget, "destroy", G_CALLBACK (widget_destroyed_cb), NULL);
	}
}

static void
entry_set_pixbuf (AvatarEntry *entry,
                  GdkPixbuf *pixbuf)
{
	gint w = gdk_pixbuf_get_width (pixbuf), h = gdk_pixbuf_get_height (pixbuf);

	/* Keep the cache small: nothing larger than 128px is ever drawn. */
	if (MAX (w, h) > 128) {
		gdouble scale = 128.0 / MAX (w, h);
		entry->pixbuf = gdk_pixbuf_scale_simple (pixbuf, MAX (1, w * scale), MAX (1, h * scale), GDK_INTERP_BILINEAR);
	} else {
		entry->pixbuf = g_object_ref (pixbuf);
	}

	entry->full_bleed = MIN (w, h) >= 64;
	entry->state = STATE_READY;
}

static GdkPixbuf *
decode_image (GBytes *bytes)
{
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new ();
	GdkPixbuf *pixbuf = NULL;
	gsize len = 0;
	gconstpointer data = g_bytes_get_data (bytes, &len);

	if (len > 0 && len <= OMA_AVATAR_MAX_BYTES &&
	    gdk_pixbuf_loader_write (loader, data, len, NULL) &&
	    gdk_pixbuf_loader_close (loader, NULL)) {
		pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
		if (pixbuf && (gdk_pixbuf_get_width (pixbuf) < 16 || gdk_pixbuf_get_height (pixbuf) < 16))
			pixbuf = NULL;
		if (pixbuf)
			g_object_ref (pixbuf);
	} else {
		gdk_pixbuf_loader_close (loader, NULL);
	}

	g_object_unref (loader);
	return pixbuf;
}

/* Best icon link on a home page: apple-touch-icon, then PNG/SVG icon, then any icon. */
static gchar *
find_linked_icon (GBytes *bytes,
                  const gchar *base_uri)
{
	GRegex *link_re, *rel_re, *href_re;
	GMatchInfo *match = NULL;
	gchar *html, *best = NULL;
	gint best_score = 0;
	gsize len = 0;
	const gchar *data = g_bytes_get_data (bytes, &len);

	if (!data || !len)
		return NULL;

	html = g_strndup (data, MIN (len, 512 * 1024));
	link_re = g_regex_new ("<link\\b[^>]*>", G_REGEX_CASELESS, 0, NULL);
	rel_re = g_regex_new ("\\brel\\s*=\\s*[\"']?([^\"'>]*)", G_REGEX_CASELESS, 0, NULL);
	href_re = g_regex_new ("\\bhref\\s*=\\s*[\"']?([^\"' >]*)", G_REGEX_CASELESS, 0, NULL);

	g_regex_match (link_re, html, 0, &match);
	while (g_match_info_matches (match)) {
		gchar *tag = g_match_info_fetch (match, 0);
		GMatchInfo *rel_m = NULL, *href_m = NULL;

		if (g_regex_match (rel_re, tag, 0, &rel_m) && g_regex_match (href_re, tag, 0, &href_m)) {
			gchar *rel = g_ascii_strdown (g_match_info_fetch (rel_m, 1), -1);
			gchar *href = g_match_info_fetch (href_m, 1);
			gint score = 0;

			if (strstr (rel, "apple-touch-icon"))
				score = 3;
			else if (strstr (rel, "icon"))
				score = (g_str_has_suffix (href, ".png") || g_str_has_suffix (href, ".svg")) ? 2 : 1;

			if (score > best_score && href && *href) {
				gchar *resolved = g_uri_resolve_relative (base_uri, href, G_URI_FLAGS_NONE, NULL);

				if (resolved && g_str_has_prefix (resolved, "https://")) {
					g_free (best);
					best = resolved;
					best_score = score;
				} else {
					g_free (resolved);
				}
			}

			g_free (rel);
			g_free (href);
		}

		g_clear_pointer (&rel_m, g_match_info_free);
		g_clear_pointer (&href_m, g_match_info_free);
		g_free (tag);
		g_match_info_next (match, NULL);
	}

	g_match_info_free (match);
	g_regex_unref (link_re);
	g_regex_unref (rel_re);
	g_regex_unref (href_re);
	g_free (html);

	return best;
}

static void
entry_finished (AvatarEntry *entry,
                gboolean definitive)
{
	gchar *path;

	if (entry->state == STATE_READY) {
		path = cache_path (entry->domain, ".png");
		gdk_pixbuf_save (entry->pixbuf, path, "png", NULL, NULL);
		g_free (path);
	} else {
		entry->state = STATE_NONE;
		/* Remember "no icon" only when the site answered; network
		 * errors are retried on the next start. */
		if (definitive) {
			path = cache_path (entry->domain, ".none");
			g_file_set_contents (path, "", 0, NULL);
			g_free (path);
		}
	}

	active--;
	redraw_widgets ();

	while (active < OMA_AVATAR_MAX_ACTIVE && !g_queue_is_empty (&pending)) {
		active++;
		fetch_next_step (g_queue_pop_head (&pending));
	}
}

static void
fetch_done_cb (GObject *source,
               GAsyncResult *result,
               gpointer user_data)
{
	AvatarEntry *entry = user_data;
	SoupMessage *msg = soup_session_get_async_result_message (SOUP_SESSION (source), result);
	GError *error = NULL;
	GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), result, &error);
	gboolean ok = bytes && msg && SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (msg));
	FetchStep next;

	/* Offline: don't remember a miss, try again on the next start. */
	if (error && !g_network_monitor_get_network_available (g_network_monitor_get_default ())) {
		g_clear_error (&error);
		g_clear_pointer (&bytes, g_bytes_unref);
		entry->step = STEP_DONE;
		entry_finished (entry, FALSE);
		return;
	}
	g_clear_error (&error);

	switch (entry->step) {
	case STEP_TOUCH_ICON:
		next = STEP_HOME_PAGE;
		break;
	case STEP_HOME_PAGE:
		next = STEP_FAVICON;
		if (ok) {
			gchar *base = g_uri_to_string (soup_message_get_uri (msg));

			g_clear_pointer (&entry->linked_icon, g_free);
			entry->linked_icon = find_linked_icon (bytes, base);
			g_free (base);
			if (entry->linked_icon)
				next = STEP_LINKED_ICON;
		}
		break;
	case STEP_LINKED_ICON:
		next = STEP_FAVICON;
		break;
	default:
		next = STEP_DONE;
		break;
	}

	if (ok && entry->step != STEP_HOME_PAGE) {
		GdkPixbuf *pixbuf = decode_image (bytes);

		if (pixbuf) {
			entry_set_pixbuf (entry, pixbuf);
			g_object_unref (pixbuf);
		}
	}

	g_clear_pointer (&bytes, g_bytes_unref);

	if (entry->state == STATE_READY || next == STEP_DONE) {
		entry->step = STEP_DONE;
		entry_finished (entry, TRUE);
		return;
	}

	entry->step = next;
	fetch_next_step (entry);
}

static void
fetch_next_step (AvatarEntry *entry)
{
	SoupMessage *msg;
	gchar *uri;

	if (!session) {
		session = soup_session_new_with_options (
			"timeout", 15,
			"user-agent", "Mozilla/5.0 (X11; Linux) Evolution",
			NULL);
	}

	switch (entry->step) {
	case STEP_TOUCH_ICON:
		uri = g_strdup_printf ("https://%s/apple-touch-icon.png", entry->domain);
		break;
	case STEP_HOME_PAGE:
		uri = g_strdup_printf ("https://%s/", entry->domain);
		break;
	case STEP_LINKED_ICON:
		uri = g_strdup (entry->linked_icon);
		break;
	case STEP_FAVICON:
		uri = g_strdup_printf ("https://%s/favicon.ico", entry->domain);
		break;
	default:
		entry_finished (entry, TRUE);
		return;
	}

	msg = soup_message_new ("GET", uri);
	g_free (uri);

	if (!msg) {
		entry->step = (entry->step == STEP_FAVICON) ? STEP_DONE : STEP_FAVICON;
		if (entry->step == STEP_DONE)
			entry_finished (entry, TRUE);
		else
			fetch_next_step (entry);
		return;
	}

	soup_session_send_and_read_async (session, msg, G_PRIORITY_LOW, NULL, fetch_done_cb, entry);
	g_object_unref (msg);
}

static AvatarEntry *
lookup_entry (const gchar *domain)
{
	AvatarEntry *entry;
	gchar *path;
	GStatBuf st;

	if (!entries)
		entries = g_hash_table_new (g_str_hash, g_str_equal);

	entry = g_hash_table_lookup (entries, domain);
	if (entry)
		return entry;

	entry = g_new0 (AvatarEntry, 1);
	entry->domain = g_strdup (domain);
	g_hash_table_insert (entries, entry->domain, entry);

	path = cache_path (domain, ".png");
	if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, NULL);

		if (pixbuf) {
			entry_set_pixbuf (entry, pixbuf);
			g_object_unref (pixbuf);
		}
	}
	g_free (path);

	if (entry->state == STATE_READY)
		return entry;

	path = cache_path (domain, ".none");
	if (g_stat (path, &st) == 0 && (g_get_real_time () / G_USEC_PER_SEC) - st.st_mtime < OMA_AVATAR_NONE_TTL) {
		entry->state = STATE_NONE;
		g_free (path);
		return entry;
	}
	g_free (path);

	path = g_build_filename (e_get_user_cache_dir (), "oma-avatars", NULL);
	g_mkdir_with_parents (path, 0700);
	g_free (path);

	entry->state = STATE_PENDING;
	entry->step = STEP_TOUCH_ICON;
	if (active < OMA_AVATAR_MAX_ACTIVE) {
		active++;
		fetch_next_step (entry);
	} else {
		g_queue_push_tail (&pending, entry);
	}

	return entry;
}

static void
draw_placeholder (cairo_t *cr,
                  gdouble x,
                  gdouble y,
                  gdouble size,
                  const gchar *email,
                  const gchar *name,
                  GtkWidget *widget)
{
	/* Tokyo Night accents; a stable pick per sender. */
	static const guint32 palette[] = {
		0x7aa2f7, 0xbb9af7, 0x7dcfff, 0x9ece6a, 0xff9e64, 0xf7768e, 0xe0af68, 0x73daca
	};
	const gchar *source = (name && *name) ? name : (email ? email : "?");
	const gchar *p;
	gunichar ch = '?';
	guint32 rgb;
	gchar text[8] = { 0 };

	for (p = source; p && *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);
		if (g_unichar_isalnum (c)) {
			ch = g_unichar_toupper (c);
			break;
		}
	}
	g_unichar_to_utf8 (ch, text);
	rgb = palette[g_str_hash (email ? email : source) % G_N_ELEMENTS (palette)];

	cairo_arc (cr, x + size / 2, y + size / 2, size / 2, 0, 2 * G_PI);
	cairo_set_source_rgba (cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0, 0.20);
	cairo_fill (cr);

	if (widget) {
		PangoLayout *layout = gtk_widget_create_pango_layout (widget, text);
		PangoFontDescription *desc = pango_font_description_copy (
			pango_context_get_font_description (pango_layout_get_context (layout)));
		gint tw, th;

		pango_font_description_set_weight (desc, PANGO_WEIGHT_BOLD);
		pango_font_description_set_absolute_size (desc, size * 0.42 * PANGO_SCALE);
		pango_layout_set_font_description (layout, desc);
		pango_layout_get_pixel_size (layout, &tw, &th);
		cairo_set_source_rgb (cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
		cairo_move_to (cr, x + (size - tw) / 2.0, y + (size - th) / 2.0);
		pango_cairo_show_layout (cr, layout);
		pango_font_description_free (desc);
		g_object_unref (layout);
	}
}

void
e_oma_avatar_draw (cairo_t *cr,
                   gdouble x,
                   gdouble y,
                   gdouble size,
                   const gchar *email,
                   const gchar *name,
                   GtkWidget *redraw_widget)
{
	gchar *domain = domain_for_email (email);
	AvatarEntry *entry = domain ? lookup_entry (domain) : NULL;

	g_free (domain);

	cairo_save (cr);

	if (entry && entry->state == STATE_READY) {
		gint w = gdk_pixbuf_get_width (entry->pixbuf), h = gdk_pixbuf_get_height (entry->pixbuf);
		gdouble inner = entry->full_bleed ? size : size * 0.62;
		gdouble scale = inner / MAX (w, h);

		cairo_arc (cr, x + size / 2, y + size / 2, size / 2, 0, 2 * G_PI);
		cairo_clip (cr);
		/* A soft light disk keeps dark logos visible on the dark list. */
		cairo_set_source_rgb (cr, 0xdf / 255.0, 0xe3 / 255.0, 0xef / 255.0);
		cairo_paint (cr);
		cairo_translate (cr, x + (size - w * scale) / 2, y + (size - h * scale) / 2);
		cairo_scale (cr, scale, scale);
		gdk_cairo_set_source_pixbuf (cr, entry->pixbuf, 0, 0);
		cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
		cairo_paint (cr);
	} else {
		if (entry && entry->state == STATE_PENDING)
			remember_widget (redraw_widget);
		draw_placeholder (cr, x, y, size, email, name, redraw_widget);
	}

	cairo_restore (cr);
}
