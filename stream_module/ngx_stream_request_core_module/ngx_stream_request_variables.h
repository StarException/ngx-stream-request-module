
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_STREAM_REQUEST_VARIABLES_H_INCLUDED_
#define _NGX_STREAM_REQUEST_VARIABLES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream_request.h>


typedef ngx_variable_value_t  ngx_stream_request_variable_value_t;

#define ngx_stream_request_variable(v)     { sizeof(v) - 1, 1, 0, 0, 0, (u_char *) v }

typedef struct ngx_stream_request_variable_s  ngx_stream_request_variable_t;

typedef void (*ngx_stream_request_set_variable_pt) (ngx_stream_request_t *r,
    ngx_stream_request_variable_value_t *v, uintptr_t data);
typedef ngx_int_t (*ngx_stream_request_get_variable_pt) (ngx_stream_request_t *r,
    ngx_stream_request_variable_value_t *v, uintptr_t data);


#define NGX_STREAM_REQUEST_VAR_CHANGEABLE   NGX_STREAM_VAR_CHANGEABLE
#define NGX_STREAM_REQUEST_VAR_NOCACHEABLE  NGX_STREAM_VAR_NOCACHEABLE
#define NGX_STREAM_REQUEST_VAR_INDEXED      NGX_STREAM_VAR_INDEXED
#define NGX_STREAM_REQUEST_VAR_NOHASH       NGX_STREAM_VAR_NOHASH
#define NGX_STREAM_REQUEST_VAR_WEAK         NGX_STREAM_VAR_WEAK
#define NGX_STREAM_REQUEST_VAR_PREFIX       NGX_STREAM_VAR_PREFIX


struct ngx_stream_request_variable_s {
    ngx_str_t                     name;   /* must be first to build the hash */
    ngx_stream_request_set_variable_pt    set_handler;
    ngx_stream_request_get_variable_pt    get_handler;
    uintptr_t                     data;
    ngx_uint_t                    flags;
    ngx_uint_t                    index;
};


ngx_stream_request_variable_t *ngx_stream_request_add_variable(ngx_conf_t *cf
    , ngx_str_t *name, ngx_uint_t flags);
ngx_int_t ngx_stream_request_get_variable_index(ngx_conf_t *cf, ngx_str_t *name);

ngx_stream_request_variable_value_t *ngx_stream_request_get_indexed_variable(
    ngx_stream_request_t *r, ngx_uint_t index);
ngx_stream_request_variable_value_t *ngx_stream_request_get_flushed_variable(
    ngx_stream_request_t *r, ngx_uint_t index);

ngx_stream_request_variable_value_t *ngx_stream_request_get_variable(
    ngx_stream_request_t *r, ngx_str_t *name, ngx_uint_t key);


#if (NGX_PCRE)

typedef struct {
    ngx_uint_t                    capture;
    ngx_int_t                     index;
} ngx_stream_request_regex_variable_t;


typedef struct {
    ngx_regex_t                  *regex;
    ngx_uint_t                    ncaptures;
    ngx_stream_request_regex_variable_t  *variables;
    ngx_uint_t                    nvariables;
    ngx_str_t                     name;
} ngx_stream_request_regex_t;


typedef struct {
    ngx_stream_request_regex_t           *regex;
    void                         *value;
} ngx_stream_request_map_regex_t;


ngx_stream_request_regex_t *ngx_stream_request_regex_compile(ngx_conf_t *cf,
    ngx_regex_compile_t *rc);
ngx_int_t ngx_stream_request_regex_exec(ngx_stream_request_t *r, ngx_stream_request_regex_t *re,
    ngx_str_t *str);

#endif


typedef struct {
    ngx_hash_combined_t           hash;
#if (NGX_PCRE)
    ngx_stream_request_map_regex_t       *regex;
    ngx_uint_t                    nregex;
#endif
} ngx_stream_request_map_t;


void *ngx_stream_request_map_find(ngx_stream_request_t *r, ngx_stream_request_map_t *map,
    ngx_str_t *match);


ngx_int_t ngx_stream_request_variables_add_core_vars(ngx_conf_t *cf);
ngx_int_t ngx_stream_request_variables_init_vars(ngx_conf_t *cf);

extern void ngx_stream_regular_var_name(ngx_str_t*);

extern ngx_stream_request_variable_value_t  ngx_stream_request_variable_null_value;
extern ngx_stream_request_variable_value_t  ngx_stream_request_variable_true_value;


#endif /* _NGX_STREAM_VARIABLES_H_INCLUDED_ */
