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

#ifndef MM_BROADBAND_MODEM_FM350GL_H
#define MM_BROADBAND_MODEM_FM350GL_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_FM350GL            (mm_broadband_modem_fm350gl_get_type ())
#define MM_BROADBAND_MODEM_FM350GL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_FM350GL, MMBroadbandModemFM350GL))
#define MM_BROADBAND_MODEM_FM350GL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_FM350GL, MMBroadbandModemFM350GLClass))
#define MM_IS_BROADBAND_MODEM_FM350GL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_FM350GL))
#define MM_IS_BROADBAND_MODEM_FM350GL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_FM350GL))
#define MM_BROADBAND_MODEM_FM350GL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_FM350GL, MMBroadbandModemFM350GLClass))

typedef struct _MMBroadbandModemFM350GL MMBroadbandModemFM350GL;
typedef struct _MMBroadbandModemClass MMBroadbandModemFM350GLClass;
typedef struct _MMBroadbandModemFM350GLPrivate MMBroadbandModemFM350GLPrivate;

struct _MMBroadbandModemFM350GL {
    MMBroadbandModem parent;
    MMBroadbandModemFM350GLPrivate *priv;
};

struct _MMBroadbandModemFM350GLClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_fm350gl_get_type (void);

MMBroadbandModemFM350GL *mm_broadband_modem_FM350GL_new (const gchar *device,
                                                       const gchar *physdev,
                                                       const gchar **drivers,
                                                       const gchar *plugin,
                                                       guint16 vendor_id,
                                                       guint16 product_id);

#endif /* MM_BROADBAND_MODEM_FM350GL_H */
