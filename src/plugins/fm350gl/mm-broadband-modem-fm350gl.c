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

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-broadband-modem-fm350gl.h"
#include "mm-broadband-bearer-fm350gl.h"
#include "mm-base-modem-at.h"
#include "mm-log-object.h"
#include "mm-modem-helpers.h"
#include "mm-common-helpers.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-time.h"
#include "mm-log.h"

#include "mm-iface-modem-3gpp-profile-manager.h"



static void iface_modem_init (MMIfaceModem *iface);
static void iface_modem_3gpp_profile_manager_init (MMIfaceModem3gppProfileManager *iface);

static MMIfaceModem *iface_modem_parent;

static void load_supported_modes (MMIfaceModem *self, GAsyncReadyCallback callback, gpointer user_data);
static GArray * load_supported_modes_finish (MMIfaceModem *self, GAsyncResult *res, GError **error);
static gboolean load_current_modes_finish (MMIfaceModem *self, GAsyncResult *res, MMModemMode *allowed, MMModemMode *preferred, GError **error);
static void load_current_modes (MMIfaceModem *self, GAsyncReadyCallback callback, gpointer user_data);
static MMBaseBearer * modem_create_bearer_finish (MMIfaceModem *self, GAsyncResult *res, GError **error);
static void modem_create_bearer (MMIfaceModem *self, MMBearerProperties *properties, GAsyncReadyCallback callback, gpointer user_data);
static void enabling_modem_init(MMBroadbandModem* self, GAsyncReadyCallback  callback, gpointer user_data);                                                             // Initialisierung ATZ0 anstelle von ATZ
static void modem_3gpp_profile_manager_check_format(MMIfaceModem3gppProfileManager* self, MMBearerIpFamily ip_type, GAsyncReadyCallback callback, gpointer user_data);  //min cid. CID sollte >0 sein
static gboolean modem_set_current_modes_finish(MMIfaceModem* self, GAsyncResult* res, GError** error);
static void modem_set_current_modes(MMIfaceModem* _self, MMModemMode allowed, MMModemMode preferred, GAsyncReadyCallback  callback, gpointer  user_data);
static void load_supported_bands(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data);
static GArray* load_supported_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error);
static void load_current_bands(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data);
static GArray* load_current_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error);
static void set_current_bands(MMIfaceModem* self, GArray* bands_array, GAsyncReadyCallback  callback, gpointer user_data);
static gboolean set_current_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error);
static void load_signal_quality(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data); // std geht auch bei dieser wird mit +CSQ abgefragt
static guint load_signal_quality_finish(MMIfaceModem* self,  GAsyncResult* res, GError** error);



G_DEFINE_TYPE_EXTENDED (MMBroadbandModemFM350GL, mm_broadband_modem_fm350gl, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init)
                        G_IMPLEMENT_INTERFACE(MM_TYPE_IFACE_MODEM_3GPP_PROFILE_MANAGER, iface_modem_3gpp_profile_manager_init)

			);


MMBroadbandModemFM350GL *mm_broadband_modem_FM350GL_new (const gchar *device,
                                                       const gchar *physdev,
                                                       const gchar **drivers,
                                                       const gchar *plugin,
                                                       guint16 vendor_id,
                                                       guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_FM350GL,
                         MM_BASE_MODEM_DEVICE, device,
			             MM_BASE_MODEM_PHYSDEV, physdev,
                         MM_BASE_MODEM_DRIVERS, drivers,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_BASE_MODEM_DATA_NET_SUPPORTED, TRUE,
                         MM_BASE_MODEM_DATA_TTY_SUPPORTED, FALSE,
                         NULL);
}

typedef struct {
    MMIfaceModem *self;
    GAsyncReadyCallback callback;
    gpointer user_data;
} DelayedModemCallback;

static gboolean
load_current_capabilities (gpointer cbdata)
{
    DelayedModemCallback *data = (DelayedModemCallback *)cbdata;

    iface_modem_parent->load_current_capabilities(data->self, data->callback, data->user_data);
    g_free(data);

    return FALSE;
}

/*
  The modem seems to crash if it receives AT+CGMR too soon after being plugged in,
  so we have to wait for it to settle. The required wait time seems to vary quite
  significantly between systems, with the highest I found at 2500ms, but out of an
   abundance of caution, we'll:
  1) pause at the earliest in the initialization sequence (see mm-iface-modem.c)
  2) delay for 4000ms instead of only 2500ms
*/
static void
delayed_load_current_capabilities (MMIfaceModem *self,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    DelayedModemCallback *data = g_new0(DelayedModemCallback, 1);
    data->self = self;
    data->callback = callback;
    data->user_data = user_data;

    g_timeout_add(4000, load_current_capabilities, data);
}

static void

iface_modem_init (MMIfaceModem *iface)
{
    iface_modem_parent = g_type_interface_peek_parent (iface);

    iface->load_current_capabilities = delayed_load_current_capabilities;
    iface->load_supported_modes = load_supported_modes;
    iface->load_supported_modes_finish = load_supported_modes_finish;
    iface->load_current_modes = load_current_modes;
    iface->load_current_modes_finish = load_current_modes_finish;
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
    iface->set_current_modes = modem_set_current_modes;
    iface->set_current_modes_finish = modem_set_current_modes_finish;
    iface->load_supported_bands = load_supported_bands;
    iface->load_supported_bands_finish = load_supported_bands_finish;
    iface->load_current_bands = load_current_bands;
    iface->load_current_bands_finish = load_current_bands_finish;
    iface->set_current_bands = set_current_bands;
    iface->set_current_bands_finish = set_current_bands_finish;
    iface->load_signal_quality = load_signal_quality;
    iface->load_signal_quality_finish = load_signal_quality_finish;
}


static void
iface_modem_3gpp_profile_manager_init(MMIfaceModem3gppProfileManager* iface)
{
    iface->check_format = modem_3gpp_profile_manager_check_format;                     
}

static void
mm_broadband_modem_fm350gl_init (MMBroadbandModemFM350GL *self) //muss da sein sonst werden die berarer funktionen vom individuellen bearer nicht genutzt?
{

}

static void
mm_broadband_modem_fm350gl_class_init (MMBroadbandModemFM350GLClass *klass)
{
    klass->enabling_modem_init = enabling_modem_init; 
}


static void
enabling_modem_init(MMBroadbandModem* self,
    GAsyncReadyCallback  callback,
    gpointer             user_data)
{
    MMPortSerialAt* primary;

    primary = mm_base_modem_peek_port_primary(MM_BASE_MODEM(self));
    if (!primary) {
        g_task_report_new_error(self, callback, user_data, enabling_modem_init,
            MM_CORE_ERROR, MM_CORE_ERROR_FAILED,
            "Failed to run init command: primary port missing");
        return;
    }
    mm_base_modem_at_command_full(MM_BASE_MODEM(self),  primary, "Z0", 6, FALSE, FALSE, NULL, /* cancellable */ callback, user_data);
}


static GArray *
load_supported_modes_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}


static void
load_supported_modes (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{

    GTask *task;
    MMModemModeCombination mode;
	GArray *combinations;
    combinations = g_array_sized_new (FALSE, FALSE, sizeof (MMModemModeCombination),4);
	
        mode.allowed = (MM_MODEM_MODE_3G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        mode.allowed = (MM_MODEM_MODE_4G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val (combinations, mode);
        mode.allowed = (MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_NONE;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        mode.preferred = MM_MODEM_MODE_3G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G);
        mode.preferred = MM_MODEM_MODE_4G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_3G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_5G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_4G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_5G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_3G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_4G;
        g_array_append_val(combinations, mode);
        mode.allowed = (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G);
        mode.preferred = MM_MODEM_MODE_5G;
        g_array_append_val(combinations, mode);

        task = g_task_new(self, NULL, callback, user_data);
	    g_task_return_pointer (task, combinations, (GDestroyNotify) g_array_unref);
}

static gboolean
load_current_modes_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           MMModemMode *allowed,
                           MMModemMode *preferred,
                           GError **error)
{
        gint mode;
        gchar** data;
        const gchar* response;
        GString* s;
        int c;
        g_autoptr(GError)   errorcf = NULL;

        *allowed = MM_MODEM_MODE_5G | MM_MODEM_MODE_4G | MM_MODEM_MODE_3G;
        *preferred = MM_MODEM_MODE_NONE;

        mode = -1;
        response = mm_base_modem_at_command_full_finish(MM_BASE_MODEM(self), res, &errorcf);
        if (!response) { return TRUE; }
        response = mm_strip_tag(response, "+GTACT: ");

        s = g_string_new(response);
        data = g_strsplit(s->str, ",", -1);
        c = g_strv_length(data);
        if (c<2) { return TRUE; }

        if (g_ascii_strcasecmp(data[0], "1") == 0) { *allowed = MM_MODEM_MODE_3G; }
        if (g_ascii_strcasecmp(data[0], "2") == 0) { *allowed = MM_MODEM_MODE_4G;}
        if (g_ascii_strcasecmp(data[0], "4") == 0) { *allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G; }
        if (g_ascii_strcasecmp(data[0], "14") == 0) { *allowed = MM_MODEM_MODE_5G; }
        if (g_ascii_strcasecmp(data[0], "16") == 0) { *allowed = MM_MODEM_MODE_3G| MM_MODEM_MODE_5G; }
        if (g_ascii_strcasecmp(data[0], "17") == 0) { *allowed = MM_MODEM_MODE_4G | MM_MODEM_MODE_5G; }
        if (g_ascii_strcasecmp(data[0], "20") == 0) { *allowed = MM_MODEM_MODE_3G | MM_MODEM_MODE_4G |MM_MODEM_MODE_5G; }

        if (g_ascii_strcasecmp(data[1], "2") == 0) { *preferred = MM_MODEM_MODE_3G; }
        if (g_ascii_strcasecmp(data[1], "3") == 0) { *preferred = MM_MODEM_MODE_4G; }
        if (g_ascii_strcasecmp(data[1], "6") == 0) { *preferred = MM_MODEM_MODE_5G; }


        return TRUE;


    
    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Failed to parse mode/tech response: Unexpected mode '%d'", mode);
    return FALSE;
}

static void
load_current_modes (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{


    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "AT+GTACT?", 
                              3,
                              FALSE,
                              callback,
                              user_data);
}


static void modem_set_current_modes(MMIfaceModem* _self, MMModemMode  allowed, MMModemMode  preferred, GAsyncReadyCallback callback, gpointer  user_data)
{
    gchar* command;
    int Rat = 20;
    int pref = 0;

    if (allowed == (MM_MODEM_MODE_3G)) { Rat = 1; }
    if (allowed == (MM_MODEM_MODE_4G)) { Rat = 2; }
    if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G)) { Rat = 4; }
    if (allowed == (MM_MODEM_MODE_5G)) { Rat = 14; }
    if (allowed == (MM_MODEM_MODE_5G | MM_MODEM_MODE_3G)) { Rat = 16; }
    if (allowed == (MM_MODEM_MODE_4G | MM_MODEM_MODE_5G)) { Rat = 17;  }
    if (allowed == (MM_MODEM_MODE_3G | MM_MODEM_MODE_4G | MM_MODEM_MODE_5G)) { Rat = 20; }
   
    if (preferred == MM_MODEM_MODE_3G) { pref = 2;}
    if (preferred == MM_MODEM_MODE_4G) { pref = 3;}
    if (preferred == MM_MODEM_MODE_5G) { pref = 6;}

 

    if (pref == 0){ command = g_strdup_printf("+GTACT=%d", Rat); } else { command = g_strdup_printf("+GTACT=%d,%d", Rat, pref); }

    mm_base_modem_at_command(MM_BASE_MODEM(_self), command, 3, FALSE, callback, user_data);
}

static gboolean modem_set_current_modes_finish(MMIfaceModem* self, GAsyncResult* res, GError** error)
{
    return TRUE;
}


static void
load_signal_quality(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data)
{
    mm_base_modem_at_command(MM_BASE_MODEM(self), "+CSQ", 3, FALSE, callback, user_data);
}

static guint
load_signal_quality_finish(MMIfaceModem* self,
    GAsyncResult* res,
    GError** error)
{
    gint quality = 0;
    gint         ber;
    const gchar* response, * p;
  

    response = mm_base_modem_at_command_finish(MM_BASE_MODEM(self), res, error);
    if (!response)
        return 0;



    if (!response[0]) {
        return 0;
    }

    p = mm_strip_tag(response, "+CSQ:");
    if (sscanf(p, "%d, %d", &quality, &ber)) {
        if (quality != 99)
            quality = CLAMP(quality, 0, 31) * 100 / 31;
        else
            quality = 0;
        return quality;
    }

    return 0;
}


static GArray* load_supported_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error)
{
    return g_task_propagate_pointer(G_TASK(res), error);
}


static void load_supported_bands_done(MMIfaceModem* self, GAsyncResult* res, GTask* task)
{
    const gchar* response;
    GError* error = NULL;
    int i, tmp;
    GString* stmp;
    gchar* umts_band;
    gchar* lte_band;
    gchar* nr_band;
    gchar** umtsbands;
    gchar** lteband;
    gchar** nrband;
    GArray* bands;
    gchar** parts;
    int c, cumts, clte, cnr;
    GString* s;


    response = mm_base_modem_at_command_finish(MM_BASE_MODEM(self), res, &error);
    if (!response) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    
    response = mm_strip_tag(response, "+GTACT: ");

    s = g_string_new(response);
    g_string_replace(s, ")", "", -1);
    g_string_replace(s, "(", "|", -1);  
    parts = g_strsplit(s->str, "|", -1);
    c = g_strv_length(parts);

    if (c != 10) {
        g_task_return_new_error(  task,  MM_CORE_ERROR,  MM_CORE_ERROR_FAILED, "Failed to parse supported bands response");
        g_object_unref(task);
        return;
    }
    
    umts_band = parts[5];       
    lte_band = parts[6];        
    nr_band = parts[9];        
 

    umtsbands = g_strsplit(umts_band, ",", -1);
    lteband = g_strsplit(lte_band, ",", -1);
    nrband = g_strsplit(nr_band, ",", -1);

    cumts = g_strv_length(umtsbands)-1;
    clte = g_strv_length(lteband)-1;
    cnr = g_strv_length(nrband);
 
    bands = g_array_new(FALSE, FALSE, sizeof(MMModemBand));

    for (i = 0; i < cumts; i++)
    {
        tmp = atoi(umtsbands[i]);
        bands = g_array_append_val(bands, tmp);

    }
    for (i = 0; i < clte; i++)
    {
        stmp = g_string_new(lteband[i]);
        g_string_erase(stmp, 0, 1);
        tmp = atoi(stmp->str)+30;
        bands = g_array_append_val(bands, tmp);
    }
    for (i = 0; i < cnr; i++)
    {
        stmp = g_string_new(nrband[i]);
        g_string_erase(stmp, 0, 1);
        tmp = atoi(stmp->str) + 300;
        bands = g_array_append_val(bands, tmp);
    }

    g_task_return_pointer(task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref(task);
}



static void load_supported_bands(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data)
{
    GTask* task;
    task = g_task_new(self, NULL, callback, user_data);

    mm_base_modem_at_command(
        MM_BASE_MODEM(self),
        "+GTACT=?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_supported_bands_done,
        task);
}



static GArray* load_current_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error)
{
    return g_task_propagate_pointer(G_TASK(res), error);
}

static void load_current_bands_done(MMIfaceModem* self, GAsyncResult* res, GTask* task)
{
    const gchar* response;
    GError* error = NULL;
    int c, i, tmp, count;
    GArray* bands;
    GString* s;
    gchar** parts;

    response = mm_base_modem_at_command_finish(MM_BASE_MODEM(self), res, &error);
    if (!response) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    response = mm_strip_tag(response, "+GTACT: ");

    s = g_string_new(response);
    parts = g_strsplit(s->str, ",", -1);
    c = g_strv_length(parts);


    if (c < 4) {
        g_task_return_new_error(task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Failed to parse supported bands response");
        g_object_unref(task);
        return;
    }


    count = g_strv_length(parts);

    bands = g_array_new(FALSE, FALSE, sizeof(MMModemBand));

   


    for (i = 3; i < count; i++)
    {
        tmp = atoi(parts[i]);
        if (tmp >= 100 && tmp < 200) { tmp = tmp - 100 + 30; }
         if (tmp >= 500 && tmp < 600) { tmp = tmp - 500 + 300; } if (tmp >= 5000 && tmp < 6000) { tmp = tmp - 5000 + 300; }

        bands = g_array_append_val(bands, tmp);
    }
   

    g_task_return_pointer(task, bands, (GDestroyNotify)g_array_unref);
    g_object_unref(task);
}

static void load_current_bands(MMIfaceModem* self, GAsyncReadyCallback callback, gpointer user_data)
{  
    GTask* task;

    task = g_task_new(self, NULL, callback, user_data);

    mm_base_modem_at_command(
        MM_BASE_MODEM(self),
        "+GTACT?",
        3,
        FALSE,
        (GAsyncReadyCallback)load_current_bands_done,
        task);  
}



static gboolean set_current_bands_finish(MMIfaceModem* self, GAsyncResult* res, GError** error)
{
    return g_task_propagate_boolean(G_TASK(res), error);
}


static void set_current_bands_set_finish(MMBaseModem* self, GAsyncResult* res, GTask* task)
{
    const gchar* response;
    GError* error = NULL;
    response = mm_base_modem_at_command_finish(MM_BASE_MODEM(self), res, &error);
    if (!response) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}

static void set_current_bands_get_current_rats(MMBaseModem* self, GAsyncResult* res, GTask* task)
{
    GString* s;

    GArray* ctx;
    gchar** parts;
    int c,i,j,b;
    GString* newcommand;

    const gchar* response;
    GError* error = NULL;

    ctx = g_task_get_task_data(task);
    response = mm_base_modem_at_command_finish(MM_BASE_MODEM(self), res, &error);
    if (!response) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    response = mm_strip_tag(response, "+GTACT: ");
    s = g_string_new(response);
    parts = g_strsplit(s->str, ",", -1);
    c = g_strv_length(parts);
    if (c < 3)
    {
        g_task_return_new_error(task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Failed to parse Rats response");
        g_object_unref(task);
        return;
    }

    newcommand = g_string_new("AT+GTACT=");
    g_string_append(newcommand, parts[0]);
    g_string_append(newcommand, ",");
    g_string_append(newcommand, parts[1]);
    g_string_append(newcommand, ",");
    g_string_append(newcommand, parts[2]);



    j = ctx->len;

    for (i=0;i<j;i++)
    {
        b = g_array_index(ctx, MMModemBand, i);
        if (b > 0 && b < 30)
        {
            g_string_append(newcommand, ",");
            g_string_append(newcommand, g_strdup_printf("%i", b));          
            continue;
        }
        if (b > 30 && b < 300)
        {
            b = b - 30 + 100;
            g_string_append(newcommand, ",");
            g_string_append(newcommand, g_strdup_printf("%i", b));
            continue;
        }
        if (b > 30 && b < 599)
        {
            b = b - 300; if ( b>0 && b <10 ) {b=b + 500;} else {b=b + 5000;}
            g_string_append(newcommand, ",");
            g_string_append(newcommand, g_strdup_printf("%i", b));
            continue;
        }

    }

    mm_base_modem_at_command(MM_BASE_MODEM(self),
        newcommand->str,
        3,
        TRUE, 
        (GAsyncReadyCallback)set_current_bands_set_finish,
        task);

    return;
}



static void set_current_bands_context_free(GArray* ctx)
{

}

static void set_current_bands(MMIfaceModem* self, GArray* bands_array, GAsyncReadyCallback  callback, gpointer user_data)
{
    GTask* task;
    task = g_task_new(self, NULL, callback, user_data);

    g_task_set_task_data(task, bands_array, (GDestroyNotify)set_current_bands_context_free);

    mm_base_modem_at_command(MM_BASE_MODEM(self),
        "AT+GTACT?",
        3,
        TRUE, 
        (GAsyncReadyCallback)set_current_bands_get_current_rats,
        task);
}




static MMBaseBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}

static void
broadband_bearer_fm350gl_new_ready (GObject *source,
                                   GAsyncResult *res,
                                   GTask *task)
{
    MMBaseBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_fm350gl_new_finish (res, &error);
    if (!bearer)
        g_task_return_error (task, error);
    else
        g_task_return_pointer (task, bearer, g_object_unref);

    g_object_unref (task);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *props,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GTask *task;
    task = g_task_new (self, NULL, callback, user_data);

    mm_broadband_bearer_fm350gl_new (MM_BROADBAND_MODEM (self),
                             props,
                             NULL, /* cancellable */
                             (GAsyncReadyCallback)broadband_bearer_fm350gl_new_ready,
                             task);
}




typedef struct {                    
    MMBearerIpFamily ip_type;
    guint            min_profile_id;
    guint            max_profile_id;
} CheckFormatContext;

static void
check_format_context_free(CheckFormatContext* ctx)
{
    g_slice_free(CheckFormatContext, ctx);
}

static void
check_format_cgdcont_test_ready(MMBaseModem* self,
    GAsyncResult* res,
    GTask* task)
{
    CheckFormatContext* ctx;
    const gchar* response;
    GList* format_list = NULL;
    g_autofree gchar* ip_family_str = NULL;
    g_autoptr(GError)   error = NULL;
    gboolean            checked = FALSE;

    ctx = g_task_get_task_data(task);

    ip_family_str = mm_bearer_ip_family_build_string_from_mask(ctx->ip_type);

    response = mm_base_modem_at_command_full_finish(self, res, &error);
    if (!response)
        mm_obj_dbg(self, "failed checking context definition format: %s", error->message);
    else {
        format_list = mm_3gpp_parse_cgdcont_test_response(response, self, &error);
        if (error)
            mm_obj_dbg(self, "error parsing +CGDCONT test response: %s", error->message);
        else if (mm_3gpp_pdp_context_format_list_find_range(format_list, ctx->ip_type,
            &ctx->min_profile_id, &ctx->max_profile_id))
            checked = TRUE;
    }

    if (!checked) {
        ctx->min_profile_id = 1;
        ctx->max_profile_id = G_MAXINT - 1;
        mm_obj_dbg(self, "unknown +CGDCONT format details for PDP type '%s', using defaults: minimum %d, maximum %d",
            ip_family_str, ctx->min_profile_id, ctx->max_profile_id);
    }
    else {
        ctx->min_profile_id = 1;   
        ctx->max_profile_id = 1;   
        mm_obj_dbg(self, "+CGDCONT format details for PDP type '%s': minimum %d, maximum %d", ip_family_str, ctx->min_profile_id, ctx->max_profile_id);
    }

    mm_3gpp_pdp_context_format_list_free(format_list);

    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}


static void
modem_3gpp_profile_manager_check_format(MMIfaceModem3gppProfileManager* self,
    MMBearerIpFamily                ip_type,
    GAsyncReadyCallback             callback,
    gpointer                        user_data)
{
    GTask* task;
    CheckFormatContext* ctx;

    task = g_task_new(self, NULL, callback, user_data);
    ctx = g_slice_new0(CheckFormatContext);
    ctx->ip_type = ip_type;
    g_task_set_task_data(task, ctx, (GDestroyNotify)check_format_context_free);

    mm_base_modem_at_command(MM_BASE_MODEM(self),
        "+CGDCONT=?",
        3,
        TRUE, /* cached */
        (GAsyncReadyCallback)check_format_cgdcont_test_ready,
        task);
}


