//
//  ngx_stream_request_core_module.c
//  nginx-1.12
//
//  Created by xpwu on 2017/12/16.
//  Copyright © 2017年 xpwu. All rights reserved.
//

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream_request.h>

#ifdef this_module
#undef this_module
#endif
#define this_module ngx_stream_request_core_module

static ngx_int_t preconfiguration(ngx_conf_t* cf);
static ngx_int_t postconfiguration(ngx_conf_t* cf);
static void *ngx_stream_request_core_create_main_conf(ngx_conf_t *cf);
static char *ngx_stream_request_core_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_stream_request_core_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_request_core_merge_srv_conf(ngx_conf_t *cf, void *parent,
                                                    void *child);
static char *ngx_stream_request_core_bind(ngx_conf_t *cf, ngx_command_t *cmd,
                                          void *conf);
static u_char* ngx_stream_request_log_handler(ngx_log_t *log, u_char *buf, size_t len);

static void ngx_stream_close_request(ngx_stream_request_t*);

static void empty_handler(ngx_event_t *ev){}

typedef struct {
  ngx_queue_t wait_send;
  ngx_queue_t processing;
  
  ngx_int_t wait_send_cnt;
  ngx_int_t processing_cnt;
  ngx_int_t request_cnt;
  
  ngx_stream_cleanup_t* cleanups;
}request_core_ctx_t;

static ngx_command_t  ngx_stream_request_core_commands[] = {
  
  { ngx_string("request_variables_hash_max_size"),
    NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_STREAM_MAIN_CONF_OFFSET,
    offsetof(ngx_stream_request_core_main_conf_t, variables_hash_max_size),
    NULL },
  
  { ngx_string("request_variables_hash_bucket_size"),
    NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_STREAM_MAIN_CONF_OFFSET,
    offsetof(ngx_stream_request_core_main_conf_t, variables_hash_bucket_size),
    NULL },
  
  // upstream
  { ngx_string("request_proxy_bind"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_stream_request_core_bind,
    NGX_STREAM_SRV_CONF_OFFSET,
    0,
    NULL },
  
  { ngx_string("request_proxy_connect_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, connect_timeout),
    NULL },
  
  { ngx_string("request_proxy_next_upstream"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, next_upstream),
    NULL },
  
  { ngx_string("request_proxy_next_upstream_tries"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, next_upstream_tries),
    NULL },
  
  { ngx_string("request_proxy_next_upstream_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, next_upstream_timeout),
    NULL },
  
  { ngx_string("request_send_to_proxy_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, send_timeout),
    NULL},
  
  { ngx_string("request_receive_from_proxy_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, receive_timeout),
    NULL},
  
  { ngx_string("request_proxy_response_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, response_timeout),
    NULL},
  
  // ------- client ----------
  
  { ngx_string("request_receive_from_client_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, receive_from_client_timeout),
    NULL},
  
  { ngx_string("client_heartbeat"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, heartbeat),
    NULL},
  
  { ngx_string("request_send_to_client_timeout"),
    NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_STREAM_SRV_CONF_OFFSET,
    offsetof(ngx_stream_request_core_srv_conf_t, send_to_client_timeout),
    NULL},

  ngx_null_command
};

static ngx_stream_module_t  ngx_stream_request_core_module_ctx = {
  preconfiguration,
  postconfiguration,                                  /* postconfiguration */
  
  ngx_stream_request_core_create_main_conf,     /* create main configuration */
  ngx_stream_request_core_init_main_conf,       /* init main configuration */
  
  ngx_stream_request_core_create_srv_conf,      /* create server configuration */
  ngx_stream_request_core_merge_srv_conf        /* merge server configuration */
};

ngx_module_t  ngx_stream_request_core_module = {
  NGX_MODULE_V1,
  &ngx_stream_request_core_module_ctx,          /* module context */
  ngx_stream_request_core_commands,             /* module directives */
  NGX_STREAM_MODULE,                     /* module type */
  NULL,                                  /* init master */
  NULL,                                  /* init module */
  NULL,                                  /* init process */
  NULL,                                  /* init thread */
  NULL,                                  /* exit thread */
  NULL,                                  /* exit process */
  NULL,                                  /* exit master */
  NGX_MODULE_V1_PADDING
};

static ngx_int_t
preconfiguration(ngx_conf_t *cf)
{
  return ngx_stream_request_variables_add_core_vars(cf);
}

static ngx_int_t
postconfiguration(ngx_conf_t *cf)
{
  return ngx_stream_request_variables_init_vars(cf);
}

static void *ngx_stream_request_core_create_main_conf(ngx_conf_t *cf) {
  ngx_stream_request_core_main_conf_t  *conf;
  conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_request_core_main_conf_t));
  if (conf == NULL) {
    return NULL;
  }
  
  conf->variables_hash_max_size = NGX_CONF_UNSET_UINT;
  conf->variables_hash_bucket_size = NGX_CONF_UNSET_UINT;
  
  return conf;
}

static char *
ngx_stream_request_core_init_main_conf(ngx_conf_t *cf, void *conf)
{
  ngx_stream_request_core_main_conf_t *cmcf = conf;
  
  ngx_conf_init_uint_value(cmcf->variables_hash_max_size, 1024);
  ngx_conf_init_uint_value(cmcf->variables_hash_bucket_size, 64);
  
  cmcf->variables_hash_bucket_size =
  ngx_align(cmcf->variables_hash_bucket_size, ngx_cacheline_size);
  
  if (cmcf->ncaptures) {
    cmcf->ncaptures = (cmcf->ncaptures + 1) * 3;
  }
  
  return NGX_CONF_OK;
}

static void *
ngx_stream_request_core_create_srv_conf(ngx_conf_t *cf)
{
  ngx_stream_request_core_srv_conf_t  *conf;
  
  conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_request_core_srv_conf_t));
  if (conf == NULL) {
    return NULL;
  }
  
  /*
   * set by ngx_pcalloc():
   *
   *
   *     conf->upstream = NULL;
   *
   *     conf->init_parser = NULL;
   *     conf->parse_request = NULL;
   *     conf->build_response = NULL;
   *
   */
  
  conf->connect_timeout = NGX_CONF_UNSET_MSEC;
  conf->next_upstream_timeout = NGX_CONF_UNSET_MSEC;
  conf->next_upstream_tries = NGX_CONF_UNSET_UINT;
  conf->next_upstream = NGX_CONF_UNSET;
  conf->local = NGX_CONF_UNSET_PTR;
  conf->receive_timeout = NGX_CONF_UNSET_MSEC;
  conf->send_timeout = NGX_CONF_UNSET_MSEC;
  conf->response_timeout = NGX_CONF_UNSET_MSEC;
  
  conf->heartbeat = NGX_CONF_UNSET_MSEC;
  conf->receive_from_client_timeout = NGX_CONF_UNSET_MSEC;
  conf->send_to_client_timeout = NGX_CONF_UNSET_MSEC;
  
  ngx_array_init(&conf->handlers, cf->pool, 1, sizeof(ngx_stream_request_handler_t));
  
  return conf;
}

static char *
ngx_stream_request_core_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
  ngx_stream_request_core_srv_conf_t *prev = parent;
  ngx_stream_request_core_srv_conf_t *conf = child;
  
  ngx_conf_merge_msec_value(conf->send_to_client_timeout,
                            prev->send_to_client_timeout, 10000);
  ngx_conf_merge_msec_value(conf->heartbeat, prev->heartbeat, 4*60000);
  ngx_conf_merge_msec_value(conf->receive_from_client_timeout, prev->receive_from_client_timeout, 10000);

  ngx_conf_merge_msec_value(conf->connect_timeout,
                            prev->connect_timeout, 60000);
  ngx_conf_merge_msec_value(conf->next_upstream_timeout,
                            prev->next_upstream_timeout, 0);
  ngx_conf_merge_uint_value(conf->next_upstream_tries,
                            prev->next_upstream_tries, 0);
  ngx_conf_merge_value(conf->next_upstream, prev->next_upstream, 1);
  ngx_conf_merge_ptr_value(conf->local, prev->local, NULL);
  ngx_conf_merge_msec_value(conf->send_timeout, prev->send_timeout, 5000);
  ngx_conf_merge_msec_value(conf->receive_timeout, prev->receive_timeout, 5000);
  ngx_conf_merge_msec_value(conf->response_timeout, prev->response_timeout, 10000);
  
  // handler
  conf->upstream = conf->upstream == NULL? prev->upstream : conf->upstream;
  conf->init_parser = conf->init_parser == NULL? prev->init_parser : conf->init_parser;
  conf->parse_request = conf->parse_request == NULL
  ? prev->parse_request : conf->parse_request;
  conf->build_response = conf->build_response == NULL
  ? prev->build_response : conf->build_response;
  
  // handlers do not merge
  
  if (conf->handlers.nelts == 0) {
    conf->handlers.elts = prev->handlers.elts;
    conf->handlers.nalloc = prev->handlers.nalloc;
    conf->handlers.nelts = prev->handlers.nelts;
    conf->handlers.pool = prev->handlers.pool;
    conf->handlers.size = prev->handlers.size;
  }
  
  return NGX_CONF_OK;
}

static char *
ngx_stream_request_core_bind(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
  ngx_stream_request_core_srv_conf_t *pscf = conf;
  
  ngx_int_t   rc;
  ngx_str_t  *value;
  
  if (pscf->local != NGX_CONF_UNSET_PTR) {
    return "is duplicate";
  }
  
  value = cf->args->elts;
  
  if (ngx_strcmp(value[1].data, "off") == 0) {
    pscf->local = NULL;
    return NGX_CONF_OK;
  }
  
  pscf->local = ngx_palloc(cf->pool, sizeof(ngx_addr_t));
  if (pscf->local == NULL) {
    return NGX_CONF_ERROR;
  }
  
  rc = ngx_parse_addr(cf->pool, pscf->local, value[1].data, value[1].len);
  
  switch (rc) {
    case NGX_OK:
      pscf->local->name = value[1];
      return NGX_CONF_OK;
      
    case NGX_DECLINED:
      ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "invalid address \"%V\"", &value[1]);
      /* fall through */
      
    default:
      return NGX_CONF_ERROR;
  }
}

static u_char* ngx_stream_request_log_handler(ngx_log_t *log, u_char *buf, size_t len) {
  ngx_stream_session_t* s = log->data;
  u_char* p = buf;
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  
  p = ngx_snprintf(p, len,
                   ", session_ptr=%p, all_request=%ud"
                   ", wait_send_cnt=%ud, processing_cnt=%ud\n"
                   , s, ctx->request_cnt, ctx->wait_send_cnt
                   , ctx->processing_cnt);
  return p;
}

#if defined ( __clang__ ) && defined ( __llvm__ )
#pragma mark - event handler
#endif

static ngx_stream_request_t* ngx_stream_init_request(ngx_stream_session_t*);

static void ngx_stream_read_handler(ngx_event_t *e) {
  ngx_connection_t* c = e->data;
  ngx_stream_session_t* s = c->data;
  ngx_stream_request_t* r = NULL;
  ngx_stream_request_core_srv_conf_t  *pscf;
  
  pscf = ngx_stream_get_module_srv_conf(s, this_module);
  if (e->timedout) {
    ngx_stream_finalize_session_r(s, "ngx_stream_read_handler timeout");
    return;
  }
  if (e->timer_set) {
    ngx_del_timer(e);
  }
  
  r = ngx_stream_init_request(s);
  if (r == NGX_STREAM_REQUEST_ERROR) {
    ngx_stream_finalize_session_r(s, "ngx_stream_create_request error"
                                  " or connection closed by client");
    return;
  }
  if (r != NULL) {
    ngx_stream_handle_request(r);
  } else {
    // 超时时间由具体的parse自行设置
    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
      ngx_stream_finalize_session_r(s, "ngx_handle_read_event error");
      return;
    }
    if (!c->read->ready) {
      return;
    }
  }
  // 如果有数据，就需要再次放入事件队列中
  if (c->read->ready) {
    ngx_post_event(c->read, &ngx_posted_events);
  }
}

static void ngx_stream_write_handler(ngx_event_t *e) {
  ngx_connection_t* c = e->data;
  ngx_stream_session_t* s = c->data;
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  
  if (e->timedout) {
    ngx_stream_finalize_session_r(s, "ngx_stream_write_handler timeout");
    return;
  }
  if (e->timer_set) {
    ngx_del_timer(e);
  }
  
  if (ngx_queue_empty(&ctx->wait_send)) {
    return;
  }
  ngx_queue_t* q = NULL;
  for (q = ngx_queue_head(&ctx->wait_send)
       ; q != ngx_queue_sentinel(&ctx->wait_send)
       ; ) {
    ngx_stream_request_t* r = ngx_queue_data(q, ngx_stream_request_t, list);
    
    /**
     ngx_darwin_sendfile_chain.c 中的ngx_output_chain_to_iovec没有考虑ngx_buf_t size=0
     的情况，会造成writev 卡死的bug
     */
    ngx_stream_request_regular_data(r);
    if (r->data == NULL) {
      // need firstly next, then close request
      q = ngx_queue_next(q);
      // close_request will remove q
      ngx_stream_close_request(r);
      continue;
    }
    
    ngx_chain_t* rc = c->send_chain(c, r->data, 0);
    if (rc == NGX_CHAIN_ERROR) {
      ngx_stream_finalize_session_r(s, "send_chain error");
      return;
    }
    if (rc != NULL) {
      r->data = rc;
      break;
    }
    
    // need firstly next, then close request
    q = ngx_queue_next(q);
    // close_request will remove q
    ngx_stream_close_request(r);
  }
  
  if (ngx_queue_empty(&ctx->wait_send)) {
    return;
  }
  
  ngx_stream_request_core_srv_conf_t  *pscf;
  pscf = ngx_stream_get_module_srv_conf(s, this_module);
  ngx_add_timer(e, pscf->send_to_client_timeout);
  if (ngx_handle_write_event(e, 0) != NGX_OK) {
    ngx_stream_finalize_session_r(s, "ngx_handle_write_event error");
    return;
  }
}

ngx_stream_request_handler_t*
ngx_stream_request_add_handler(ngx_stream_request_core_srv_conf_t* cscf) {
  ngx_stream_request_handler_t* re = ngx_array_push(&cscf->handlers);
  re->build_response = NULL;
  re->handle_request = NULL;
  
  return re;
}

#if defined ( __clang__ ) && defined ( __llvm__ )
#pragma mark - request handler
#endif

static void empty_u_r(ngx_stream_request_t*r) {}
static void empty_u_r_f(ngx_stream_request_t*r, char* reason) {}

static ngx_stream_request_t* ngx_stream_init_request(ngx_stream_session_t* s) {
  ngx_stream_request_t* r = NULL;
  ngx_stream_request_core_srv_conf_t  *pscf;
  ngx_stream_request_upstream_t           *ru;
  ngx_stream_upstream_t           *u;
  ngx_stream_upstream_srv_conf_t  *uscf;
  
  pscf = ngx_stream_get_module_srv_conf(s, this_module);
  r = pscf->parse_request != NULL? pscf->parse_request(s) : NULL;
  if (r == NULL || r == NGX_STREAM_REQUEST_ERROR) {
    return r;
  }
  
  if (pscf->upstream == NULL) {
    return r;
  }
  
  ru = ngx_pcalloc(r->pool, sizeof(ngx_stream_request_upstream_t));
  ru->upstream_connected = empty_u_r;
  ru->upstream_connect_failed = empty_u_r_f;
  if (ru == NULL) {
    ngx_stream_finalize_session_r(r->session, "ngx_pcalloc error");
    return NULL;
  }
  
  u = &ru->upstream;
  u->peer.log = s->connection->log;
  u->peer.log_error = NGX_ERROR_ERR;
  
  u->peer.local = pscf->local;
  u->peer.type = s->connection->type;
  
  uscf = pscf->upstream;
  // 为了兼容session upstream的现有逻辑，在peer.init执行前需要将s->connection->pool换为r->pool
  // peer.init所需要的内存在r->pool 中申请，请参见ngx_stream_upstream_round_robin.c 中
  // ngx_stream_upstream_init_round_robin_peer 的实现
  do {
    s->upstream = u;
    ngx_pool_t* pool = s->connection->pool;
    s->connection->pool = r->pool;
    ngx_int_t rc = uscf->peer.init(s, uscf);
    s->connection->pool = pool;
    if (rc != NGX_OK) {
      ngx_stream_finalize_session_r(r->session, "peer.init error");
      return NULL;
    }
    r->upstream = ru;
    s->upstream = NULL;
  } while (0);
  
  
  u->peer.start_time = ngx_current_msec;
  
  if (pscf->next_upstream_tries
      && u->peer.tries > pscf->next_upstream_tries)
  {
    u->peer.tries = pscf->next_upstream_tries;
  }
  
  u->proxy_protocol = 0;
  u->start_sec = ngx_time();
  
  return r;
}

static void ngx_stream_close_request(ngx_stream_request_t* r) {
  ngx_stream_session_t* s = r->session;
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  
  ngx_queue_remove(&r->list);
  ctx->wait_send_cnt--;
  
  ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "close request %p", r);
  
  ngx_stream_request_cleanup_t* cln = NULL;
  for (cln = r->cln; cln; cln = cln->next) {
    if (cln->handler) {
      cln->handler(cln->data);
      cln->handler = NULL;
    }
  }
  
  ngx_stream_upstream_t* u;
  
  u = &r->upstream->upstream;
  
  if (u != NULL && u->peer.connection != NULL) {
    ngx_close_connection(u->peer.connection);
  }
  
  ngx_pool_t* pool = r->pool;
  ngx_destroy_pool(pool);
}

extern ngx_stream_request_t* ngx_stream_new_request(ngx_stream_session_t* s) {
  ngx_pool_t* pool = ngx_create_pool(2000, s->connection->log);
  ngx_stream_request_t* r = ngx_pcalloc(pool, sizeof(ngx_stream_request_t));
  r->pool = pool;
  r->session = s;
  r->data = ngx_pcalloc(pool, sizeof(ngx_chain_t));
  r->data->buf = ngx_create_temp_buf(r->pool, 1);
  r->ctx = ngx_pcalloc(pool, sizeof(void**)*ngx_stream_max_module);
  
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  ngx_queue_insert_tail(&ctx->processing, &r->list);
  ctx->request_cnt++;
  ctx->processing_cnt++;
  
  ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "new request %p", r);
  
  return r;
}

extern void ngx_stream_handle_request(ngx_stream_request_t* r) {
  ngx_stream_session_t* s = r->session;
  ngx_connection_t* c = s->connection;
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  ngx_stream_request_core_srv_conf_t  *pscf;
  ngx_stream_request_handler_t* handlers;
  ngx_int_t rc;
  ngx_log_t* log = s->connection->log;
  
  pscf = ngx_stream_get_module_srv_conf(s, this_module);
  handlers = pscf->handlers.elts;
  
  // handler request
  for (; r->handler_index < pscf->handlers.nelts; ++r->handler_index) {
    if (handlers[r->handler_index].handle_request == NULL) {
      continue;
    }
    rc = handlers[r->handler_index].handle_request(r);
    if (rc == NGX_AGAIN) {
      return;
    } else if (rc == NGX_ERROR) {
      ngx_log_error(NGX_LOG_ERR, log, 0
                    , "request handler error for handling request, index = %u"
                    , r->handler_index);
      r->handler_index = 2*pscf->handlers.nelts - r->handler_index - 1;
      break;
    }
  }
  
  // handler response
  for (; r->handler_index < 2*pscf->handlers.nelts; ++r->handler_index) {
    if (handlers[r->handler_index].build_response == NULL) {
      continue;
    }
    rc = handlers[2*pscf->handlers.nelts - 1 - r->handler_index].build_response(r);
    if (rc == NGX_AGAIN) {
      return;
    } else if (rc == NGX_ERROR) {
      ngx_log_error(NGX_LOG_ERR, log, 0
                    , "request handler error for building response, index = %u"
                    , 2*pscf->handlers.nelts - 1 - r->handler_index);
    }
  }
  
  // protocol response
  if (pscf->build_response) {
    pscf->build_response(r);
  }
  
  ngx_queue_remove(&r->list);
  ctx->processing_cnt--;
  ngx_queue_insert_tail(&ctx->wait_send, &r->list);
  ctx->wait_send_cnt++;
  
  ngx_post_event(c->write, &ngx_posted_events);
}

extern void ngx_stream_finalize_session_r(ngx_stream_session_t *s, char* reason) {
  ngx_log_t* log = s->connection->log;
  log->action = NULL;
  
  ngx_log_error(NGX_LOG_ERR, log, 0, "finalize session because %s", reason);
  
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  ngx_queue_t* q = NULL;
  for (q = ngx_queue_head(&ctx->processing)
       ; q != ngx_queue_sentinel(&ctx->processing)
       ; ) {
    ngx_queue_t* qtmp = q;
    q = ngx_queue_next(q);
    ngx_stream_close_request(ngx_queue_data(qtmp, ngx_stream_request_t, list));
  }
  for (q = ngx_queue_head(&ctx->wait_send)
       ; q != ngx_queue_sentinel(&ctx->wait_send)
       ; ) {
    ngx_queue_t* qtmp = q;
    q = ngx_queue_next(q);
    ngx_stream_close_request(ngx_queue_data(qtmp, ngx_stream_request_t, list));
  }
  
  ngx_stream_cleanup_t * cln = NULL;
  for (cln = ctx->cleanups; cln; cln = cln->next) {
    if (cln->handler) {
      cln->handler(cln->data);
      cln->handler = NULL;
    }
  }
  
  ngx_stream_finalize_session(s, NGX_STREAM_BAD_REQUEST);
}

extern ngx_stream_cleanup_t * ngx_stream_cleanup_add(ngx_stream_session_t *s) {
  request_core_ctx_t* ctx = ngx_stream_get_module_ctx(s, this_module);
  ngx_stream_cleanup_t  *cln;
  cln = ngx_palloc(s->connection->pool, sizeof(ngx_stream_cleanup_t));
  if (cln == NULL) {
    return NULL;
  }
  
  cln->data = NULL;
  cln->handler = NULL;
  cln->next = ctx->cleanups;
  
  ctx->cleanups = cln;
  
  return cln;
}

extern
ngx_stream_request_cleanup_t * ngx_stream_request_cleanup_add(ngx_stream_request_t* r){
  ngx_stream_request_cleanup_t  *cln;
  cln = ngx_palloc(r->pool, sizeof(ngx_stream_request_cleanup_t));
  if (cln == NULL) {
    return NULL;
  }
  
  cln->data = NULL;
  cln->handler = NULL;
  cln->next = r->cln;
  
  r->cln = cln;
  
  return cln;
}



