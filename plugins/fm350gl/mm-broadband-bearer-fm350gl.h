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
 */

#ifndef MM_BROADBAND_BEARER_FM350GL_H
#define MM_BROADBAND_BEARER_FM350GL_H

#include <glib.h>
#include <glib-object.h>

#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include "mm-broadband-bearer.h"
#include "mm-broadband-modem-fm350gl.h"

#define MM_TYPE_BROADBAND_BEARER_FM350GL            (mm_broadband_bearer_fm350gl_get_type ())
#define MM_BROADBAND_BEARER_FM350GL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_BEARER_FM350GL, MMBroadbandBearerFM350GL))
#define MM_BROADBAND_BEARER_FM350GL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_BEARER_FM350GL, MMBroadbandBearerFM350GLClass))
#define MM_IS_BROADBAND_BEARER_FM350GL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_BEARER_FM350GL))
#define MM_IS_BROADBAND_BEARER_FM350GL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_BEARER_FM350GL))
#define MM_BROADBAND_BEARER_FM350GL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_BEARER_FM350GL, MMBroadbandBearerFM350GLClass))

GType mm_broadband_bearer_fm350gl_get_type (void);

typedef struct _MMBroadbandBearerFM350GL MMBroadbandBearerFM350GL;
typedef struct _MMBroadbandBearerFM350GLClass MMBroadbandBearerFM350GLClass;
typedef struct _MMBroadbandBearerFM350GLPrivate MMBroadbandBearerFM350GLPrivate;



typedef enum {
    CONNECTION_TYPE_NONE,
    CONNECTION_TYPE_3GPP,
    CONNECTION_TYPE_CDMA,
} ConnectionType;

struct _MMBroadbandBearerFM350GL {
    /*-- Common stuff --*/
    /* Data port used when modem is connected */
    MMPort *port;
    /* Current connection type */
    ConnectionType connection_type;

    /* PPP specific */
//    MMFlowControl flow_control;

    /*-- 3GPP specific --*/
    /* CID of the PDP context */
    gint profile_id;

    MMBroadbandBearer parent;
    MMBroadbandBearerFM350GLPrivate *priv;
};

struct _MMBroadbandBearerFM350GLClass {
    MMBroadbandBearerClass parent;
};



void mm_broadband_bearer_fm350gl_new (MMBroadbandModem *modem,
                         MMBearerProperties *bearer_properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data);

MMBaseBearer *mm_broadband_bearer_fm350gl_new_finish (GAsyncResult *res,
                                                     GError **error);


#endif /* MM_BROADBAND_BEARER_FM350GL_H */
