#include "php7_ext_wrapper.h"
#include "ext/standard/info.h"
#include <zend_extensions.h>
#include <zend_exceptions.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include "common_define.h"
#include "logger.h"
#include "shared_func.h"
#include "shmcache_types.h"
#include "shmcache.h"
#include "php_shmcache.h"

#define MAJOR_VERSION  1
#define MINOR_VERSION  0
#define PATCH_VERSION  0

typedef struct
{
#if PHP_MAJOR_VERSION < 7
	zend_object zo;
#endif
	struct shmcache_context context;
#if PHP_MAJOR_VERSION >= 7
	zend_object zo;
#endif
} php_shmcache_t;

#if PHP_MAJOR_VERSION < 7
#define shmcache_get_object(obj) zend_object_store_get_object(obj)
#else
#define shmcache_get_object(obj) (void *)((char *)(Z_OBJ_P(obj)) - XtOffsetOf(php_shmcache_t, zo))
#endif

static int le_shmcache;

static zend_class_entry *shmcache_ce = NULL;
static zend_class_entry *shmcache_exception_ce = NULL;

#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers shmcache_object_handlers;
#endif

#if HAVE_SPL
static zend_class_entry *spl_ce_RuntimeException = NULL;
#endif

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION < 3)
const zend_fcall_info empty_fcall_info = { 0, NULL, NULL, NULL, NULL, 0, NULL, NULL, 0 };
#undef ZEND_BEGIN_ARG_INFO_EX
#define ZEND_BEGIN_ARG_INFO_EX(name, pass_rest_by_reference, return_reference, required_num_args) \
    static zend_arg_info name[] = {                                                               \
        { NULL, 0, NULL, 0, 0, 0, pass_rest_by_reference, return_reference, required_num_args },
#endif

// Every user visible function must have an entry in shmcache_functions[].
	zend_function_entry shmcache_functions[] = {
		ZEND_FE(shmcache_version, NULL)
		{NULL, NULL, NULL}  /* Must be the last line */
	};

zend_module_entry shmcache_module_entry = {
	STANDARD_MODULE_HEADER,
	"shmcache",
	shmcache_functions,
	PHP_MINIT(shmcache),
	PHP_MSHUTDOWN(shmcache),
	NULL,//PHP_RINIT(shmcache),
	NULL,//PHP_RSHUTDOWN(shmcache),
	PHP_MINFO(shmcache),
	"1.00",
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SHMCACHE
	ZEND_GET_MODULE(shmcache)
#endif

static void php_shmcache_destroy(php_shmcache_t *i_obj)
{
	zend_object_std_dtor(&i_obj->zo TSRMLS_CC);
	efree(i_obj);
}


ZEND_RSRC_DTOR_FUNC(php_shmcache_dtor)
{
#if PHP_MAJOR_VERSION < 7
	if (rsrc->ptr != NULL)
	{
		php_shmcache_t *i_obj = (php_shmcache_t *)rsrc->ptr;
		php_shmcache_destroy(i_obj TSRMLS_CC);
		rsrc->ptr = NULL;
	}
#else
	if (res->ptr != NULL)
	{
		php_shmcache_t *i_obj = (php_shmcache_t *)res->ptr;
		php_shmcache_destroy(i_obj TSRMLS_CC);
		res->ptr = NULL;
	}
#endif

}

#if PHP_MAJOR_VERSION < 7
zend_object_value php_shmcache_new(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value retval;
	php_shmcache_t *i_obj;

	i_obj = (php_shmcache_t *)ecalloc(1, sizeof(php_shmcache_t));

	zend_object_std_init(&i_obj->zo, ce TSRMLS_CC);
	retval.handle = zend_objects_store_put(i_obj, \
		(zend_objects_store_dtor_t)zend_objects_destroy_object, \
        NULL, NULL TSRMLS_CC);
	retval.handlers = zend_get_std_object_handlers();

	return retval;
}

#else

zend_object* php_shmcache_new(zend_class_entry *ce)
{
	php_shmcache_t *i_obj;

	i_obj = (php_shmcache_t *)ecalloc(1, sizeof(php_shmcache_t) + zend_object_properties_size(ce));

	zend_object_std_init(&i_obj->zo, ce TSRMLS_CC);
    object_properties_init(&i_obj->zo, ce);
    i_obj->zo.handlers = &shmcache_object_handlers;
	return &i_obj->zo;
}

#endif

/* ShmCache::__construct([int config_index = 0, bool bMultiThread = false])
   Creates a ShmCache object */
static PHP_METHOD(ShmCache, __construct)
{
	char *config_filename;
    zend_size_t filename_len;
	zval *object;
	php_shmcache_t *i_obj;

    object = getThis();
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s",
			&config_filename, &filename_len) == FAILURE)
	{
		logError("file: "__FILE__", line: %d, "
			"zend_parse_parameters fail!", __LINE__);
		ZVAL_NULL(object);
		return;
	}

	i_obj = (php_shmcache_t *) shmcache_get_object(object);
}

static PHP_METHOD(ShmCache, __destruct)
{
	zval *object = getThis();
	php_shmcache_t *i_obj;

	i_obj = (php_shmcache_t *) shmcache_get_object(object);
	php_shmcache_destroy(i_obj);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo___construct, 0, 0, 1)
ZEND_ARG_INFO(0, config_filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo___destruct, 0, 0, 0)
ZEND_END_ARG_INFO()

#define SHMC_ME(name, args) PHP_ME(ShmCache, name, args, ZEND_ACC_PUBLIC)
static zend_function_entry shmcache_class_methods[] = {
    SHMC_ME(__construct,        arginfo___construct)
    SHMC_ME(__destruct,         arginfo___destruct)
    { NULL, NULL, NULL }
};


PHP_MINIT_FUNCTION(shmcache)
{
	zend_class_entry ce;

	log_init();

#if PHP_MAJOR_VERSION >= 7
	memcpy(&shmcache_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	shmcache_object_handlers.offset = XtOffsetOf(php_shmcache_t, zo);
	shmcache_object_handlers.free_obj = NULL;
	shmcache_object_handlers.clone_obj = NULL;
#endif

	le_shmcache = zend_register_list_destructors_ex(NULL, php_shmcache_dtor, \
			"ShmCache", module_number);

	INIT_CLASS_ENTRY(ce, "ShmCache", shmcache_class_methods);
	shmcache_ce = zend_register_internal_class(&ce TSRMLS_CC);
	shmcache_ce->create_object = php_shmcache_new;

	INIT_CLASS_ENTRY(ce, "ShmCacheException", NULL);
#if PHP_MAJOR_VERSION < 7
	shmcache_exception_ce = zend_register_internal_class_ex(&ce, \
		php_shmcache_get_exception_base(0 TSRMLS_CC), NULL TSRMLS_CC);
#else
	shmcache_exception_ce = zend_register_internal_class_ex(&ce, \
		php_shmcache_get_exception_base(0 TSRMLS_CC));
#endif

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(shmcache)
{
	log_destroy();
	return SUCCESS;
}

PHP_RINIT_FUNCTION(shmcache)
{
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(shmcache)
{
	return SUCCESS;
}

PHP_MINFO_FUNCTION(shmcache)
{
	char mc_info[64];
	sprintf(mc_info, "shmcache v%d.%02d support", 
		MAJOR_VERSION, MINOR_VERSION);

	php_info_print_table_start();
	php_info_print_table_header(2, mc_info, "enabled");
	php_info_print_table_end();
}

/*
string shmcache_version()
return client library version
*/
ZEND_FUNCTION(shmcache_version)
{
	char szVersion[16];
	int len;

	len = sprintf(szVersion, "%d.%d.%d",
		MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);

	ZEND_RETURN_STRINGL(szVersion, len, 1);
}

PHP_SHMCACHE_API zend_class_entry *php_shmcache_get_ce(void)
{
	return shmcache_ce;
}

PHP_SHMCACHE_API zend_class_entry *php_shmcache_get_exception(void)
{
	return shmcache_exception_ce;
}

PHP_SHMCACHE_API zend_class_entry *php_shmcache_get_exception_base(int root TSRMLS_DC)
{
#if HAVE_SPL
	if (!root)
	{
		if (!spl_ce_RuntimeException)
		{
			zend_class_entry *pce;
			zval *value;

			if (zend_hash_find_wrapper(CG(class_table), "runtimeexception",
			   sizeof("RuntimeException"), &value) == SUCCESS)
			{
				pce = Z_CE_P(value);
				spl_ce_RuntimeException = pce;
				return pce;
			}
			else
			{
				return NULL;
			}
		}
		else
		{
			return spl_ce_RuntimeException;
		}
	}
#endif
#if (PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 2)
	return zend_exception_get_default();
#else
	return zend_exception_get_default(TSRMLS_C);
#endif
}