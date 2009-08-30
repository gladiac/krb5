/*
 * plugins/authdata/greet_client/
 *
 * Copyright 2009 by the Massachusetts Institute of Technology.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * Sample authorization data plugin
 */

#include <string.h>
#include <errno.h>
#include "k5-int.h"
#include <krb5/authdata_plugin.h>
#include <assert.h>

struct greet_context {
    krb5_data greeting;
    krb5_boolean verified;
};

static krb5_data greet_attr = {
    KV5M_DATA, sizeof("greet:greeting") - 1, "greet:greeting" };

static krb5_error_code
greet_init(krb5_context kcontext, void **plugin_context)
{
    *plugin_context = 0;
    return 0;
}

static void
greet_flags(krb5_context kcontext,
            void *plugin_context,
            krb5_authdatatype ad_type,
            krb5_flags *flags)
{
    *flags = AD_USAGE_AP_REQ | AD_USAGE_KDC_ISSUED | AD_INFORMATIONAL;
}

static void
greet_fini(krb5_context kcontext, void *plugin_context)
{
    return;
}

static krb5_error_code
greet_request_init(krb5_context kcontext,
                   krb5_authdata_context context,
                   void *plugin_context,
                   void **request_context)
{
    struct greet_context *greet;

    greet = malloc(sizeof(*greet));
    if (greet == NULL)
        return ENOMEM;

    greet->greeting.data = NULL;
    greet->greeting.length = 0;

    *request_context = greet;

    return 0;
}

static krb5_error_code
greet_import_attributes(krb5_context kcontext,
                        krb5_authdata_context context,
                        void *plugin_context,
                        void *request_context,
                        krb5_authdata **authdata)
{
    krb5_error_code code;
    struct greet_context *greet = (struct greet_context *)request_context;
    krb5_data data;

    krb5_free_data_contents(kcontext, &greet->greeting);
    greet->verified = FALSE;

    assert(authdata[0] != NULL);

    data.length = authdata[0]->length;
    data.data = (char *)authdata[0]->contents;

    code = krb5int_copy_data_contents_add0(kcontext, &data, &greet->greeting);

    return code;
}

static void
greet_request_fini(krb5_context kcontext,
                   krb5_authdata_context context,
                   void *plugin_context,
                   void *request_context)
{
    struct greet_context *greet = (struct greet_context *)request_context;

    if (greet != NULL) {
        krb5_free_data_contents(kcontext, &greet->greeting);
        free(greet);
    }
}

static krb5_error_code
greet_get_attribute_types(krb5_context kcontext,
                          krb5_authdata_context context,
                          void *plugin_context,
                          void *request_context,
                          krb5_data **verified,
                          krb5_data **asserted,
                          krb5_data **all_attrs)
{
    krb5_error_code code;
    struct greet_context *greet = (struct greet_context *)request_context;

    if (greet->greeting.length == 0)
        return ENOENT;

    if (asserted == NULL)
        return 0;

    *asserted = calloc(2, sizeof(krb5_data));
    if (*asserted == NULL)
        return ENOMEM;

    code = krb5int_copy_data_contents_add0(kcontext, &greet_attr, &(*asserted)[0]);
    if (code != 0) {
        free(*asserted);
        *asserted = NULL;
        return code;
    }

    return 0;
}

static krb5_error_code
greet_get_attribute(krb5_context kcontext,
                    krb5_authdata_context context,
                    void *plugin_context,
                    void *request_context,
                    const krb5_data *attribute,
                    krb5_boolean *authenticated,
                    krb5_boolean *complete,
                    krb5_data *value,
                    krb5_data *display_value,
                    int *more)
{
    struct greet_context *greet = (struct greet_context *)request_context;

    if (!data_eq(*attribute, greet_attr) || greet->greeting.length == 0)
        return ENOENT;

    *authenticated = greet->verified;
    *complete = TRUE;
    *more = 0;

    return krb5int_copy_data_contents_add0(kcontext, &greet->greeting, value);
}

static krb5_error_code
greet_set_attribute(krb5_context kcontext,
                    krb5_authdata_context context,
                    void *plugin_context,
                    void *request_context,
                    krb5_boolean complete,
                    const krb5_data *attribute,
                    const krb5_data *value)
{
    struct greet_context *greet = (struct greet_context *)request_context;
    krb5_data data;
    krb5_error_code code;

    code = krb5int_copy_data_contents_add0(kcontext, value, &data);
    if (code != 0)
        return code;

    krb5_free_data_contents(kcontext, &greet->greeting);
    greet->greeting = data;
    greet->verified = FALSE;

    return 0;
}

static krb5_error_code
greet_delete_attribute(krb5_context kcontext,
                        krb5_authdata_context context,
                        void *plugin_context,
                        void *request_context,
                        const krb5_data *attribute)
{
    struct greet_context *greet = (struct greet_context *)request_context;

    krb5_free_data_contents(kcontext, &greet->greeting);
    greet->verified = FALSE;

    return 0;
}

static krb5_error_code
greet_export_attributes(krb5_context kcontext,
                        krb5_authdata_context context,
                        void *plugin_context,
                        void *request_context,
                        krb5_flags usage,
                        krb5_authdata ***out_authdata)
{
    struct greet_context *greet = (struct greet_context *)request_context;
    krb5_authdata *data[2];
    krb5_authdata datum;
    krb5_error_code code;

    datum.ad_type = -42;
    datum.length = greet->greeting.length;
    datum.contents = (krb5_octet *)greet->greeting.data;

    data[0] = &datum;
    data[1] = NULL;

    code = krb5_copy_authdata(kcontext, data, out_authdata);

    return code;
}

static krb5_error_code
greet_copy_context(krb5_context kcontext,
                   krb5_authdata_context context,
                   void *plugin_context,
                   void *request_context,
                   void *dst_plugin_context,
                   void *dst_request_context)
{
    struct greet_context *src = (struct greet_context *)request_context;
    struct greet_context *dst = (struct greet_context *)dst_request_context;

    return krb5int_copy_data_contents_add0(kcontext,
                                           &src->greeting,
                                           &dst->greeting);
}

static krb5_error_code
greet_verify(krb5_context kcontext,
             krb5_authdata_context context,
             void *plugin_context,
             void *request_context,
             const krb5_auth_context *auth_context,
             const krb5_keyblock *key,
             const krb5_ap_req *req,
             krb5_boolean kdc_issued_flag,
             krb5_const_principal issuer)
{
    struct greet_context *greet = (struct greet_context *)request_context;

    greet->verified = kdc_issued_flag;

    return 0;
}

static krb5_authdatatype greet_ad_types[] = { -42, 0 };

krb5plugin_authdata_client_ftable_v0 authdata_client_0 = {
    "greet",
    greet_ad_types,
    greet_init,
    greet_fini,
    greet_flags,
    greet_request_init,
    greet_request_fini,
    greet_get_attribute_types,
    greet_get_attribute,
    greet_set_attribute,
    greet_delete_attribute,
    greet_import_attributes,
    greet_export_attributes,
    NULL,
    NULL,
    greet_copy_context,
    greet_verify,
};
