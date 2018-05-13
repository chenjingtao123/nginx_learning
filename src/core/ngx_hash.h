
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    void             *value;/*指向用户自定义元素数据*/
    u_short           len;/*元素关键字的长度*/
    u_char            name[1];/*元素关键字首地址 柔性数组->不占空间大小*/
} ngx_hash_elt_t;


typedef struct {
    ngx_hash_elt_t  **buckets;//指针的指针可以完整的表示二维数组
    ngx_uint_t        size;//散列表中槽的个数
} ngx_hash_t;//拉链法


typedef struct {
    ngx_hash_t        hash;
    void             *value;//value指针可以指向用户数据
} ngx_hash_wildcard_t;//支持通配符的散列表


typedef struct {
    ngx_str_t         key;//元素关键字
    ngx_uint_t        key_hash;//计算出来的hash
    void             *value;//实际用户数据
} ngx_hash_key_t;

//散列方法的函数指针定义
//传入关键字的首地址，len是关键字的长度
typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


typedef struct {
    //精准匹配的散列表
    ngx_hash_t            hash;
    //查询前置通配符的散列表
    ngx_hash_wildcard_t  *wc_head;
    //查询后置通配符的散列表
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;


typedef struct {
    ngx_hash_t       *hash;//指向普通的完全匹配散列表
    ngx_hash_key_pt   key;//hash函数指针

    ngx_uint_t        max_size;//hash表中的桶的个数。该字段越大，元素存储时冲突的可能性越小，每个桶中存储的元素会更少
    ngx_uint_t        bucket_size;//每个bucket的空间，这就限制了关键字的最大长度,bucket_size=该桶上这个链表里所有元素的容量和

    char             *name;//散列表名字
    ngx_pool_t       *pool;//给散列表分配空间的内存池
    ngx_pool_t       *temp_pool;
} ngx_hash_init_t;


#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


typedef struct {
    ngx_uint_t        hsize;

    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;

    ngx_array_t       keys;
    ngx_array_t      *keys_hash;

    ngx_array_t       dns_wc_head;
    ngx_array_t      *dns_wc_head_hash;

    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;
//通过构造ngx_hash_keys_arrays_t(其实也是一个简易的hash表)，辅助我们构造最终的hash表


typedef struct {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
} ngx_table_elt_t;


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);


ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
