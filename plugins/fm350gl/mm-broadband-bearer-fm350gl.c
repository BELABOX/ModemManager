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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#define _LIBMM_INSIDE_MM
#include <libmm-glib.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "mm-base-modem-at.h"
#include "mm-broadband-bearer-fm350gl.h"
#include "mm-log-object.h"



static MMPort * dial_3gpp_finish (MMBroadbandBearer *self, GAsyncResult *res, GError **error);
static void dial_3gpp (MMBroadbandBearer *self, MMBaseModem *modem, MMPortSerialAt *primary, guint cid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
static void get_ip_config_3gpp (MMBroadbandBearer *self, MMBroadbandModem *modem, MMPortSerialAt *primary, MMPortSerialAt *secondary, MMPort *data, guint cid, MMBearerIpFamily ip_family, GAsyncReadyCallback callback, gpointer user_data);
static gboolean get_ip_config_3gpp_finish (MMBroadbandBearer *self, GAsyncResult *res, MMBearerIpConfig **ipv4_config, MMBearerIpConfig **ipv6_config, GError **error);
static void disconnect_3gpp(MMBroadbandBearer* self, MMBroadbandModem* modem, MMPortSerialAt* primary, MMPortSerialAt* secondary, MMPort* data, guint cid, GAsyncReadyCallback callback, gpointer user_data); // behandelt den disconnect
gboolean parse_ip(const gchar* response, MMBearerIpConfig* out_ip4_config, MMBearerIpConfig* out_ip6_config, GError** error);
void deztohexip(gchar* dezstring, GString* output);

G_DEFINE_TYPE (MMBroadbandBearerFM350GL, mm_broadband_bearer_fm350gl, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerFM350GLPrivate {
    /*-- Common stuff --*/
    /* Data port used when modem is connected */
    MMPort* port;
    /* Current connection type */
    ConnectionType connection_type;

    /* PPP specific */
    MMFlowControl flow_control;

    /*-- 3GPP specific --*/
    /* CID of the PDP context */
    gint profile_id;
};


typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    GError *saved_error;
    MMPortSerialAt* dial_port;
    gboolean close_dial_port_on_exit;
} Dial3gppContext;

typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    MMPortSerialAt *secondary;
    MMPort *data;
    /* 3GPP-specific */
    MMBearerIpFamily ip_family;
} DetailedConnectContext;






void
mm_broadband_bearer_fm350gl_new (MMBroadbandModem *modem,
                         MMBearerProperties *bearer_properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_FM350GL,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BASE_BEARER_MODEM,             modem,
        MM_BASE_BEARER_CONFIG,            bearer_properties,
        NULL);
}




	      
MMBaseBearer * mm_broadband_bearer_fm350gl_new_finish (GAsyncResult *res, GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_base_bearer_export (MM_BASE_BEARER (bearer));

    return MM_BASE_BEARER (bearer);
}



static void
mm_broadband_bearer_fm350gl_init (MMBroadbandBearerFM350GL *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_FM350GL,
                                              MMBroadbandBearerFM350GLPrivate);
}



static void
mm_broadband_bearer_fm350gl_class_init (MMBroadbandBearerFM350GLClass *klass)
{
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    broadband_bearer_class->dial_3gpp = dial_3gpp;					
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;			
    broadband_bearer_class->get_ip_config_3gpp = get_ip_config_3gpp;		
    broadband_bearer_class->get_ip_config_3gpp_finish = get_ip_config_3gpp_finish;
    broadband_bearer_class->disconnect_3gpp = disconnect_3gpp;
}



static void
cgact_ready(MMBaseModem* modem,
    GAsyncResult* res,
    GTask* task)
{
    MMBroadbandBearer* self;

    GError* error = NULL;

    self = g_task_get_source_object(task);
 
   
    mm_base_modem_at_command_full_finish(modem, res, &error);

    if (!error)
    { }
    else {
        mm_obj_dbg(self, "PDP context deactivation failed (not fatal): %s", error->message);
    }

    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}
static void
disconnect_3gpp(MMBroadbandBearer* self,
    MMBroadbandModem* modem,
    MMPortSerialAt* primary,
    MMPortSerialAt* secondary,
    MMPort* data,
    guint cid,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    GTask* task;
    gchar* command;

    g_assert(primary != NULL);
    command = g_strdup_printf("+CGACT=0,%d", cid);
    task = g_task_new(self, NULL, callback, user_data);

        mm_obj_dbg(self, "sending PDP context deactivation in primary port...");
        mm_base_modem_at_command_full(MM_BASE_MODEM(g_object_ref(modem)),
            primary,
            command, //"Z0",
            45,
            FALSE,
            FALSE, /* raw */
            NULL, /* cancellable */
            (GAsyncReadyCallback)cgact_ready,
            task);


        return;

}



static MMPort *
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return g_task_propagate_pointer (G_TASK (res), error);
}





static void
activate_pdp_finish (MMBaseModem *modem,
           GAsyncResult *res,
           GTask *task)
{
    Dial3gppContext *ctx;


    ctx = g_task_get_task_data (task);
    mm_base_modem_at_command_full_finish(modem, res, &ctx->saved_error);

    g_task_return_pointer (task,
                           g_object_ref (mm_base_modem_peek_best_data_port (modem, MM_PORT_TYPE_NET)), //return Port for IP Configuration (ethX) to -> dial_3gpp_finish
                           g_object_unref);
    g_object_unref (task);
}


static void
dial_3gpp_context_free (Dial3gppContext *ctx)
{
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (Dial3gppContext, ctx);
}


static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMPortSerialAt *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    gchar *command;
    Dial3gppContext *ctx;
    GTask *task;

    g_assert (primary != NULL);

    ctx = g_slice_new0 (Dial3gppContext);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
	
    task = g_task_new (self, cancellable, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)dial_3gpp_context_free);


    command = g_strdup_printf ("+CGACT=1,%d", cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   MM_BASE_BEARER_DEFAULT_CONNECTION_TIMEOUT,
                                   FALSE,
                                   FALSE, /* raw */
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)activate_pdp_finish,
                                   task);
    g_free (command);
}


void deztohexip(gchar* dezstring, GString* output)
{
    GString* dstr = g_string_new(dezstring);
    gchar** parts;
    int c,i;

    parts = g_strsplit(dstr->str, ".", -1);
    c = g_strv_length(parts);

    for (i = 0; i < c; i++)
    {
        if (i == 0)
        {
            g_string_append(output, g_strdup_printf("%02x", atoi(parts[i])));
        }
        else
        {
            if (i%2==0){ g_string_append(output, ":"); }          
            g_string_append(output, g_strdup_printf("%02x", atoi(parts[i])));
        }

    }
  
}


gboolean
parse_ip (const gchar *response,
                               MMBearerIpConfig *out_ip4_config,
                               MMBearerIpConfig *out_ip6_config,
                               GError **error)
{

    gchar** data;
    gchar** data1;
    gchar** data2;
    gchar* ip1;
    gchar* ip2;
    gchar* ipv4;
    gchar* ipv6;
    GString* gstmp;
    int c;
    GString* s;

    g_return_val_if_fail(out_ip4_config, FALSE);
    g_return_val_if_fail(out_ip6_config, FALSE);

    if (!response || !g_str_has_prefix (response, "+CGPADDR")) {
        g_set_error (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing +CGPADDR prefix");
        return FALSE;
    }

    response = mm_strip_tag (response, "+CGPADDR: ");
    
    s=g_string_new(response);
    g_string_replace(s,"\"","",-1);
    data = g_strsplit(s->str, ",", -1);
    c= g_strv_length(data);


    if (c!=3) { g_set_error_literal (error, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match +CGPADDR reply");  return TRUE; }

    ipv4 = NULL;
    ipv6 = NULL;
    ip1=data[1];
    ip2=data[2];
    g_free(data);


    mm_bearer_ip_config_set_method (out_ip4_config, MM_BEARER_IP_METHOD_UNKNOWN);
    mm_bearer_ip_config_set_method (out_ip6_config, MM_BEARER_IP_METHOD_UNKNOWN);

    data1 = g_strsplit(ip1, ".", -1); 
    c= g_strv_length(data1);
    if(c<5 && c>1)
    {
	    ipv4=ip1; mm_bearer_ip_config_set_method (out_ip4_config, MM_BEARER_IP_METHOD_STATIC); 
	    mm_bearer_ip_config_set_address (out_ip4_config, ipv4); 
	    mm_bearer_ip_config_set_prefix (out_ip4_config, 32); 
	    mm_bearer_ip_config_set_gateway(out_ip4_config, ipv4);
    }
    if(c>5)
    {
	    ipv6=ip1; mm_bearer_ip_config_set_method (out_ip6_config, MM_BEARER_IP_METHOD_DHCP);
    }
    g_free(data1);


    data2 = g_strsplit(ip2, ".", -1);
    c= g_strv_length(data2);
    if(c<5 && c>1)            
    {
        ipv4=ip2; mm_bearer_ip_config_set_method (out_ip4_config, MM_BEARER_IP_METHOD_STATIC);
        mm_bearer_ip_config_set_address (out_ip4_config, ipv4);
        mm_bearer_ip_config_set_prefix (out_ip4_config, 32);
        mm_bearer_ip_config_set_gateway(out_ip4_config, ipv4);
    }
    if(c>5)
    {
        ipv6=ip2; mm_bearer_ip_config_set_method (out_ip6_config, MM_BEARER_IP_METHOD_DHCP);
        gstmp = g_string_new("");
        deztohexip(ipv6, gstmp);
        ipv6 = gstmp->str;
    }
    g_free(data2);

    mm_obj_info (NULL, "ipv4: %s",ipv4);
    mm_obj_info (NULL, "ipv6: %s",ipv6);
    

    return TRUE;
}


typedef struct {
    MMBaseModem *modem;
    MMPortSerialAt *primary;
    guint cid;
    MMBearerIpConfig *ipv4_config;
    MMBearerIpConfig* ipv6_config;

} GetIpConfig3gppContext;

static void
get_ip_config_context_free (GetIpConfig3gppContext *ctx)
{
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_slice_free (GetIpConfig3gppContext, ctx);
    g_object_unref (ctx->ipv4_config);
    g_object_unref (ctx->ipv6_config);
}

static gboolean
get_ip_config_3gpp_finish (MMBroadbandBearer *self,
                           GAsyncResult *res,
                           MMBearerIpConfig **ipv4_config,
                           MMBearerIpConfig **ipv6_config,
                           GError **error)
{
    MMBearerConnectResult *configs;
    MMBearerIpConfig *ipv4, *ipv6;

    configs = g_task_propagate_pointer (G_TASK (res), error);
    if (!configs)
        return FALSE;


    ipv4 = mm_bearer_connect_result_peek_ipv4_config (configs);
    ipv6 = mm_bearer_connect_result_peek_ipv6_config (configs);
    g_assert (ipv4 || ipv6);
    if (ipv4_config && ipv4)
        *ipv4_config = g_object_ref (ipv4);
    if (ipv6_config && ipv6)
        *ipv6_config = g_object_ref (ipv6);

    mm_bearer_connect_result_unref (configs);
    return TRUE;
}



static void ip_config_dns_gw_ready (MMBaseModem *modem, GAsyncResult *res,  GTask *task)
{
    GetIpConfig3gppContext *ctx;
    const gchar *response;
    GError *error = NULL;
    MMBearerConnectResult *connect_result;

    gchar* dns1;
    gchar* dns2;
    gchar* dnsv61;
    gchar* dnsv62;
    const gchar* dns[3] = { 0 };
    const gchar* dnsv6[3] = { 0 };
    GString* s;
    gchar** data;
    int c;
    GString* gstmp;

    ctx = g_task_get_task_data(task);
    response = mm_base_modem_at_command_full_finish(modem, res, &error);

    if (!response || !g_str_has_prefix (response, "+CGCONTRDP")) {
        g_task_return_new_error(task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Missing +cgcontrdp prefix");
        return;
    }

    response = mm_strip_tag (response, "+CGCONTRDP: ");
    
    s=g_string_new(response);
    g_string_replace(s,"\"","",-1);



    data = g_strsplit(s->str, ",", -1);
    c= g_strv_length(data);


    if (c<30) { g_task_return_new_error(task, MM_CORE_ERROR, MM_CORE_ERROR_FAILED, "Couldn't match +cgcontrdp reply");   return; }

    dns1=data[5];
    dns2=data[6];
    dnsv61=data[28];
    dnsv62=data[29];
    g_free(data);

    gstmp = g_string_new("");
    deztohexip(dnsv61, gstmp);
    dnsv61 = gstmp->str;

    gstmp = g_string_new("");
    deztohexip(dnsv62, gstmp);
    dnsv62 = gstmp->str;


    mm_obj_info (NULL, "dns1: %s",dns1);
    mm_obj_info (NULL, "dns2: %s",dns2);
    mm_obj_info (NULL, "dnsv61: %s", dnsv61);
    mm_obj_info (NULL, "dnsv61: %s", dnsv62);


	
	dns[0]= dns1;
	dns[1]= dns2;

    dnsv6[0] = dnsv61;
    dnsv6[1] = dnsv62;

	mm_bearer_ip_config_set_dns(ctx->ipv4_config, (const gchar **) &dns);
    mm_bearer_ip_config_set_dns(ctx->ipv6_config, (const gchar **) &dnsv6);

    connect_result = mm_bearer_connect_result_new(mm_base_modem_peek_best_data_port(modem, MM_PORT_TYPE_NET), ctx->ipv4_config, ctx->ipv6_config); 

    g_task_return_pointer (task,
                           connect_result,
                           (GDestroyNotify)mm_bearer_connect_result_unref);


    g_object_unref(task);
}



static void
ip_config_ipaddress_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 GTask *task)
{
    gchar* command;
    const gchar *response;
    GError *error = NULL;
    GetIpConfig3gppContext *ctx;
    ctx=g_task_get_task_data (task);
    ctx->ipv4_config = mm_bearer_ip_config_new ();
    ctx->ipv6_config = mm_bearer_ip_config_new ();
    response = mm_base_modem_at_command_full_finish (modem, res, &error);

    if (!error)
    {
    }
    else {
        mm_obj_dbg(NULL, "AT+CGPADDR Failed no IP Data: %s", error->message);
        g_task_return_error(task, error);
        return;
    }


    if (!parse_ip (response, ctx->ipv4_config, ctx->ipv6_config, &error))
    {
        g_task_return_error (task, error);
        
        goto out;
    }    

    
    command = g_strdup_printf ("at+cgcontrdp");
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (modem),
        ctx->primary,
        command,
        3,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)ip_config_dns_gw_ready,
        task);
    g_free (command);
out:

}



static void
get_ip_config_3gpp (MMBroadbandBearer *self,
                    MMBroadbandModem *modem,
                    MMPortSerialAt *primary,
                    MMPortSerialAt *secondary,
                    MMPort *data,
                    guint cid,
                    MMBearerIpFamily ip_family,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GetIpConfig3gppContext *ctx;
    GTask *task;
    gchar *command;

    mm_obj_info (NULL, "3gpp Dataport:: %s",  mm_port_get_device (data));
    ctx = g_slice_new0 (GetIpConfig3gppContext);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;

    task = g_task_new (self, NULL, callback, user_data);
    g_task_set_task_data (task, ctx, (GDestroyNotify)get_ip_config_context_free);

    command = g_strdup_printf ("AT+CGPADDR=%d", cid);
    mm_base_modem_at_command_full (
        MM_BASE_MODEM (modem),
        primary,
        command,
        3,
        FALSE,
        FALSE, /* raw */
        NULL, /* cancellable */
        (GAsyncReadyCallback)ip_config_ipaddress_ready,
        task);
    g_free (command);
}

