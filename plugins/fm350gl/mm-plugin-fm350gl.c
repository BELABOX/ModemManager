/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) PMGZED
 * Copyright (C) ModemManager Team
 */

#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>

#include <gmodule.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-plugin.h"
#include "mm-broadband-modem.h"
#include "mm-serial-parsers.h"
#include "mm-log-object.h"
#include "mm-iface-modem.h"

#include "mm-plugin-fm350gl.h"
#include "mm-broadband-modem-fm350gl.h"

G_DEFINE_TYPE (MMPluginfm350gl, mm_plugin_fm350gl, MM_TYPE_PLUGIN)
//#define MM_TYPE_PLUGIN_FM350GL mm_plugin_fm350gl_get_type ()
//MM_DEFINE_PLUGIN (FM350GL, fm350gl, fm350gl)

MM_PLUGIN_DEFINE_MAJOR_VERSION
MM_PLUGIN_DEFINE_MINOR_VERSION


static MMBaseModem *
create_modem (MMPlugin *self,
              const gchar *uid,
              const gchar **drivers,
              guint16 vendor,
              guint16 product,
              GList *probes,
              GError **error)
{
    return MM_BASE_MODEM (mm_broadband_modem_FM350GL_new (uid,
                                                  drivers,
                                                  mm_plugin_get_name (self),
                                                  vendor,
                                                  product));

}



G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    static const gchar *subsystems[] = { "tty", "net", NULL };
    static const mm_uint16_pair product_ids[] = {
        { 0x0e8d, 0x7126 },
        { 0x0e8d, 0x7127 }
    };

    return MM_PLUGIN (
        g_object_new (MM_TYPE_PLUGIN_FM350GL,
                      MM_PLUGIN_NAME,               MM_MODULE_NAME,
                      MM_PLUGIN_ALLOWED_SUBSYSTEMS, subsystems,
		              MM_PLUGIN_ALLOWED_PRODUCT_IDS, product_ids,
                      MM_PLUGIN_ALLOWED_AT,         TRUE,
		              MM_PLUGIN_ICERA_PROBE,         FALSE,
                      MM_PLUGIN_REMOVE_ECHO,         TRUE,
                      NULL));
}

static void
mm_plugin_fm350gl_init (MMPluginfm350gl *self)
{
}



static void
mm_plugin_fm350gl_class_init (MMPluginfm350glClass *klass)
{
    MMPluginClass *plugin_class = MM_PLUGIN_CLASS (klass);
    plugin_class->create_modem = create_modem;
}


