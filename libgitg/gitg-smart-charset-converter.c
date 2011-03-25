/*
 * gedit-smart-charset-converter.c
 * This file is part of gedit
 *
 * Copyright (C) 2009 - Ignacio Casal Quinteiro
 *
 * gedit is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * gedit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gedit; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */

#include "gitg-smart-charset-converter.h"
#include "gitg-debug.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#define GITG_SMART_CHARSET_CONVERTER_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE((object), GITG_TYPE_SMART_CHARSET_CONVERTER, GitgSmartCharsetConverterPrivate))

struct _GitgSmartCharsetConverterPrivate
{
	GCharsetConverter *charset_conv;

	GSList *encodings;
	GSList *current_encoding;

	guint is_utf8 : 1;
	guint use_first : 1;
};

static void gitg_smart_charset_converter_iface_init    (GConverterIface *iface);

G_DEFINE_TYPE_WITH_CODE (GitgSmartCharsetConverter, gitg_smart_charset_converter,
			 G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER,
						gitg_smart_charset_converter_iface_init))

GQuark
gitg_charset_conversion_error_quark (void)
{
	static GQuark ret = 0;

	if (G_UNLIKELY (ret == 0))
	{
		ret = g_quark_from_static_string ("GitgCharsetConversionError");
	}

	return ret;
}

static void
gitg_smart_charset_converter_finalize (GObject *object)
{
	GitgSmartCharsetConverter *smart = GITG_SMART_CHARSET_CONVERTER (object);

	g_slist_free (smart->priv->encodings);

	G_OBJECT_CLASS (gitg_smart_charset_converter_parent_class)->finalize (object);
}

static void
gitg_smart_charset_converter_dispose (GObject *object)
{
	GitgSmartCharsetConverter *smart = GITG_SMART_CHARSET_CONVERTER (object);

	if (smart->priv->charset_conv != NULL)
	{
		g_object_unref (smart->priv->charset_conv);
		smart->priv->charset_conv = NULL;
	}

	G_OBJECT_CLASS (gitg_smart_charset_converter_parent_class)->dispose (object);
}

static void
gitg_smart_charset_converter_class_init (GitgSmartCharsetConverterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gitg_smart_charset_converter_finalize;
	object_class->dispose = gitg_smart_charset_converter_dispose;

	g_type_class_add_private (object_class, sizeof (GitgSmartCharsetConverterPrivate));
}

static void
gitg_smart_charset_converter_init (GitgSmartCharsetConverter *smart)
{
	smart->priv = GITG_SMART_CHARSET_CONVERTER_GET_PRIVATE (smart);

	smart->priv->charset_conv = NULL;
	smart->priv->encodings = NULL;
	smart->priv->current_encoding = NULL;
	smart->priv->is_utf8 = FALSE;
	smart->priv->use_first = FALSE;
}

static const GitgEncoding *
get_encoding (GitgSmartCharsetConverter *smart)
{
	if (smart->priv->current_encoding == NULL)
	{
		smart->priv->current_encoding = smart->priv->encodings;
	}
	else
	{
		smart->priv->current_encoding = g_slist_next (smart->priv->current_encoding);
	}

	if (smart->priv->current_encoding != NULL)
		return (const GitgEncoding *)smart->priv->current_encoding->data;

#if 0
	FIXME: uncomment this when using fallback
	/* If we tried all encodings, we return the first encoding */
	smart->priv->use_first = TRUE;
	smart->priv->current_encoding = smart->priv->encodings;

	return (const GitgEncoding *)smart->priv->current_encoding->data;
#endif
	return NULL;
}

static gboolean
try_convert (GCharsetConverter *converter,
             const void        *inbuf,
             gsize              inbuf_size)
{
	GError *err;
	gsize bytes_read, nread;
	gsize bytes_written, nwritten;
	GConverterResult res;
	gchar *out;
	gboolean ret;
	gsize out_size;

	if (inbuf == NULL || inbuf_size == 0)
	{
		return FALSE;
	}

	err = NULL;
	nread = 0;
	nwritten = 0;
	out_size = inbuf_size * 4;
	out = g_malloc (out_size);

	do
	{
		res = g_converter_convert (G_CONVERTER (converter),
		                           (gchar *)inbuf + nread,
		                           inbuf_size - nread,
		                           (gchar *)out + nwritten,
		                           out_size - nwritten,
		                           G_CONVERTER_INPUT_AT_END,
		                           &bytes_read,
		                           &bytes_written,
		                           &err);

		nread += bytes_read;
		nwritten += bytes_written;
	} while (res != G_CONVERTER_FINISHED && res != G_CONVERTER_ERROR && err == NULL);

	if (err != NULL)
	{
		if (err->code == G_CONVERT_ERROR_PARTIAL_INPUT)
		{
			/* FIXME We can get partial input while guessing the
			   encoding because we just take some amount of text
			   to guess from. */
			ret = TRUE;
		}
		else
		{
			ret = FALSE;
		}

		g_error_free (err);
	}
	else
	{
		ret = TRUE;
	}

	/* FIXME: Check the remainder? */
	if (ret == TRUE && !g_utf8_validate (out, nwritten, NULL))
	{
		ret = FALSE;
	}

	g_free (out);

	return ret;
}

static GCharsetConverter *
guess_encoding (GitgSmartCharsetConverter *smart,
		const void                 *inbuf,
		gsize                       inbuf_size)
{
	GCharsetConverter *conv = NULL;

	if (inbuf == NULL || inbuf_size == 0)
	{
		smart->priv->is_utf8 = TRUE;
		return NULL;
	}

	if (smart->priv->encodings != NULL &&
	    smart->priv->encodings->next == NULL)
	{
		smart->priv->use_first = TRUE;
	}

	/* We just check the first block */
	while (TRUE)
	{
		const GitgEncoding *enc;

		if (conv != NULL)
		{
			g_object_unref (conv);
			conv = NULL;
		}

		/* We get an encoding from the list */
		enc = get_encoding (smart);

		/* if it is NULL we didn't guess anything */
		if (enc == NULL)
		{
			break;
		}

		if (enc == gitg_encoding_get_utf8 ())
		{
			gsize remainder;
			const gchar *end;
			
			if (g_utf8_validate (inbuf, inbuf_size, &end) ||
			    smart->priv->use_first)
			{
				smart->priv->is_utf8 = TRUE;
				break;
			}

			/* Check if the end is less than one char */
			remainder = inbuf_size - (end - (gchar *)inbuf);
			if (remainder < 6)
			{
				smart->priv->is_utf8 = TRUE;
				break;
			}

			continue;
		}

		conv = g_charset_converter_new ("UTF-8",
						gitg_encoding_get_charset (enc),
						NULL);

		/* If we tried all encodings we use the first one */
		if (smart->priv->use_first)
		{
			break;
		}

		/* Try to convert */
		if (try_convert (conv, inbuf, inbuf_size))
		{
			gitg_debug (GITG_DEBUG_CHARSET_CONVERSION,
			            "Guessed %s conversion",
			            gitg_encoding_get_charset (enc));
			break;
		}
	}

	if (smart->priv->is_utf8)
	{
		gitg_debug (GITG_DEBUG_CHARSET_CONVERSION, "%s", "Guessed UTF8 conversion");
	}

	if (conv != NULL)
	{
		g_converter_reset (G_CONVERTER (conv));

		/* FIXME: uncomment this when we want to use the fallback
		g_charset_converter_set_use_fallback (conv, TRUE);*/
	}

	return conv;
}

static GConverterResult
gitg_smart_charset_converter_convert (GConverter       *converter,
				       const void       *inbuf,
				       gsize             inbuf_size,
				       void             *outbuf,
				       gsize             outbuf_size,
				       GConverterFlags   flags,
				       gsize            *bytes_read,
				       gsize            *bytes_written,
				       GError          **error)
{
	GitgSmartCharsetConverter *smart = GITG_SMART_CHARSET_CONVERTER (converter);

	/* Guess the encoding if we didn't make it yet */
	if (smart->priv->charset_conv == NULL && !smart->priv->is_utf8)
	{
		smart->priv->charset_conv = guess_encoding (smart, inbuf, inbuf_size);

		/* If we still have the previous case is that we didn't guess
		   anything */
		if (smart->priv->charset_conv == NULL && !smart->priv->is_utf8)
		{
			g_set_error_literal (error, GITG_CHARSET_CONVERSION_ERROR,
					     GITG_CHARSET_CONVERSION_ERROR_ENCODING_AUTO_DETECTION_FAILED,
					     _("It is not possible to detect the encoding automatically"));

			return G_CONVERTER_ERROR;
		}
	}

	/* Now if the encoding is utf8 just redirect the input to the output */
	if (smart->priv->is_utf8)
	{
		gsize size;
		GConverterResult ret;

		size = MIN (inbuf_size, outbuf_size);

		memcpy (outbuf, inbuf, size);
		*bytes_read = size;
		*bytes_written = size;

		ret = G_CONVERTER_CONVERTED;

		if (flags & G_CONVERTER_INPUT_AT_END)
		{
			ret = G_CONVERTER_FINISHED;
		}
		else if (flags & G_CONVERTER_FLUSH)
		{
			ret = G_CONVERTER_FLUSHED;
		}

		return ret;
	}

	/* If we reached here is because we need to convert the text so, we
	   convert it with the charset converter */
	return g_converter_convert (G_CONVERTER (smart->priv->charset_conv),
				    inbuf,
				    inbuf_size,
				    outbuf,
				    outbuf_size,
				    flags,
				    bytes_read,
				    bytes_written,
				    error);
}

static void
gitg_smart_charset_converter_reset (GConverter *converter)
{
	GitgSmartCharsetConverter *smart = GITG_SMART_CHARSET_CONVERTER (converter);

	smart->priv->current_encoding = NULL;
	smart->priv->is_utf8 = FALSE;

	if (smart->priv->charset_conv != NULL)
	{
		g_object_unref (smart->priv->charset_conv);
		smart->priv->charset_conv = NULL;
	}
}

static void
gitg_smart_charset_converter_iface_init (GConverterIface *iface)
{
	iface->convert = gitg_smart_charset_converter_convert;
	iface->reset = gitg_smart_charset_converter_reset;
}

GitgSmartCharsetConverter *
gitg_smart_charset_converter_new (GSList *candidate_encodings)
{
	GitgSmartCharsetConverter *smart;

	g_return_val_if_fail (candidate_encodings != NULL, NULL);

	smart = g_object_new (GITG_TYPE_SMART_CHARSET_CONVERTER, NULL);

	smart->priv->encodings = g_slist_copy (candidate_encodings);

	return smart;
}

const GitgEncoding *
gitg_smart_charset_converter_get_guessed (GitgSmartCharsetConverter *smart)
{
	g_return_val_if_fail (GITG_IS_SMART_CHARSET_CONVERTER (smart), NULL);

	if (smart->priv->current_encoding != NULL)
	{
		return (const GitgEncoding *)smart->priv->current_encoding->data;
	}
	else if (smart->priv->is_utf8)
	{
		return gitg_encoding_get_utf8 ();
	}

	return NULL;
}

guint
gitg_smart_charset_converter_get_num_fallbacks (GitgSmartCharsetConverter *smart)
{
	g_return_val_if_fail (GITG_IS_SMART_CHARSET_CONVERTER (smart), FALSE);

	if (smart->priv->charset_conv == NULL)
		return FALSE;

	return g_charset_converter_get_num_fallbacks (smart->priv->charset_conv) != 0;
}

/* ex:ts=8:noet: */
