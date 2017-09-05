#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <ffi.h>

#include "php.h"
#include "zend_exceptions.h"
#include "php_mffi.h"
#include "mffi_internal.h"

zend_class_entry *mffi_ce_struct;

static zend_object_handlers mffi_struct_object_handlers;

static php_mffi_struct_definition *php_mffi_struct_get_definition(zend_object *obj);
static php_mffi_struct_definition *php_mffi_make_struct_definition(zval *elements);

#if PHP_VERSION_ID >= 70100
#define GET_CALLED_SCOPE() zend_get_called_scope(EG(current_execute_data))
#else
#define GET_CALLED_SCOPE() EX(called_scope)
#endif

/* {{{ PHP_METHOD(MFFI_Struct, __construct */
PHP_METHOD(MFFI_Struct, __construct)
{
	zend_object *obj = NULL;
	php_mffi_struct_object *intern = NULL;

	PHP_MFFI_ERROR_HANDLING();
	if (zend_parse_parameters_none() == FAILURE) {
		PHP_MFFI_RESTORE_ERRORS();
		return;
	}
	PHP_MFFI_RESTORE_ERRORS();

	obj = Z_OBJ_P(getThis());
	intern = php_mffi_struct_fetch_object(obj);

	if (intern->template == NULL) {
		intern->template = php_mffi_struct_get_definition(obj);
	}

	intern->data = ecalloc(1, intern->template->struct_size);
}

/* }}} */

/* {{{ PHP_METHOD(MFFI_Struct, pointer) */
PHP_METHOD(MFFI_Struct, pointer)
{
	php_mffi_struct_object *intern = NULL;

	PHP_MFFI_ERROR_HANDLING();
	if (zend_parse_parameters_none() == FAILURE) {
		PHP_MFFI_RESTORE_ERRORS();
		return;
	}
	PHP_MFFI_RESTORE_ERRORS();

	object_init_ex(return_value, GET_CALLED_SCOPE());
	intern = php_mffi_struct_fetch_object(Z_OBJ_P(return_value));

	if (intern->template == NULL) {
		intern->template = php_mffi_struct_get_definition(Z_OBJ_P(return_value));
	}

	intern->data = NULL;
}
/* }}} */

/* {{{ PHP_METHOD(MFFI_Struct, byReference) */
PHP_METHOD(MFFI_Struct, byReference)
{
	php_mffi_struct_definition *def;

	PHP_MFFI_ERROR_HANDLING();
	if (zend_parse_parameters_none() == FAILURE) {
		PHP_MFFI_RESTORE_ERRORS();
		return;
	}
	PHP_MFFI_RESTORE_ERRORS();

	def = zend_hash_find_ptr(MFFI_G(struct_definitions), GET_CALLED_SCOPE()->name);
	if (def == NULL) {
		zval tmp;
		object_init_ex(&tmp, GET_CALLED_SCOPE());
		php_mffi_struct_get_definition(Z_OBJ(tmp));
		zval_ptr_dtor(&tmp);
	}

	array_init(return_value);
	add_next_index_string(return_value, GET_CALLED_SCOPE()->name->val);
	add_next_index_long(return_value, PHP_MFFI_BY_REFERENCE);
}
/* }}} */

/* {{{ PHP_METHOD(MFFI_Struct, byValue) */
PHP_METHOD(MFFI_Struct, byValue)
{
	php_mffi_struct_definition *def;

	PHP_MFFI_ERROR_HANDLING();
	if (zend_parse_parameters_none() == FAILURE) {
		PHP_MFFI_RESTORE_ERRORS();
		return;
	}
	PHP_MFFI_RESTORE_ERRORS();

	def = zend_hash_find_ptr(MFFI_G(struct_definitions), GET_CALLED_SCOPE()->name);
	if (def == NULL) {
		zval tmp;
		object_init_ex(&tmp, GET_CALLED_SCOPE());
		def = php_mffi_struct_get_definition(Z_OBJ(tmp));
		zval_ptr_dtor(&tmp);
	}

	array_init(return_value);
	add_next_index_string(return_value, GET_CALLED_SCOPE()->name->val);
	add_next_index_long(return_value, PHP_MFFI_BY_VALUE);
}
/* }}} */

/* {{{ PHP_METHOD(MFFI_Struct, define) */
PHP_METHOD(MFFI_Struct, define)
{
	zend_string *class_name = NULL;
	zval *elements = NULL;
	zend_class_entry new_class;
	php_mffi_struct_definition *template;

	PHP_MFFI_ERROR_HANDLING();
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "Sa", &class_name, &elements) == FAILURE) {
		PHP_MFFI_RESTORE_ERRORS();
		return;
	}
	PHP_MFFI_RESTORE_ERRORS();

	if (zend_hash_exists(EG(class_table), class_name)) {
		zend_throw_exception_ex(mffi_ce_exception, 0, "Cannot redeclare class %s", class_name->val);
		return;
	}

	template = php_mffi_make_struct_definition(elements);

	zend_hash_add_ptr(MFFI_G(struct_definitions), class_name, template);

	INIT_CLASS_ENTRY_EX(new_class, class_name->val, class_name->len, NULL);
	zend_register_internal_class_ex(&new_class, mffi_ce_struct);
}
/* }}} */

/* {{{ */
const zend_function_entry mffi_struct_methods[] = {
	PHP_ME(MFFI_Struct, define, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(MFFI_Struct, pointer, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(MFFI_Struct, byReference, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(MFFI_Struct, byValue, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	PHP_ME(MFFI_Struct, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_FE_END
};
/* }}} */

static php_mffi_struct_definition *php_mffi_struct_get_definition(zend_object *obj) {
	/* Class hasn't been made using define(), look for a Class::definition() class method */
	zval elements;
	zend_fcall_info fci;
	int result;
	php_mffi_struct_definition *template;

	fci.size = sizeof(fci);
	ZVAL_STRINGL(&fci.function_name, "definition", sizeof("definition") - 1);
#if PHP_VERSION_ID < 70100
	fci.function_table = &obj->ce->function_table;
	fci.symbol_table = NULL;
#endif
	fci.object = obj;
	fci.retval = &elements;
	fci.param_count = 0;
	fci.params = NULL;
	fci.no_separation = 1;

	result = zend_call_function(&fci, NULL);

	zval_ptr_dtor(&fci.function_name);

	if (EG(exception)) {
		return NULL;
	}

	if (result != SUCCESS) {
		zend_throw_exception_ex(mffi_ce_exception, 0, "Class %s has not been defined correctly: could not call %s::definition()", obj->ce->name->val, obj->ce->name->val);
		return NULL;
	}

	template = php_mffi_make_struct_definition(&elements);

	if (EG(exception)) {
		return NULL;
	}

	zend_hash_add_ptr(MFFI_G(struct_definitions), obj->ce->name, template);
	zval_ptr_dtor(&elements);

	return template;
}

/* {{{ */
static php_mffi_struct_definition *php_mffi_make_struct_definition(zval *elements) {
	HashTable *element_hash = NULL;
	zend_string *string_key = NULL;
	zval *current_elem = NULL;
	long i = 0, php_type = 0, result = 0;
	size_t struct_size = 0;
	zend_ulong num_key = -1;
	ffi_type *type;
	php_mffi_struct_definition *template = NULL, *def = NULL;

	element_hash = Z_ARRVAL_P(elements);
	template = ecalloc(1, sizeof(php_mffi_struct_definition));
	template->element_count = zend_hash_num_elements(element_hash);
	template->elements = ecalloc(template->element_count, sizeof(php_mffi_struct_element));
	template->element_types = ecalloc(template->element_count + 1, sizeof(ffi_type *));

	zend_hash_init(&template->element_hash, 8, NULL, NULL, 0);

	ZEND_HASH_FOREACH_KEY_VAL(element_hash, num_key, string_key, current_elem) {
		if (string_key == NULL) {
			zend_throw_exception(mffi_ce_exception, "Structure elements need names", 1);
			goto cleanup;
		}

		/* TODO: Support user-defined structs */
		if (Z_TYPE_P(current_elem) != IS_LONG && Z_TYPE_P(current_elem) != IS_ARRAY && Z_TYPE_P(current_elem) != IS_STRING) {
			zend_throw_exception_ex(mffi_ce_exception, 0, "Unsupported type for element %s", string_key->val);
			goto cleanup;
		}

		switch(Z_TYPE_P(current_elem)) {
			case IS_LONG:

				type = php_mffi_get_type(Z_LVAL_P(current_elem));
				if (!type) {
					zend_throw_exception(mffi_ce_exception, "Unsupported type", 1);
					goto cleanup;
				}

				php_type = Z_LVAL_P(current_elem);
				break;

			case IS_STRING:
				def = zend_hash_find_ptr(MFFI_G(struct_definitions), Z_STR_P(current_elem));

				if (def == NULL) {
					zend_throw_exception_ex(mffi_ce_exception, 0, "Struct definition %s not found", Z_STRVAL_P(current_elem));
					goto cleanup;
				}

				php_type = FFI_TYPE_POINTER;
				type = &ffi_type_pointer;
				break;

			case IS_ARRAY:
				result = php_mffi_types_from_array(current_elem, &php_type, &type);

				if (!result) {
					zend_throw_exception_ex(mffi_ce_exception, 0, "Invalid argument definition for %s", string_key->val);
					goto cleanup;
				}

				break;
		}


		template->elements[i].php_type = php_type;
		template->elements[i].type = type;
		template->elements[i].index = i;
		template->elements[i].offset = struct_size;
		struct_size += type->size;
		template->element_types[i] = type;

		zend_hash_add_ptr(&template->element_hash, string_key, &template->elements[i]);
		i++;

	} ZEND_HASH_FOREACH_END();

	template->element_types[template->element_count] = NULL;
	template->type.elements = template->element_types;
	template->struct_size = struct_size;

	return template;

cleanup:
	efree(template->element_types);
	efree(template->elements);
	efree(template);

	return NULL;
}
/* }}} */

/* {{{ */
php_mffi_struct_object *php_mffi_struct_fetch_object(zend_object *obj) {
	return (php_mffi_struct_object *)((char*)(obj) - XtOffsetOf(php_mffi_struct_object, std));
}
/* }}} */


/* {{{ */
static zend_object *mffi_struct_object_new(zend_class_entry *ce)
{
	php_mffi_struct_object *object;
	php_mffi_struct_definition *template;
	template = zend_hash_find_ptr(MFFI_G(struct_definitions), ce->name);

	object = ecalloc(1, sizeof(php_mffi_struct_object) + zend_object_properties_size(ce));
	object->template = template;

	zend_object_std_init(&object->std, ce TSRMLS_CC);
	object_properties_init(&object->std, ce);
	object->std.handlers = &mffi_struct_object_handlers;
	return &object->std;
}
/* }}} */

/* {{{ */
static void mffi_struct_object_free_storage(zend_object *object)
{
	php_mffi_struct_object *intern = php_mffi_struct_fetch_object(object);
	php_mffi_struct_element *element;
	size_t data;
	php_mffi_value *val;

	if (!intern) {
		return;
	}

	if (intern->data != NULL) {
		ZEND_HASH_FOREACH_PTR(&intern->template->element_hash, element) {
			data = (size_t) intern->data + element->offset;
			if (element->php_type == PHP_MFFI_TYPE_STRING) {
				val = (php_mffi_value *) data;
				efree(val->p);
			}

		} ZEND_HASH_FOREACH_END();

		efree(intern->data);
	}

	zend_object_std_dtor(&intern->std);
}
/* }}} */

/* {{{ */
static zval *php_mffi_struct_read_property(zval *object, zval *member, int type, void **cache_slot, zval *rv) {
	php_mffi_struct_object *intern = NULL;
	php_mffi_struct_element *element = NULL;
	php_mffi_value *val = NULL;
	zval tmp;
	zend_string *member_key;
	size_t data;

	if (Z_TYPE_P(member) != IS_STRING) {
		tmp = *member;
		zval_copy_ctor(&tmp);
		convert_to_string(&tmp);
		member = &tmp;
	}

	member_key = Z_STR_P(member);

	intern = php_mffi_struct_fetch_object(Z_OBJ_P(object));

	/* TODO - there has to be a better way */
	element = zend_hash_find_ptr(&intern->template->element_hash, member_key);

	if (!element) {
		rv = &EG(uninitialized_zval);
		return rv;
	}

	/* TODO - there has to be a safer way to add bytes to a pointer? */
	data = (size_t) intern->data + element->offset;
	val = (php_mffi_value *) data;

	php_mffi_set_return_value(rv, val, element->php_type);
	return rv;
}
/* }}} */

/* {{{ */
static HashTable *php_mffi_struct_get_properties(zval *object) {
	php_mffi_struct_object *intern = NULL;
	zend_string *key;
	php_mffi_struct_element *element;
	HashTable *props;
	zval ret;
	size_t data;

	intern = php_mffi_struct_fetch_object(Z_OBJ_P(object));
	props = zend_std_get_properties(object);

	ZEND_HASH_FOREACH_STR_KEY_PTR(&intern->template->element_hash, key, element) {
		data = (size_t) intern->data + element->offset;
		php_mffi_set_return_value(&ret, (php_mffi_value *) data, element->php_type);
		zend_hash_update(props, key, &ret);
	} ZEND_HASH_FOREACH_END();

	return intern->std.properties;
}
/* }}} */

/* {{{ */
static int php_mffi_struct_has_property(zval *object, zval *member, int has_set_exists, void **cache_slot)
{
	php_mffi_struct_object *intern;
	zval tmp, val, *value = NULL;
	int ret = 0;

	intern = php_mffi_struct_fetch_object(Z_OBJ_P(object));

	if (Z_TYPE_P(member) != IS_STRING) {
		ZVAL_STR(&tmp, zval_get_string(member));
		member = &tmp;
	}

	if (zend_hash_find(&intern->template->element_hash, Z_STR_P(member)) == NULL) {
		return 0;
	}

	switch (has_set_exists) {
		case 2:
			ret = 1;
			break;
		case 0:
			php_mffi_struct_read_property(object, member, 0, NULL, &val);
			ret = Z_TYPE(val) != IS_NULL? 1:0;
			break;
		default:
			value = php_mffi_struct_read_property(object, member, 0, NULL, &val);
			if (value != &EG(uninitialized_zval)) {
				tmp = *value;
				zval_copy_ctor(&tmp);
				convert_to_boolean(&tmp);
				ret = Z_TYPE(tmp) == IS_TRUE;
			}

			break;
	}

	return ret;
}
/* }}} */

static void php_mffi_struct_write_property(zval *object, zval *member, zval *value, void **cache_slot) /* {{{ */
{
	php_mffi_struct_object *intern;
	php_mffi_value *val;
	zval tmp;
	size_t data = 0;
	php_mffi_struct_element *element;
	zend_string *member_key;

	intern = php_mffi_struct_fetch_object(Z_OBJ_P(object));

	if (Z_TYPE_P(member) != IS_STRING) {
		ZVAL_STR(&tmp, zval_get_string(member));
		member = &tmp;
	}

	member_key = Z_STR_P(member);

	element = zend_hash_find_ptr(&intern->template->element_hash, member_key);

	if (!element) {
		return;
	}

	/* TODO - there has to be a safer way to add bytes to a pointer? */
	data = (size_t) intern->data + element->offset;
	val = (php_mffi_value *) data;

	php_mffi_set_argument(value, val, element->php_type);
}
/* }}} */


/* {{{ */
PHP_MINIT_FUNCTION(mffi_struct)
{
	zend_class_entry function_ce;

	memcpy(&mffi_struct_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	mffi_struct_object_handlers.offset = XtOffsetOf(php_mffi_struct_object, std);
	mffi_struct_object_handlers.free_obj = mffi_struct_object_free_storage;
	mffi_struct_object_handlers.clone_obj = NULL;
	mffi_struct_object_handlers.read_property = php_mffi_struct_read_property;
	mffi_struct_object_handlers.write_property = php_mffi_struct_write_property;
	mffi_struct_object_handlers.has_property = php_mffi_struct_has_property;
	mffi_struct_object_handlers.get_properties = php_mffi_struct_get_properties;

	INIT_NS_CLASS_ENTRY(function_ce, "MFFI", "Struct", mffi_struct_methods);
	function_ce.create_object = mffi_struct_object_new;
	mffi_ce_struct = zend_register_internal_class(&function_ce);

	return SUCCESS;
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
