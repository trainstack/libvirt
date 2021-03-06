/*
 * virtypedparam.c: utility functions for dealing with virTypedParameters
 *
 * Copyright (C) 2011-2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>
#include "virtypedparam.h"

#include <stdarg.h>

#include "viralloc.h"
#include "virutil.h"
#include "virerror.h"

#define VIR_FROM_THIS VIR_FROM_NONE

VIR_ENUM_DECL(virTypedParameter)
VIR_ENUM_IMPL(virTypedParameter, VIR_TYPED_PARAM_LAST,
              "unknown",
              "int",
              "uint",
              "llong",
              "ullong",
              "double",
              "boolean",
              "string")

/* Validate that PARAMS contains only recognized parameter names with
 * correct types, and with no duplicates.  Pass in as many name/type
 * pairs as appropriate, and pass NULL to end the list of accepted
 * parameters.  Return 0 on success, -1 on failure with error message
 * already issued.  */
int
virTypedParameterArrayValidate(virTypedParameterPtr params, int nparams, ...)
{
    va_list ap;
    int ret = -1;
    int i, j;
    const char *name;
    int type;

    va_start(ap, nparams);

    /* Yes, this is quadratic, but since we reject duplicates and
     * unknowns, it is constrained by the number of var-args passed
     * in, which is expected to be small enough to not be
     * noticeable.  */
    for (i = 0; i < nparams; i++) {
        va_end(ap);
        va_start(ap, nparams);

        name = va_arg(ap, const char *);
        while (name) {
            type = va_arg(ap, int);
            if (STREQ(params[i].field, name)) {
                if (params[i].type != type) {
                    const char *badtype;

                    badtype = virTypedParameterTypeToString(params[i].type);
                    if (!badtype)
                        badtype = virTypedParameterTypeToString(0);
                    virReportError(VIR_ERR_INVALID_ARG,
                                   _("invalid type '%s' for parameter '%s', "
                                     "expected '%s'"),
                                   badtype, params[i].field,
                                   virTypedParameterTypeToString(type));
                }
                break;
            }
            name = va_arg(ap, const char *);
        }
        if (!name) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("parameter '%s' not supported"),
                           params[i].field);
            goto cleanup;
        }
        for (j = 0; j < i; j++) {
            if (STREQ(params[i].field, params[j].field)) {
                virReportError(VIR_ERR_INVALID_ARG,
                               _("parameter '%s' occurs multiple times"),
                               params[i].field);
                goto cleanup;
            }
        }
    }

    ret = 0;
cleanup:
    va_end(ap);
    return ret;

}

/* Assign name, type, and the appropriately typed arg to param; in the
 * case of a string, the caller is assumed to have malloc'd a string,
 * or can pass NULL to have this function malloc an empty string.
 * Return 0 on success, -1 after an error message on failure.  */
int
virTypedParameterAssign(virTypedParameterPtr param, const char *name,
                        int type, ...)
{
    va_list ap;
    int ret = -1;

    va_start(ap, type);

    if (virStrcpyStatic(param->field, name) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Field name '%s' too long"),
                       name);
        goto cleanup;
    }
    param->type = type;
    switch (type)
    {
    case VIR_TYPED_PARAM_INT:
        param->value.i = va_arg(ap, int);
        break;
    case VIR_TYPED_PARAM_UINT:
        param->value.ui = va_arg(ap, unsigned int);
        break;
    case VIR_TYPED_PARAM_LLONG:
        param->value.l = va_arg(ap, long long int);
        break;
    case VIR_TYPED_PARAM_ULLONG:
        param->value.ul = va_arg(ap, unsigned long long int);
        break;
    case VIR_TYPED_PARAM_DOUBLE:
        param->value.d = va_arg(ap, double);
        break;
    case VIR_TYPED_PARAM_BOOLEAN:
        param->value.b = !!va_arg(ap, int);
        break;
    case VIR_TYPED_PARAM_STRING:
        param->value.s = va_arg(ap, char *);
        if (!param->value.s)
            param->value.s = strdup("");
        if (!param->value.s) {
            virReportOOMError();
            goto cleanup;
        }
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unexpected type %d for field %s"), type, name);
        goto cleanup;
    }

    ret = 0;
cleanup:
    va_end(ap);
    return ret;
}

/* Assign name, type, and convert the argument from a const string.
 * In case of a string, the string is copied.
 * Return 0 on success, -1 after an error message on failure.  */
int
virTypedParameterAssignFromStr(virTypedParameterPtr param, const char *name,
                               int type, const char *val)
{
    int ret = -1;

    if (!val) {
        virReportError(VIR_ERR_INVALID_ARG, _("NULL value for field '%s'"),
                       name);
        goto cleanup;
    }

    if (virStrcpyStatic(param->field, name) == NULL) {
        virReportError(VIR_ERR_INTERNAL_ERROR, _("Field name '%s' too long"),
                       name);
        goto cleanup;
    }

    param->type = type;
    switch (type) {
    case VIR_TYPED_PARAM_INT:
        if (virStrToLong_i(val, NULL, 10, &param->value.i) < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid value for field '%s': expected int"),
                           name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_UINT:
        if (virStrToLong_ui(val, NULL, 10, &param->value.ui) < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid value for field '%s': "
                             "expected unsigned int"),
                           name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_LLONG:
        if (virStrToLong_ll(val, NULL, 10, &param->value.l) < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid value for field '%s': "
                             "expected long long"),
                           name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_ULLONG:
        if (virStrToLong_ull(val, NULL, 10, &param->value.ul) < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid value for field '%s': "
                             "expected unsigned long long"),
                           name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_DOUBLE:
        if (virStrToDouble(val, NULL, &param->value.d) < 0) {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid value for field '%s': "
                             "expected double"),
                           name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_BOOLEAN:
        if (STRCASEEQ(val, "true") || STREQ(val, "1")) {
            param->value.b = true;
        } else if (STRCASEEQ(val, "false") || STREQ(val, "0")) {
            param->value.b = false;
        } else {
            virReportError(VIR_ERR_INVALID_ARG,
                           _("Invalid boolean value for field '%s'"), name);
            goto cleanup;
        }
        break;
    case VIR_TYPED_PARAM_STRING:
        if (!(param->value.s = strdup(val))) {
            virReportOOMError();
            goto cleanup;
        }
        break;
    default:
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("unexpected type %d for field %s"), type, name);
        goto cleanup;
    }

    ret = 0;
cleanup:
    return ret;
}


/* The following APIs are public and their signature may never change. */

/**
 * virTypedParamsGet:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 *
 * Finds typed parameter called @name.
 *
 * Returns pointer to the parameter or NULL if it does not exist in @params.
 */
virTypedParameterPtr
virTypedParamsGet(virTypedParameterPtr params,
                  int nparams,
                  const char *name)
{
    int i;

    virResetLastError();

    if (!params || !name)
        return NULL;

    for (i = 0; i < nparams; i++) {
        if (STREQ(params[i].field, name))
            return params + i;
    }

    return NULL;
}


#define VIR_TYPED_PARAM_CHECK_TYPE(check_type)                              \
    do { if (param->type != check_type) {                                   \
        virReportError(VIR_ERR_INVALID_ARG,                                 \
                       _("Invalid type '%s' requested for parameter '%s', " \
                         "actual type is '%s'"),                            \
                       virTypedParameterTypeToString(check_type),           \
                       name,                                                \
                       virTypedParameterTypeToString(param->type));         \
        virDispatchError(NULL);                                             \
        return -1;                                                          \
    } } while (0)


/**
 * virTypedParamsGetInt:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its int value in @value. The
 * function fails with VIR_ERR_INVALID_ARG error if the parameter does not
 * have the expected type. By passing NULL as @value, the function may be
 * used to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetInt(virTypedParameterPtr params,
                     int nparams,
                     const char *name,
                     int *value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_INT);
    if (value)
        *value = param->value.i;

    return 1;
}


/**
 * virTypedParamsGetUInt:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its unsigned int value in
 * @value. The function fails with VIR_ERR_INVALID_ARG error if the parameter
 * does not have the expected type. By passing NULL as @value, the function
 * may be used to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetUInt(virTypedParameterPtr params,
                      int nparams,
                      const char *name,
                      unsigned int *value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_UINT);
    if (value)
        *value = param->value.ui;

    return 1;
}


/**
 * virTypedParamsGetLLong:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its long long int value in
 * @value. The function fails with VIR_ERR_INVALID_ARG error if the parameter
 * does not have the expected type. By passing NULL as @value, the function
 * may be used to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetLLong(virTypedParameterPtr params,
                       int nparams,
                       const char *name,
                       long long *value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_LLONG);
    if (value)
        *value = param->value.l;

    return 1;
}


/**
 * virTypedParamsGetULLong:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its unsigned long long int
 * value in @value. The function fails with VIR_ERR_INVALID_ARG error if the
 * parameter does not have the expected type. By passing NULL as @value, the
 * function may be used to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetULLong(virTypedParameterPtr params,
                        int nparams,
                        const char *name,
                        unsigned long long *value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_ULLONG);
    if (value)
        *value = param->value.ul;

    return 1;
}


/**
 * virTypedParamsGetDouble:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its double value in @value.
 * The function fails with VIR_ERR_INVALID_ARG error if the parameter does not
 * have the expected type. By passing NULL as @value, the function may be used
 * to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetDouble(virTypedParameterPtr params,
                        int nparams,
                        const char *name,
                        double *value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_DOUBLE);
    if (value)
        *value = param->value.d;

    return 1;
}


/**
 * virTypedParamsGetBoolean:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its boolean value in @value.
 * The function fails with VIR_ERR_INVALID_ARG error if the parameter does not
 * have the expected type. By passing NULL as @value, the function may be used
 * to check presence and type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetBoolean(virTypedParameterPtr params,
                         int nparams,
                         const char *name,
                         int *value)
{
    virTypedParameterPtr param;

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_BOOLEAN);
    if (value)
        *value = !!param->value.b;

    return 1;
}


/**
 * virTypedParamsGetString:
 * @params: array of typed parameters
 * @nparams: number of parameters in the @params array
 * @name: name of the parameter to find
 * @value: where to store the parameter's value
 *
 * Finds typed parameter called @name and store its char * value in @value.
 * The function does not create a copy of the string and the caller must not
 * free the string @value points to. The function fails with
 * VIR_ERR_INVALID_ARG error if the parameter does not have the expected type.
 * By passing NULL as @value, the function may be used to check presence and
 * type of the parameter.
 *
 * Returns 1 on success, 0 when the parameter does not exist in @params, or
 * -1 on error.
 */
int
virTypedParamsGetString(virTypedParameterPtr params,
                        int nparams,
                        const char *name,
                        const char **value)
{
    virTypedParameterPtr param;

    virResetLastError();

    if (!(param = virTypedParamsGet(params, nparams, name)))
        return 0;

    VIR_TYPED_PARAM_CHECK_TYPE(VIR_TYPED_PARAM_STRING);
    if (value)
        *value = param->value.s;

    return 1;
}


#define VIR_TYPED_PARAM_CHECK()                                     \
    do { if (virTypedParamsGet(*params, n, name)) {                 \
        virReportError(VIR_ERR_INVALID_ARG,                         \
                       _("Parameter '%s' is already set"), name);   \
        goto error;                                                 \
    } } while (0)


/**
 * virTypedParamsAddInt:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with int type and sets its value to @value.
 * If @params array points to NULL or to a space that is not large enough to
 * accommodate the new parameter (@maxparams < @nparams + 1), the function
 * allocates more space for it and updates @maxparams. On success, @nparams
 * is incremented by one. The function fails with VIR_ERR_INVALID_ARG error
 * if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddInt(virTypedParameterPtr *params,
                     int *nparams,
                     int *maxparams,
                     const char *name,
                     int value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_INT, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddUInt:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with unsigned int type and sets its value
 * to @value. If @params array points to NULL or to a space that is not large
 * enough to accommodate the new parameter (@maxparams < @nparams + 1), the
 * function allocates more space for it and updates @maxparams. On success,
 * @nparams is incremented by one. The function fails with VIR_ERR_INVALID_ARG
 * error if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddUInt(virTypedParameterPtr *params,
                      int *nparams,
                      int *maxparams,
                      const char *name,
                      unsigned int value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_UINT, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddLLong:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with long long int type and sets its value
 * to @value. If @params array points to NULL or to a space that is not large
 * enough to accommodate the new parameter (@maxparams < @nparams + 1), the
 * function allocates more space for it and updates @maxparams. On success,
 * @nparams is incremented by one. The function fails with VIR_ERR_INVALID_ARG
 * error if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddLLong(virTypedParameterPtr *params,
                       int *nparams,
                       int *maxparams,
                       const char *name,
                       long long value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_LLONG, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddULLong:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with unsigned long long type and sets its
 * value to @value. If @params array points to NULL or to a space that is not
 * large enough to accommodate the new parameter (@maxparams < @nparams + 1),
 * the function allocates more space for it and updates @maxparams. On success,
 * @nparams is incremented by one. The function fails with VIR_ERR_INVALID_ARG
 * error if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddULLong(virTypedParameterPtr *params,
                        int *nparams,
                        int *maxparams,
                        const char *name,
                        unsigned long long value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_ULLONG, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddDouble:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with double type and sets its value to
 * @value. If @params array points to NULL or to a space that is not large
 * enough to accommodate the new parameter (@maxparams < @nparams + 1), the
 * function allocates more space for it and updates @maxparams. On success,
 * @nparams is incremented by one. The function fails with VIR_ERR_INVALID_ARG
 * error if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddDouble(virTypedParameterPtr *params,
                        int *nparams,
                        int *maxparams,
                        const char *name,
                        double value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_DOUBLE, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddBoolean:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with boolean type and sets its value to
 * @value. If @params array points to NULL or to a space that is not large
 * enough to accommodate the new parameter (@maxparams < @nparams + 1), the
 * function allocates more space for it and updates @maxparams. On success,
 * @nparams is incremented by one. The function fails with VIR_ERR_INVALID_ARG
 * error if the parameter already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddBoolean(virTypedParameterPtr *params,
                         int *nparams,
                         int *maxparams,
                         const char *name,
                         int value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_BOOLEAN, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddString:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @value: the value to store into the new parameter
 *
 * Adds new parameter called @name with char * type and sets its value to
 * @value. The function creates its own copy of @value string, which needs to
 * be freed using virTypedParamsFree or virTypedParamsClear. If @params array
 * points to NULL or to a space that is not large enough to accommodate the
 * new parameter (@maxparams < @nparams + 1), the function allocates more
 * space for it and updates @maxparams. On success, @nparams is incremented
 * by one. The function fails with VIR_ERR_INVALID_ARG error if the parameter
 * already exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddString(virTypedParameterPtr *params,
                        int *nparams,
                        int *maxparams,
                        const char *name,
                        const char *value)
{
    char *str = NULL;
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (value && !(str = strdup(value))) {
        virReportOOMError();
        goto error;
    }

    if (virTypedParameterAssign(*params + n, name,
                                VIR_TYPED_PARAM_STRING, str) < 0) {
        VIR_FREE(str);
        goto error;
    }

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsAddFromString:
 * @params: pointer to the array of typed parameters
 * @nparams: number of parameters in the @params array
 * @maxparams: maximum number of parameters that can be stored in @params
 *      array without allocating more memory
 * @name: name of the parameter to find
 * @type: type of the parameter
 * @value: the value to store into the new parameter encoded as a string
 *
 * Adds new parameter called @name with the requested @type and parses its
 * value from the @value string. If the requested type is string, the function
 * creates its own copy of the @value string, which needs to be freed using
 * virTypedParamsFree or virTypedParamsClear. If @params array points to NULL
 * or to a space that is not large enough to accommodate the new parameter
 * (@maxparams < @nparams + 1), the function allocates more space for it and
 * updates @maxparams. On success, @nparams is incremented by one. The
 * function fails with VIR_ERR_INVALID_ARG error if the parameter already
 * exists in @params.
 *
 * Returns 0 on success, -1 on error.
 */
int
virTypedParamsAddFromString(virTypedParameterPtr *params,
                            int *nparams,
                            int *maxparams,
                            const char *name,
                            int type,
                            const char *value)
{
    size_t max = *maxparams;
    size_t n = *nparams;

    virResetLastError();

    VIR_TYPED_PARAM_CHECK();
    if (VIR_RESIZE_N(*params, max, n, 1) < 0) {
        virReportOOMError();
        goto error;
    }
    *maxparams = max;

    if (virTypedParameterAssignFromStr(*params + n, name, type, value) < 0)
        goto error;

    *nparams += 1;
    return 0;

error:
    virDispatchError(NULL);
    return -1;
}


/**
 * virTypedParamsClear:
 * @params: the array of typed parameters
 * @nparams: number of parameters in the @params array
 *
 * Frees all memory used by string parameters. The memory occupied by @params
 * is not freed; use virTypedParamsFree if you want it to be freed too.
 *
 * Returns nothing.
 */
void
virTypedParamsClear(virTypedParameterPtr params,
                    int nparams)
{
    int i;

    if (!params)
        return;

    for (i = 0; i < nparams; i++) {
        if (params[i].type == VIR_TYPED_PARAM_STRING)
            VIR_FREE(params[i].value.s);
    }
}


/**
 * virTypedParamsFree:
 * @params: the array of typed parameters
 * @nparams: number of parameters in the @params array
 *
 * Frees all memory used by string parameters and the memory occupied by
 * @params.
 *
 * Returns nothing.
 */
void
virTypedParamsFree(virTypedParameterPtr params,
                   int nparams)
{
    virTypedParamsClear(params, nparams);
    VIR_FREE(params);
}
